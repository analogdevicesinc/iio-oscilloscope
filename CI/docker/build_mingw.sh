#/bin/bash
set -xe

export WORKDIR="${WORKDIR:-/home/$USER/}"

init_env() {

export ARCH=x86_64
export MINGW_VERSION=mingw64
export CC=/${MINGW_VERSION}/bin/${ARCH}-w64-mingw32-gcc.exe
export CXX=/${MINGW_VERSION}/bin/${ARCH}-w64-mingw32-g++.exe

export JOBS=-j9
export STAGING_DIR="/mingw64"
export MAKE_BIN="/bin/make"
export MAKE="$MAKE_BIN $JOBS"
export CMAKE_OPTS="-DCMAKE_C_COMPILER:FILEPATH=${CC} \
-DCMAKE_CXX_COMPILER:FILEPATH=${CXX} \
-DCMAKE_MAKE_PROGRAM:FILEPATH=${MAKE_BIN} \
-DPKG_CONFIG_EXECUTABLE=/$MINGW_VERSION/bin/pkg-config.exe \
-DCMAKE_MODULE_PATH=$STAGING_DIR \
-DCMAKE_PREFIX_PATH=$STAGING_DIR/lib/cmake \
-DCMAKE_BUILD_TYPE=RelWithDebInfo \
-DCMAKE_STAGING_PREFIX=$STAGING_DIR \
-DCMAKE_INSTALL_PREFIX=$STAGING_DIR"

export CMAKE="/$MINGW_VERSION/bin/cmake"
export HOST=${MINGW_VERSION}-w64-mingw32
export AUTOCONF_OPTS="--prefix=/mingw64 \
        --host=${ARCH}-w64-mingw32 \
        --enable-shared \
        --disable-static"
}

install_pacman_deps() {
WINDEPS="mingw-w64-x86_64-gdb \
mingw-w64-x86_64-dlfcn \
mingw-w64-x86_64-libserialport \
mingw-w64-x86_64-libusb \
mingw-w64-x86_64-matio \
mingw-w64-x86_64-fftw \
mingw-w64-x86_64-autotools \
mingw-w64-x86_64-gtk3 \
mingw-w64-x86_64-glib2 \
mingw-w64-x86_64-cmake \
mingw-w64-x86_64-gcc \
mingw-w64-x86_64-python3 \
mingw-w64-x86_64-autotools \
mingw-w64-x86_64-make \
mingw-w64-x86_64-doxygen \
mingw-w64-x86_64-jansson \
cmake 
"
pacman -S --noconfirm --needed $WINDEPS
}

get_innosetup() {
	pushd /home/
	wget https://jrsoftware.org/download.php/is.exe 
	popd
}

build_libiio() {
	pushd $WORKDIR
	export DIR="$WORKDIR/libiio"
	if [ -d "$DIR" ]; then
  		rm -rf $DIR
	fi
	git clone https://github.com/analogdevicesinc/libiio
	cd libiio
	git checkout $LIBIIO_BRANCH
	mkdir build
	cd build
	$CMAKE $CMAKE_OPTS -G"Unix Makefiles" -DWITH_SERIAL_BACKEND=ON ../
	$MAKE install
	popd
}

build_libad9361() {
	pushd $WORKDIR
	export DIR="$WORKDIR/libad9361-iio"
	if [ -d "$DIR" ]; then
  		rm -rf $DIR
	fi
	git clone https://github.com/analogdevicesinc/libad9361-iio
	cd libad9361-iio
	git checkout $LIBAD9361_BRANCH
	mkdir build
	cd build
	$CMAKE $CMAKE_OPTS -G"Unix Makefiles" ../
	$MAKE install
	popd
	}

build_libad9166 () {
	pushd $WORKDIR
	export DIR="$WORKDIR/libad9166-iio"
	if [ -d "$DIR" ]; then
  		rm -rf $DIR
	fi
	git clone https://github.com/analogdevicesinc/libad9166-iio
	cd libad9166-iio
	git checkout $LIBAD9166_BRANCH
	mkdir build
	cd build
	$CMAKE $CMAKE_OPTS -G"Unix Makefiles" ../
	$MAKE install
	popd
}

build_gtkdatabox () {
	pushd $WORKDIR
	wget https://downloads.sourceforge.net/project/gtkdatabox/gtkdatabox-1/gtkdatabox-1.0.0.tar.gz
	tar xvf gtkdatabox-1.0.0.tar.gz
	cd gtkdatabox-1.0.0
	./configure $AUTOCONF_OPTS
	$MAKE install
	popd
}

build_deps() {
	build_libiio
	build_libad9361
	build_libad9166
	build_gtkdatabox
}

build_osc() {
	pushd "${SRCDIR:-/home/docker/iio-oscilloscope}"
	mkdir -p build
	cd build
	$CMAKE $CMAKE_OPTS -G"Unix Makefiles" ../

	# --- CI diagnostics (temporary) -------------------------------------
	# osc fails with every .c file reporting "Error 1" but NO gcc diagnostic
	# in the log. A source-level -Werror would always print "file:line:
	# error:" before exiting, so the silent, instant, non-zero failure points
	# at the compiler subprocess (cc1) failing to *launch* -- not the osc
	# source. This block pins that down in a single run:
	#   1. list cc1's DLL dependencies and flag any unresolved ones,
	#   2. compile a trivial file to see whether the toolchain works here,
	#   3. make the fallback genuinely serial. The old "-j1 VERBOSE=1"
	#      silently re-forced -j9 because MAKE is exported as "$MAKE_BIN -j9"
	#      and CMake's recursive $(MAKE) inherits it ("-j9 forced in
	#      submake"), so parallel output masked the real per-file error.
	echo "=== toolchain probe ==="
	"$CC" --version
	CC1=$("$CC" -print-prog-name=cc1 || true); echo "cc1: $CC1"
	ldd "$CC1" 2>&1 | grep -i "not found" && echo "cc1: UNRESOLVED DLLS ^^" || echo "cc1 deps: all resolved"
	printf 'int main(void){return 0;}\n' > _probe.c
	"$CC" -v -c _probe.c -o _probe.o && echo "probe: OK" || echo "probe: FAILED (exit $?)"
	rm -f _probe.c _probe.o
	echo "=== osc build ==="
	# --------------------------------------------------------------------

	$MAKE || env -u MAKE $MAKE_BIN -j1 VERBOSE=1
	popd
}

init_env
$@