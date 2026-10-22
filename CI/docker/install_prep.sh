#!/bin/bash
set -xe

export REPODIR="${REPODIR:-$(pwd)}"
export STAGING_DIR="${STAGING_DIR:-/ucrt64}"
export STAGING_BIN="$STAGING_DIR/bin"
export EXES="$STAGING_BIN/curl.exe \
$STAGING_BIN/iio_genxml.exe \
$STAGING_BIN/iio_info.exe \
$STAGING_BIN/iio_readdev.exe
"

bin_dir() {
	mkdir -p $REPODIR/build/bin
	cp  $REPODIR/build/osc.exe $REPODIR/build/bin/
	cp  $REPODIR/build/styles.css $REPODIR/build/bin/
	cp  $REPODIR/build/libosc.dll $REPODIR/build/bin/

	for f in $EXES; do cp -r "$f" $REPODIR/build/bin/ 2>/dev/null || true; done

	cp -r $REPODIR/build/plugins $REPODIR/build/bin/

	pacman -Qq 2>/dev/null \
		| grep "^mingw-w64-ucrt-x86_64-" \
		| xargs pacman -Ql 2>/dev/null \
		| awk '$2 ~ /\/ucrt64\/bin\/.*\.dll$/ {print $2}' \
		| sort -u \
		| xargs -r -I{} cp -u {} "$REPODIR/build/bin/"

	# ADI libs land in /ucrt64/bin via get_adi_deps but are not pacman packages
	cp -u "$STAGING_BIN"/libad9361.dll "$REPODIR/build/bin/" 2>/dev/null || true
	cp -u "$STAGING_BIN"/libad9166.dll "$REPODIR/build/bin/" 2>/dev/null || true

	local dll_count
	dll_count=$(find "$REPODIR/build/bin" -maxdepth 1 -name '*.dll' | wc -l)
	if [ "$dll_count" -lt 20 ]; then
		echo "install_prep: only $dll_count DLLs copied -- pacman query likely failed" >&2
		exit 1
	fi

	cp -r $REPODIR/glade $REPODIR/build/bin/
	cp -r $REPODIR/block_diagrams $REPODIR/build/bin/
	cp -r $REPODIR/icons $REPODIR/build/bin/
	cp -r $REPODIR/xmls $REPODIR/build/bin/
}

lib_dir() {
	mkdir -p $REPODIR/build/lib
	cp -r $STAGING_DIR/lib/gdk-pixbuf-2.0 $REPODIR/build/lib
	cp -r $STAGING_DIR/lib/gtk-3.0 $REPODIR/build/lib
	mkdir -p $REPODIR/build/lib/osc
	cp -r $REPODIR/filters $REPODIR/build/lib/osc
	cp -r $REPODIR/build/profiles $REPODIR/build/lib/osc
	cp -r $REPODIR/waveforms $REPODIR/build/lib/osc
	cp $REPODIR/build/plugins/*.dll $REPODIR/build/lib/osc
}

share_dir() {
	mkdir -p $REPODIR/build/share/icons $REPODIR/build/share/glib-2.0
	cp -r $STAGING_DIR/share/locale $REPODIR/build/share
	cp -r $STAGING_DIR/share/themes $REPODIR/build/share
	cp -r $STAGING_DIR/share/icons/Adwaita $REPODIR/build/share/icons
	cp -r $STAGING_DIR/share/icons/hicolor $REPODIR/build/share/icons
	cp -r $STAGING_DIR/share/glib-2.0/schemas $REPODIR/build/share/glib-2.0
}

lib_dir
share_dir
bin_dir
$@
