#!/bin/bash

set -xe
set -euo pipefail

TARGET_ARCH="armhf"
BUILD_DIR="/workspace"
INSTALL_PREFIX="${BUILD_DIR}/install"
APPDIR="${BUILD_DIR}/AppDir"
APPIMAGE_OUT="${BUILD_DIR}/iio-oscilloscope-${TARGET_ARCH}.AppImage"
JOBS=$(nproc)

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
log()  { echo -e "${GREEN}[+]${NC} $*"; }
warn() { echo -e "${YELLOW}[!]${NC} $*"; }
die()  { echo -e "${RED}[x]${NC} $*" >&2; exit 1; }


build_iio_oscilloscope() {
    log "Building iio-oscilloscope..."
    local SRC="${BUILD_DIR}/iio-oscilloscope"
    local BLD="${BUILD_DIR}/osc"
    mkdir -p "$BLD"
    cd "$BLD"

    export PKG_CONFIG_PATH="${INSTALL_PREFIX}/lib/pkgconfig:/usr/lib/$(uname -m)-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig"
    
    cmake -S "${SRC}" -B "${BLD}" \
        -DCMAKE_INSTALL_PREFIX="/usr" \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DCMAKE_SKIP_RPATH=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="${INSTALL_PREFIX}" \
        -DCMAKE_SHARED_LINKER_FLAGS="-L${INSTALL_PREFIX}/lib -Wl,-rpath-link,${INSTALL_PREFIX}/lib" \
        -DCMAKE_EXE_LINKER_FLAGS="-L${INSTALL_PREFIX}/lib -Wl,-rpath-link,${INSTALL_PREFIX}/lib" \
        -DWITH_PKEXEC=OFF \
        -GNinja
    cmake --build "${BLD}" -j"${JOBS}"

    log "Staging iio-oscilloscope..."

    mkdir -p "$INSTALL_PREFIX/usr/bin" "$INSTALL_PREFIX/usr/lib/osc" "$INSTALL_PREFIX/usr/share/osc"
    [ -f "${BLD}/osc" ] && cp -v "${BLD}/osc" "${INSTALL_PREFIX}/usr/bin/osc"
    find "${BLD}" -maxdepth 1 -name "libosc.so*" -exec cp -Pv {} "${INSTALL_PREFIX}/usr/lib/" \;
    find "${BLD}/plugins" -name "*.so" 2>/dev/null -exec cp -v {} "${INSTALL_PREFIX}/usr/lib/osc/" \;

    for dir in glade filters profiles waveforms icons xmls block_diagrams; do
        if [ -d "${SRC}/${dir}" ]; then
            cp -r "${SRC}/${dir}" "${INSTALL_PREFIX}/usr/share/osc/"
            log "Copied ${dir}/"
        fi
    done

    [ -f "${SRC}/styles.css" ] && cp -v "${SRC}/styles.css" "${INSTALL_PREFIX}/usr/share/osc/"
    if [ -d "${INSTALL_PREFIX}/usr/share/osc/icons" ] && [ -d "${INSTALL_PREFIX}/usr/share/osc/glade" ]; then
        find "${INSTALL_PREFIX}/usr/share/osc/icons" -name "*.png" -exec cp -v {} "${INSTALL_PREFIX}/usr/share/osc/glade/" \;
    fi

    cd - >/dev/null
    log "osc built & staged"
}

build_appimage() {
    local TOOL="${BUILD_DIR}/tools/appimagetool-armhf.AppImage"

    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr"
    cp -a "$INSTALL_PREFIX/usr/." "$APPDIR/usr/"

    mkdir -p "$APPDIR/usr/lib"
    if [ -d "${INSTALL_PREFIX}/lib" ]; then
        cp -a "${INSTALL_PREFIX}/lib/." "${APPDIR}/usr/lib/" 2>/dev/null || true
        log "All libraries from install/lib/ copied into AppDir/usr/lib/"
    fi

    # Copy libgtkdatabox from the system (installed via apt, not built from
    # source, so it is not under INSTALL_PREFIX and must be copied explicitly).
    # Use dpkg-architecture to get the correct Debian multiarch triplet —
    # on armhf this is arm-linux-gnueabihf, which differs from uname -m output.
    local ARCH_TRIPLET
    ARCH_TRIPLET="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"
    local GTKDATABOX_LIBS
    GTKDATABOX_LIBS=$(find "/usr/lib/${ARCH_TRIPLET}" -name "libgtkdatabox*.so*" 2>/dev/null)
    [[ -n "${GTKDATABOX_LIBS}" ]] || die "libgtkdatabox not found in /usr/lib/${ARCH_TRIPLET} — is libgtkdatabox-dev installed?"
    echo "${GTKDATABOX_LIBS}" | xargs -I{} cp -av {} "${APPDIR}/usr/lib/"
    log "libgtkdatabox bundled into AppDir."

    # Copy libiio, libad9361, libad9166 from system (installed via .deb)
    for lib in libiio libad9361 libad9166; do
        LIB_FILES=$(find "/usr/lib/${ARCH_TRIPLET}" -name "${lib}*.so*" 2>/dev/null)
        if [[ -n "${LIB_FILES}" ]]; then
            echo "${LIB_FILES}" | xargs -I{} cp -av {} "${APPDIR}/usr/lib/"
            log "${lib} bundled into AppDir."
        else
            warn "${lib} not found in /usr/lib/${ARCH_TRIPLET}"
        fi
    done

    ln -sf usr/lib/osc          "${APPDIR}/plugins" || true
    ln -sf usr/share/osc/glade  "${APPDIR}/glade"   || true

    # AppRun — entry point invoked by makeself after extraction
    cat > "${APPDIR}/AppRun" << 'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
cd "$HERE" || { echo "Failed to cd into AppDir"; exit 1; }

export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
export GTK_MODULES=""
export JOURNAL_STREAM=""
export SYSTEMD_LOG_TARGET=stderr
export G_MESSAGES_DEBUG=""
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

OSC_SHARE="$HOME/.local/share/osc"
OSC_LIB="$HOME/.local/lib/osc"

mkdir -p "$OSC_SHARE" "$OSC_LIB"

for dir in glade icons block_diagrams; do
    [ -d "$OSC_SHARE/$dir" ] || cp -r "$HERE/usr/share/osc/$dir" "$OSC_SHARE/$dir"
done
[ -f "$OSC_SHARE/styles.css" ] || cp "$HERE/usr/share/osc/styles.css" "$OSC_SHARE/styles.css"

for dir in filters waveforms profiles xmls block_diagrams; do
    [ -d "$OSC_LIB/$dir" ] || cp -r "$HERE/usr/share/osc/$dir" "$OSC_LIB/$dir"
done

exec "$HERE/usr/bin/osc" "$@"
APPRUN
    chmod +x "${APPDIR}/AppRun"

    log "Packing self-extracting archive with makeself..."
    rm -f "${APPIMAGE_OUT}"
    makeself --tar-quietly \
        "${APPDIR}" \
        "${APPIMAGE_OUT}" \
        "IIO Oscilloscope" \
        ./AppRun
    chmod +x "${APPIMAGE_OUT}"
    log "AppImage created → ${APPIMAGE_OUT}"
    log "Single file, no FUSE required. Copy and run on any armhf machine."

}

package_tarball() {
    local TARBALL="${BUILD_DIR}/iio-oscilloscope-armhf.tar.gz"
    log "Creating tarball → ${TARBALL}"
    tar -czf "${TARBALL}" -C "${INSTALL_PREFIX}" .
}

main() {
    log "==================================================="
    log "          iio-oscilloscope armhf AppImage          "
    log "==================================================="

    build_iio_oscilloscope
    build_appimage
    package_tarball

    log "All done! AppImage is ready."
}

main "$@"
