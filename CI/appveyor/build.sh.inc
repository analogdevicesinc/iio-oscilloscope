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

export PKG_CONFIG_SYSROOT_DIR="${SYSROOT}"
export PKG_CONFIG_LIBDIR="${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig"

# Debian's mingw32 toolchain is not sysroot-aware...
export CPPFLAGS="-I${SYSROOT}/${INSTALL_PREFIX}/include"
export LDFLAGS="-L${SYSROOT}/${INSTALL_PREFIX}/lib"
#export CFLAGS="-g -ggdb -std=gnu89"
export DESTDIR=${SYSROOT}/
# Required for globbing
shopt -s extglob

if [ -z "$MAKE_J" ] ; then
	MAKE_J=-j$(getconf _NPROCESSORS_ONLN)
	MAKE_J=${MAKE_J:--j1}
fi

# Build the osc
build_osc () {
	cd ${CURDIR}
	make clean
	PREFIX=.. CROSS_COMPILE=${HOST}- make SYSROOT=${SYSROOT} GIT_BRANCH=${BRANCH} ${MAKE_J}
	PREFIX=/usr CROSS_COMPILE=${HOST}- DEBIAN_INSTALL=1 make SYSROOT=${SYSROOT} GIT_BRANCH=${BRANCH} install
}

# Remove garbage
remove_garbage () {
	rm -rf ${SYSROOT}/${INSTALL_PREFIX}/bin/!(*.dll|osc.exe|iio_info.exe|iio_readdev.exe|iio_genxml.exe|curl.exe)
	rm -rf ${SYSROOT}/${INSTALL_PREFIX}/include
	rm -rf ${SYSROOT}/${INSTALL_PREFIX}/share/!(locale|themes|osc)
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

