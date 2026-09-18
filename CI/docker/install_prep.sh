#/bin/bash
set -xe

export WORKDIR=/home/docker
export SRCDIR="${SRCDIR:-$SRCDIR}"
export STAGING_DIR="/mingw64"
export STAGING_BIN="$STAGING_DIR/bin"
export DLLS="$STAGING_BIN/libad9166.dll \
$STAGING_BIN/libad9361.dll \
$STAGING_BIN/msvcp140.dll \
$STAGING_BIN/vcruntime140.dll \
$STAGING_BIN/libatk-1.0-0.dll \
$STAGING_BIN/libbrotlicommon.dll \
$STAGING_BIN/libbrotlidec.dll \
$STAGING_BIN/libbz2-1.dll \
$STAGING_BIN/libcairo-2.dll \
$STAGING_BIN/libcairo-gobject-2.dll \
$STAGING_BIN/libcrypto-3-x64.dll \
$STAGING_BIN/libcurl-4.dll \
$STAGING_BIN/libdatrie-1.dll \
$STAGING_BIN/libepoxy-0.dll \
$STAGING_BIN/libexpat-1.dll \
$STAGING_BIN/libffi-8.dll \
$STAGING_BIN/libfftw3-3.dll \
$STAGING_BIN/libfontconfig-1.dll \
$STAGING_BIN/libfreetype-6.dll \
$STAGING_BIN/libfribidi-0.dll \
$STAGING_BIN/libgcc_s_seh-1.dll \
$STAGING_BIN/libgdk_pixbuf-2.0-0.dll \
$STAGING_BIN/libgdk-3-0.dll \
$STAGING_BIN/libgio-2.0-0.dll \
$STAGING_BIN/libglib-2.0-0.dll \
$STAGING_BIN/libgmodule-2.0-0.dll \
$STAGING_BIN/libgobject-2.0-0.dll \
$STAGING_BIN/libgraphite2.dll \
$STAGING_BIN/libgthread-2.0-0.dll \
$STAGING_BIN/libgtk-3-0.dll \
$STAGING_BIN/libgtkdatabox-1.dll \
$STAGING_BIN/libharfbuzz-0.dll \
$STAGING_BIN/libhdf5-310.dll \
$STAGING_BIN/libiconv-2.dll \
$STAGING_BIN/libidn2-0.dll \
$STAGING_BIN/libiio.dll \
$STAGING_BIN/libintl-8.dll \
$STAGING_BIN/libjansson-4.dll \
$STAGING_BIN/liblzma-5.dll \
$STAGING_BIN/libmatio-11.dll \
$STAGING_BIN/libnghttp2-14.dll \
$STAGING_BIN/libpango-1.0-0.dll \
$STAGING_BIN/libpangocairo-1.0-0.dll \
$STAGING_BIN/libpangoft2-1.0-0.dll \
$STAGING_BIN/libpangowin32-1.0-0.dll \
$STAGING_BIN/libpcre2-8-0.dll \
$STAGING_BIN/libpcre2-32-0.dll \
$STAGING_BIN/libpixman-1-0.dll \
$STAGING_BIN/libpng16-16.dll \
$STAGING_BIN/libpsl-5.dll \
$STAGING_BIN/librsvg-2-2.dll \
$STAGING_BIN/libserialport-0.dll \
$STAGING_BIN/libssh2-1.dll \
$STAGING_BIN/libssl-3-x64.dll \
$STAGING_BIN/libstdc++-6.dll \
$STAGING_BIN/libsz.dll \
$STAGING_BIN/libthai-0.dll \
$STAGING_BIN/libunistring-5.dll \
$STAGING_BIN/libusb-1.0.dll \
$STAGING_BIN/libwinpthread-1.dll \
$STAGING_BIN/libxml2-2.dll \
$STAGING_BIN/libzstd.dll \
$STAGING_BIN/zlib1.dll
"
export EXES="$STAGING_BIN/curl.exe \
$STAGING_BIN/iio_genxml.exe \
$STAGING_BIN/iio_info.exe \
$STAGING_BIN/iio_readdev.exe
"

bin_dir() {
	pushd "$SRCDIR"
	mkdir $SRCDIR/build/bin
	cp  $SRCDIR/build/osc.exe $SRCDIR/build/bin/
	cp  $SRCDIR/build/styles.css $SRCDIR/build/bin/
	cp  $SRCDIR/build/libosc.dll $SRCDIR/build/bin/

	cp $DLLS $SRCDIR/build/bin/
	cp -r $EXES $SRCDIR/build/bin/

	cp -r $SRCDIR/build/plugins $SRCDIR/build/bin/
	cp -r $SRCDIR/glade $SRCDIR/build/bin/
	cp -r $SRCDIR/block_diagrams $SRCDIR/build/bin/
	cp -r $SRCDIR/icons $SRCDIR/build/bin/
	cp -r $SRCDIR/xmls $SRCDIR/build/bin/
	popd
}

lib_dir() {
	pushd "$SRCDIR"
	mkdir $SRCDIR/build/lib
	cp -r $STAGING_DIR/lib/gdk-pixbuf-2.0 $SRCDIR/build/lib
	cp -r $STAGING_DIR/lib/gtk-3.0 $SRCDIR/build/lib
	mkdir $SRCDIR/build/lib/osc
	cp -r $SRCDIR/filters $SRCDIR/build/lib/osc
	cp -r $SRCDIR/build/profiles $SRCDIR/build/lib/osc
	cp -r $SRCDIR/waveforms $SRCDIR/build/lib/osc
	cp $SRCDIR/build/plugins/*.dll $SRCDIR/build/lib/osc
	popd
}

share_dir() {
	pushd "$SRCDIR"
	mkdir $SRCDIR/build/share
	cp -r $STAGING_DIR/share/locale $SRCDIR/build/share
	cp -r $STAGING_DIR/share/themes $SRCDIR/build/share
	mkdir $SRCDIR/build/share/icons
	cp -r $STAGING_DIR/share/icons/Adwaita $SRCDIR/build/share/icons
	cp -r $STAGING_DIR/share/icons/hicolor $SRCDIR/build/share/icons
	mkdir $SRCDIR/build/share/glib-2.0
	cp -r $STAGING_DIR/share/glib-2.0/schemas $SRCDIR/build/share/glib-2.0
	popd

}

lib_dir
share_dir
bin_dir
$@
