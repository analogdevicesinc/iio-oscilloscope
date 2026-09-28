#/bin/bash
set -xe

export WORKDIR=/home/docker
export SRCDIR="${SRCDIR:-$SRCDIR}"
export STAGING_DIR="/mingw64"
export STAGING_BIN="$STAGING_DIR/bin"
# The runtime DLL closure is resolved dynamically from /mingw64 in bin_dir()
# via ldd, instead of hardcoding a soname list here. The old static list broke
# whenever the MSYS2 packages updated (version-suffixed names like libhdf5-310
# / libmatio-11 drift) and carried MSVC-only leftovers (msvcp140/vcruntime140)
# from the era when the ADI libs were prebuilt with VS instead of MinGW.
export EXES="$STAGING_BIN/curl.exe \
$STAGING_BIN/iio_genxml.exe \
$STAGING_BIN/iio_info.exe \
$STAGING_BIN/iio_readdev.exe
"

bin_dir() {
	pushd "$SRCDIR"
	mkdir -p $SRCDIR/build/bin
	cp  $SRCDIR/build/osc.exe $SRCDIR/build/bin/
	cp  $SRCDIR/build/styles.css $SRCDIR/build/bin/
	cp  $SRCDIR/build/libosc.dll $SRCDIR/build/bin/

	cp -r $EXES $SRCDIR/build/bin/
	cp -r $SRCDIR/build/plugins $SRCDIR/build/bin/

	# Copy the runtime DLL closure from /mingw64 instead of a hardcoded soname
	# list (which drifts every time the MSYS2 packages update). Seed ldd with
	# everything we ship that has imports -- osc + the bundled EXEs, libosc,
	# every plugin, and the gdk-pixbuf / gtk loader modules staged by lib_dir()
	# (their deps, e.g. librsvg for SVG icons, are dlopened at runtime so they
	# never show up under osc.exe itself) -- plus the ADI libs explicitly in
	# case a plugin only dlopens them. Keep only deps under /mingw64/bin;
	# Windows system DLLs resolve elsewhere and are skipped. ldd walks deps
	# transitively, so one pass yields the full closure.
	local seeds
	seeds=$(find "$SRCDIR/build/bin" "$SRCDIR/build/lib" -type f \
		\( -name '*.exe' -o -name '*.dll' \))
	ldd $seeds $STAGING_BIN/libad9361.dll $STAGING_BIN/libad9166.dll 2>/dev/null \
		| awk '/=> \/mingw64\/bin\// {print $3}' \
		| sort -u \
		| xargs -r -I{} cp -u {} $SRCDIR/build/bin/

	# Guard against a silent empty closure (e.g. if ldd's output format ever
	# changes and the filter above stops matching): a healthy osc/GTK3 closure
	# is ~60 DLLs, so if far fewer landed, fail the build now instead of
	# shipping an installer that crashes on launch. Name-agnostic on purpose --
	# no soname to keep in sync.
	dll_count=$(find "$SRCDIR/build/bin" -maxdepth 1 -name '*.dll' | wc -l)
	if [ "$dll_count" -lt 20 ]; then
		echo "install_prep: only $dll_count DLLs resolved from /mingw64 -- ldd dependency resolution likely failed" >&2
		exit 1
	fi

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
