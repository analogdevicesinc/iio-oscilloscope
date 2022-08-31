#!/bin/bash

# ------------------------------------------------------------------------------

set -xe
CURDIR_ONE_DIR_UP=$(dirname ${CURDIR})
CURDIR_TWO_DIR_UP=$(dirname ${CURDIR_ONE_DIR_UP})
export CURDIR=${CURDIR_TWO_DIR_UP}
export SYSROOT=${CURDIR}/sysroot-${MINGW_VERSION}/
export HOST=${MINGW_VERSION}-w64-mingw32
INSTALL_PREFIX=/usr

CONFIGURE_OPTS="--prefix=${INSTALL_PREFIX} --host=${HOST} --with-sysroot=${SYSROOT} --disable-dependency-tracking --enable-silent-rules --disable-static --disable-option-checking"
export CMAKE=/home/test/Downloads/cmake-3.24.1-linux-x86_64/bin/cmake

export PKG_CONFIG_SYSROOT_DIR="${SYSROOT}"
export PKG_CONFIG_LIBDIR="${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig"
export PKG_CONFIG_PATH="${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig:${SYSROOT}/usr/local/lib/pkgconfig"

# Debian's mingw32 toolchain is not sysroot-aware...
export CPPFLAGS="-I${SYSROOT}/${INSTALL_PREFIX}/include" 
export LDFLAGS="-L${SYSROOT}/${INSTALL_PREFIX}/lib"
# fix printf formatting errors

export CFLAGS="-D__USE_MINGW_ANSI_STDIO=1"
#export CFLAGS="-g -ggdb -std=gnu89"
export DESTDIR=${SYSROOT}/
# Required for globbing
shopt -s extglob

# Build the osc
build_osc () {
	cd ${CURDIR}
	rm -rf build
	mkdir build && cd build
	$CMAKE -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=${HOST}-gcc -DCMAKE_RC_COMPILER=${HOST}-windres \
                -DCMAKE_SYSROOT=${SYSROOT} -DCMAKE_INSTALL_PREFIX:STRING=.. -DCPACK_PACKAGE_FILE_NAME=osc-${MINGW_VERSION} -DCMAKE_PREFIX_PATH="$SYSROOT/usr;$SYSROOT/usr/local" ../
	make -j$(nproc)
	make package
	tar -C ${SYSROOT}/usr -xf osc-${MINGW_VERSION}.tar.Z --strip-components=1
	cd -
}

# Remove garbage
remove_garbage () {
	rm -rf ${SYSROOT}/${INSTALL_PREFIX}/bin/!(*.dll|osc.exe|iio_info.exe|iio_readdev.exe|iio_genxml.exe|curl.exe)
	rm -rf ${SYSROOT}/${INSTALL_PREFIX}/include
	rm -rf ${SYSROOT}/${INSTALL_PREFIX}/share/!(locale|themes|osc|icons)
	rm -rf ${SYSROOT}/${INSTALL_PREFIX}/lib/!(gtk-2.0|osc|gdk-pixbuf-2.0)
	rm -rf ${SYSROOT}/${INSTALL_PREFIX}/lib/gtk-2.0/2.10.0/engines/!(*.dll)
	rm -rf ${SYSROOT}/${INSTALL_PREFIX}/lib/gtk-2.0/modules/!(*.dll)
	rm -rf ${SYSROOT}/${INSTALL_PREFIX}/lib/gtk-2.0/include
	rm -rf ${SYSROOT}/${INSTALL_PREFIX}/lib/gdk-pixbuf-2.0/2.10.0/loaders/!(*.dll)
}

# Strip!
strip () {
	${HOST}-strip ${SYSROOT}/${INSTALL_PREFIX}/bin/*
	${HOST}-strip ${SYSROOT}/${INSTALL_PREFIX}/lib/osc/*.dll
	${HOST}-strip ${SYSROOT}/${INSTALL_PREFIX}/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.dll
}

main () {
	build_osc
	remove_garbage
	strip

	echo "Build complete."
}

main

