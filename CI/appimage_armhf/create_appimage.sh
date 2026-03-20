#!/bin/bash
set -xe

TARGET_ARCH="armhf"
BUILD_DIR="/workspace"
INSTALL_PREFIX="${BUILD_DIR}/install"
APPDIR="${BUILD_DIR}/AppDir"
APPIMAGE_OUT="${BUILD_DIR}/iio-oscilloscope-${TARGET_ARCH}.AppImage"
JOBS=$(nproc)

log()  { echo -e "\033[0;32m[+]\033[0m $*"; }
warn() { echo -e "\033[1;33m[!]\033[0m $*"; }


build_osc() {
    local SRC="${BUILD_DIR}/iio-oscilloscope"
    local BLD="${BUILD_DIR}/osc"
    mkdir -p "$BLD"
    cd "$BLD"
    cmake "$SRC" -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX" \
          -DCMAKE_SHARED_LINKER_FLAGS="-L$INSTALL_PREFIX/lib" \
          -DWITH_PKEXEC=OFF -GNinja
    cmake --build . -j"$JOBS"

    mkdir -p "$INSTALL_PREFIX/usr/bin" "$INSTALL_PREFIX/usr/lib/osc" "$INSTALL_PREFIX/usr/share/osc"
    cp osc "$INSTALL_PREFIX/usr/bin/osc" 2>/dev/null || cp osc/osc "$INSTALL_PREFIX/usr/bin/osc"
    cp libosc.so* "$INSTALL_PREFIX/usr/lib/" 2>/dev/null
    cp -r plugins/*.so "$INSTALL_PREFIX/usr/lib/osc/" 2>/dev/null || true

    for d in glade filters profiles waveforms icons xmls block_diagrams; do
        cp -r "$SRC/$d" "$INSTALL_PREFIX/usr/share/osc/" 2>/dev/null && log "Copied $d"
    done

    cp "$SRC/styles.css" "$INSTALL_PREFIX/usr/share/osc/" 2>/dev/null && log "Copied styles.css" || warn "styles.css missing!"
    cp "$SRC"/icons/*.png "$INSTALL_PREFIX/usr/share/osc/glade/" 2>/dev/null || true

    cd - >/dev/null
    log "osc built & staged"
}

# =============================================================================
# AppImage tooling & packaging
# =============================================================================
fetch_appimagetool() {
    mkdir -p "${BUILD_DIR}/tools"
    cd "${BUILD_DIR}/tools"
    if [[ ! -f appimagetool-armhf.AppImage ]]; then
        wget https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-armhf.AppImage
        chmod +x appimagetool-armhf.AppImage
    fi
    cd - >/dev/null
}

build_appimage() {
    local TOOL="${BUILD_DIR}/tools/appimagetool-armhf.AppImage"

    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr"
    cp -a "$INSTALL_PREFIX/usr/." "$APPDIR/usr/"

    mkdir -p "$APPDIR/usr/lib"
    cp -a "$INSTALL_PREFIX/lib/." "$APPDIR/usr/lib/" 2>/dev/null || true
    cp -a /usr/local/lib/libgtkdatabox*.so* "${APPDIR}/usr/lib/" 2>/dev/null || true
    cp -a /lib/arm-linux-gnueabihf/libiio*.so* "${APPDIR}/usr/lib/" 2>/dev/null || true
    cp -a /lib/arm-linux-gnueabihf/libad9*.so* "${APPDIR}/usr/lib/" 2>/dev/null || true
    ln -sf usr/lib/osc     "$APPDIR/plugins"  || true
    ln -sf usr/share/osc/glade "$APPDIR/glade" || true

    cat > "$APPDIR/AppRun" << 'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
cd "$HERE" || exit 1
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/share}"
export OSC_GLADE_FILE_PATH="$HERE/usr/share/osc/glade"
export OSC_PLUGIN_PATH="$HERE/usr/lib/osc"
export OSC_STYLE_PATH="$HERE/usr/share/osc/styles.css"
export OSC_FILTER_PATH="$HERE/usr/share/osc/filters"
export OSC_WAVEFORM_PATH="$HERE/usr/share/osc/waveforms"
export OSC_PROFILE_PATH="$HERE/usr/share/osc/profiles"
export OSC_BLOCK_DIAGRAM_PATH="$HERE/usr/share/osc/block_diagrams"
exec "$HERE/usr/bin/osc" "$@"
EOF
    chmod +x "$APPDIR/AppRun"

    cat > "$APPDIR/iio-oscilloscope.desktop" << 'EOF'
[Desktop Entry]
Name=IIO Oscilloscope
Exec=osc
Icon=iio-oscilloscope
Type=Application
Categories=Science;Electronics;
EOF

    # Icon placeholder or real
    cp "$INSTALL_PREFIX"/usr/share/osc/*/*.png "$APPDIR/iio-oscilloscope.png" 2>/dev/null || \
       printf '\x89PNG...\x82' > "$APPDIR/iio-oscilloscope.png"  # tiny transparent
    export APPIMAGE_EXTRACT_AND_RUN=1
    "$TOOL" --no-appstream "$APPDIR" "$APPIMAGE_OUT"
    chmod +x "$APPIMAGE_OUT"
    log "AppImage created: $APPIMAGE_OUT"
}

# =============================================================================
main() {
    build_osc
    fetch_appimagetool
    build_appimage
    log "Build finished."
}

main
