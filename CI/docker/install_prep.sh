#/bin/bash
set -xe

export WORKDIR=/home/docker
export STAGING_DIR="/mingw64"
export STAGING_BIN="$STAGING_DIR/bin"
export DLLS="$STAGING_BIN/libad9166.dll \
$STAGING_BIN/libad9361.dll \
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
	pushd $WORKDIR
	mkdir $WORKDIR/iio-oscilloscope/build/bin
	cp  $WORKDIR/iio-oscilloscope/build/osc.exe $WORKDIR/iio-oscilloscope/build/bin/
	cp  $WORKDIR/iio-oscilloscope/build/styles.css $WORKDIR/iio-oscilloscope/build/bin/
	cp  $WORKDIR/iio-oscilloscope/build/libosc.dll $WORKDIR/iio-oscilloscope/build/bin/

	cp $DLLS $WORKDIR/iio-oscilloscope/build/bin/
	cp -r $EXES $WORKDIR/iio-oscilloscope/build/bin/

	cp -r $WORKDIR/iio-oscilloscope/build/plugins $WORKDIR/iio-oscilloscope/build/bin/
	cp -r $WORKDIR/iio-oscilloscope/glade $WORKDIR/iio-oscilloscope/build/bin/
	cp -r $WORKDIR/iio-oscilloscope/block_diagrams $WORKDIR/iio-oscilloscope/build/bin/
	cp -r $WORKDIR/iio-oscilloscope/icons $WORKDIR/iio-oscilloscope/build/bin/
	cp -r $WORKDIR/iio-oscilloscope/xmls $WORKDIR/iio-oscilloscope/build/bin/
	popd
}

lib_dir() {
	pushd $WORKDIR
	mkdir $WORKDIR/iio-oscilloscope/build/lib
	cp -r $STAGING_DIR/lib/gdk-pixbuf-2.0 $WORKDIR/iio-oscilloscope/build/lib
	cp -r $STAGING_DIR/lib/gtk-3.0 $WORKDIR/iio-oscilloscope/build/lib
	mkdir $WORKDIR/iio-oscilloscope/build/lib/osc
	cp -r $WORKDIR/iio-oscilloscope/filters $WORKDIR/iio-oscilloscope/build/lib/osc
	cp -r $WORKDIR/iio-oscilloscope/build/profiles $WORKDIR/iio-oscilloscope/build/lib/osc
	cp -r $WORKDIR/iio-oscilloscope/waveforms $WORKDIR/iio-oscilloscope/build/lib/osc
	cp $WORKDIR/iio-oscilloscope/build/plugins/*.dll $WORKDIR/iio-oscilloscope/build/lib/osc
	popd
}

share_dir() {
	pushd $WORKDIR
	mkdir $WORKDIR/iio-oscilloscope/build/share
	cp -r $STAGING_DIR/share/locale $WORKDIR/iio-oscilloscope/build/share
	cp -r $STAGING_DIR/share/themes $WORKDIR/iio-oscilloscope/build/share
	mkdir $WORKDIR/iio-oscilloscope/build/share/icons
	cp -r $STAGING_DIR/share/icons/Adwaita $WORKDIR/iio-oscilloscope/build/share/icons
	cp -r $STAGING_DIR/share/icons/hicolor $WORKDIR/iio-oscilloscope/build/share/icons
	mkdir $WORKDIR/iio-oscilloscope/build/share/glib-2.0
	cp -r $STAGING_DIR/share/glib-2.0/schemas $WORKDIR/iio-oscilloscope/build/share/glib-2.0
	popd

}

lib_dir
share_dir
bin_dir
$@
