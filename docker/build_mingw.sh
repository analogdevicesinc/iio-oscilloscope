#/bin/bash
set -xe
LIBIIO_BRANCH=master
LIBAD9361_BRANCH=master
LIBAD9166_BRANCH=master
OSC_BRANCH=$2

export WORKDIR=/home/$USER/

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
    git clone https://github.com/analogdevicesinc/libiio --branch $LIBIIO_BRANCH
    cd libiio
    rm -rf build
    mkdir build 
    cd build
    $CMAKE $CMAKE_OPTS -G"Unix Makefiles" -DWITH_SERIAL_BACKEND=ON ../
    $MAKE install
    popd
}

build_libad9361() {
    pushd $WORKDIR
    git clone https://github.com/analogdevicesinc/libad9361-iio --branch $LIBAD9361_BRANCH
    cd libad9361-iio
    mkdir build 
    cd build
    $CMAKE $CMAKE_OPTS -G"Unix Makefiles" ../
    $MAKE install
    popd
}

build_libad9166 () {
    pushd $WORKDIR
    git clone https://github.com/analogdevicesinc/libad9166-iio --branch $LIBAD9166_BRANCH
    cd libad9166-iio
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
    pushd /home/docker/
    git clone https://github.com/analogdevicesinc/iio-oscilloscope --branch $OSC_BRANCH
    cd iio-oscilloscope
    mkdir build
    cd build
    $CMAKE $CMAKE_OPTS -G"Unix Makefiles" ../
    $MAKE 
    popd
}

init_env
$@

