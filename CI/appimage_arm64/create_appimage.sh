#!/bin/bash

set -xe

# --- Configuration -----------------------------------------------------------
TARGET_ARCH="aarch64"
BUILD_DIR="/workspace"
INSTALL_PREFIX="${BUILD_DIR}/install"
APPDIR="${BUILD_DIR}/AppDir"
APPIMAGE_OUT="${BUILD_DIR}/iio-oscilloscope-${TARGET_ARCH}.AppImage"
JOBS=$(nproc)

# Colours
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
log()  { echo -e "${GREEN}[+]${NC} $*"; }
warn() { echo -e "${YELLOW}[!]${NC} $*"; }

build_iio_oscilloscope() {
    log "Building iio-oscilloscope..."
    local SRC="${BUILD_DIR}/iio-oscilloscope"
    local BLD="${BUILD_DIR}/osc"

    # Clean and create build directory to ensure fresh cmake configuration
    rm -rf "${BLD}"
    mkdir -p "${BLD}"

    export PKG_CONFIG_PATH="${INSTALL_PREFIX}/lib/pkgconfig:/usr/lib/$(uname -m)-linux-gnu/pkgconfig:/usr/share/pkgconfig"
    log "PKG_CONFIG_PATH=${PKG_CONFIG_PATH}"

    cmake -S "${SRC}" -B "${BLD}" \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="${INSTALL_PREFIX}" \
        -DPKG_CONFIG_USE_CMAKE_PREFIX_PATH=ON \
        -DCMAKE_SHARED_LINKER_FLAGS="-L${INSTALL_PREFIX}/lib -Wl,-rpath-link,${INSTALL_PREFIX}/lib" \
        -DCMAKE_EXE_LINKER_FLAGS="-L${INSTALL_PREFIX}/lib -Wl,-rpath-link,${INSTALL_PREFIX}/lib" \
        -DWITH_PKEXEC=OFF \
        -GNinja

    cmake --build "${BLD}" -j"${JOBS}"

    log "Staging iio-oscilloscope..."
    mkdir -p "${INSTALL_PREFIX}/usr/bin" \
             "${INSTALL_PREFIX}/usr/lib/osc" \
             "${INSTALL_PREFIX}/usr/share/osc"

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

    # Icons into glade/ for relative references
    if [ -d "${INSTALL_PREFIX}/usr/share/osc/icons" ] && [ -d "${INSTALL_PREFIX}/usr/share/osc/glade" ]; then
        find "${INSTALL_PREFIX}/usr/share/osc/icons" -name "*.png" -exec cp -v {} "${INSTALL_PREFIX}/usr/share/osc/glade/" \;
    fi
}

fetch_appimage_tools() {
    local TOOLS_DIR="${BUILD_DIR}/appimage-tools"
    mkdir -p "${TOOLS_DIR}"
    local TOOL="appimagetool-aarch64.AppImage"
    local URL="https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-aarch64.AppImage"

    if [[ ! -f "${TOOLS_DIR}/${TOOL}" ]]; then
        log "Downloading appimagetool..."
        wget -q --show-progress -O "${TOOLS_DIR}/${TOOL}" "${URL}"
        chmod +x "${TOOLS_DIR}/${TOOL}"
    fi
}

build_appimage() {
    local TOOLS_DIR="${BUILD_DIR}/appimage-tools"
    local APPIMAGETOOL="${TOOLS_DIR}/appimagetool-aarch64.AppImage"

    log "Populating AppDir..."
    rm -rf "${APPDIR}"
    mkdir -p "${APPDIR}/usr"

    # 1. Copy everything under usr/ (osc binary, plugins, glade, etc.)
    cp -a "${INSTALL_PREFIX}/usr/." "${APPDIR}/usr/"
    cp -a "${INSTALL_PREFIX}/lib/." "${APPDIR}/usr/lib/" 2>/dev/null || true
    cp -a /usr/local/lib/libgtkdatabox*.so* "${APPDIR}/usr/lib/" 2>/dev/null || true
    cp -a /lib/aarch64-linux-gnu/libiio*.so* "${APPDIR}/usr/lib/" 2>/dev/null || true
    cp -a /lib/aarch64-linux-gnu/libad9*.so* "${APPDIR}/usr/lib/" 2>/dev/null || true

    # Symlinks for relative paths used by the original code
    ln -sf usr/lib/osc     "${APPDIR}/plugins" || true
    ln -sf usr/share/osc/glade "${APPDIR}/glade" || true

    # AppRun with cd + full library path
    cat > "${APPDIR}/AppRun" << 'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
cd "$HERE" || { echo "Failed to cd into AppImage"; exit 1; }

export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"

export GTK_MODULES=""
export JOURNAL_STREAM=""
export SYSTEMD_LOG_TARGET=stderr
export G_MESSAGES_DEBUG=""

export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

export OSC_GLADE_FILE_PATH="$HERE/usr/share/osc/glade"
export OSC_PLUGIN_PATH="$HERE/usr/lib/osc"
export OSC_GLADE_DIR="$HERE/usr/share/osc/glade"
export OSC_FILTER_PATH="$HERE/usr/share/osc/filters"
export OSC_WAVEFORM_PATH="$HERE/usr/share/osc/waveforms"
export OSC_PROFILE_PATH="$HERE/usr/share/osc/profiles"
export OSC_BLOCK_DIAGRAM_PATH="$HERE/usr/share/osc/block_diagrams"
export OSC_STYLE_PATH="$HERE/usr/share/osc/styles.css"

exec "$HERE/usr/bin/osc" "$@"
APPRUN
    chmod +x "${APPDIR}/AppRun"

    # Desktop file + icon
    cat > "${APPDIR}/iio-oscilloscope.desktop" << 'DESKTOP'
[Desktop Entry]
Name=IIO Oscilloscope
Comment=GTK+ oscilloscope for IIO devices
Exec=osc
Icon=iio-oscilloscope
Type=Application
Categories=Science;Electronics;
DESKTOP

    local ICON_SRC=$(find "${INSTALL_PREFIX}" -name "*.png" 2>/dev/null | head -n 1)
    if [[ -n "$ICON_SRC" ]]; then
        cp "$ICON_SRC" "${APPDIR}/iio-oscilloscope.png"
    else
        printf '\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\nIDATx\x9cc\x00\x01\x00\x00\x05\x00\x01\r\n-\xb4\x00\x00\x00\x00IEND\xaeB`\x82' > "${APPDIR}/iio-oscilloscope.png"
    fi

    log "Packing AppImage..."
    rm -f "${APPIMAGE_OUT}"
    export APPIMAGE_EXTRACT_AND_RUN=1
    ARCH=aarch64 "${APPIMAGETOOL}" --no-appstream "${APPDIR}" "${APPIMAGE_OUT}"
    chmod +x "${APPIMAGE_OUT}"
    log "AppImage created → ${APPIMAGE_OUT}"
}

main() {
  build_iio_oscilloscope
  fetch_appimage_tools
  build_appimage
  log "All done! AppImage is ready."
}

main
