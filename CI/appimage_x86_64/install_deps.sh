#!/bin/bash
set -xe

SRC_DIR=$(git rev-parse --show-toplevel 2>/dev/null ) || \
SRC_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && cd ../../ && pwd )
SRC_SCRIPT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

STAGING_AREA=$SRC_SCRIPT/staging

LIBSERIALPORT_BRANCH="master"
LIBIIO_BRANCH="main"
LIBAD9361_BRANCH="staging/libiio1-support"
LIBAD9166_BRANCH="staging/libiio1-support"

BUILD_STATUS_FILE=$SRC_SCRIPT/build-status-appimage-x86_64

JOBS="-j14"

install_apt_pkgs() {
        APT_PKGS="libglib2.0-dev \
        	libgtk-3-dev \
        	libmatio-dev \
        	libfftw3-dev \
        	libxml2 \
        	libxml2-dev\
        	bison \
        	flex \
        	libavahi-common-dev \
        	libavahi-client-dev \
        	libcurl4-openssl-dev \
        	libjansson-dev \
        	cmake \
        	libaio-dev \
        	libcdk5-dev \
        	libusb-1.0-0-dev \
        	autotools-dev \
        	autoconf \
        	wget \
        	git \
        	libtool \
        	libfuse2 \
		dpkg-dev \
		libzstd-dev \
		libserialport-dev
        "
        sudo apt-get update
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y $APT_PKGS

	echo "apt packages installed" >> $BUILD_STATUS_FILE
	apt list --installed >> $BUILD_STATUS_FILE
	echo "" >> $BUILD_STATUS_FILE
}

install_gtkdatabox() {
	mkdir -p $STAGING_AREA
	pushd $STAGING_AREA
	if [ ! -d 'gtkdatabox-1.0.0' ]; then
		wget https://downloads.sourceforge.net/project/gtkdatabox/gtkdatabox-1/gtkdatabox-1.0.0.tar.gz
		tar xvf gtkdatabox-1.0.0.tar.gz
	fi
        cd gtkdatabox-1.0.0
        ./configure
	sudo make $JOBS install
	echo "gtkdatabox-1.0.0: https://downloads.sourceforge.net/project/gtkdatabox/gtkdatabox-1/gtkdatabox-1.0.0.tar.gz" >> $BUILD_STATUS_FILE
	popd
}

install_libiio() {
	mkdir -p $STAGING_AREA
	pushd $STAGING_AREA
	[ -d 'libiio' ]	|| git clone https://github.com/analogdevicesinc/libiio.git -b $LIBIIO_BRANCH libiio
	cd libiio
	mkdir -p build
	cd build
	cmake -DWITH_SERIAL_BACKEND=ON -DLIBIIO_COMPAT=ON ../
	make $JOBS
	sudo make install
	echo "$(basename -a "$(git config --get remote.origin.url)") - $(git rev-parse --abbrev-ref HEAD) - $(git rev-parse --short HEAD)" >> $BUILD_STATUS_FILE
	popd
}

install_libad9361() {
	mkdir -p $STAGING_AREA
	pushd $STAGING_AREA
	[ -d 'libad9361-iio' ] || git clone https://github.com/analogdevicesinc/libad9361-iio.git -b $LIBAD9361_BRANCH libad9361-iio
	cd libad9361-iio
	mkdir -p build
	cd build
	cmake ../
	make $JOBS
	sudo make install
	echo "$(basename -a "$(git config --get remote.origin.url)") - $(git rev-parse --abbrev-ref HEAD) - $(git rev-parse --short HEAD)" >> $BUILD_STATUS_FILE
	popd
	}

install_libad9166 () {
	mkdir -p $STAGING_AREA
	pushd $STAGING_AREA
	[ -d 'libad9166-iio' ] || git clone https://github.com/analogdevicesinc/libad9166-iio.git -b $LIBAD9166_BRANCH libad9166-iio
	cd libad9166-iio
	mkdir -p build
	cd build
	cmake ../
	make $JOBS
	sudo make install
	echo "$(basename -a "$(git config --get remote.origin.url)") - $(git rev-parse --abbrev-ref HEAD) - $(git rev-parse --short HEAD)" >> $BUILD_STATUS_FILE
	popd
}

move_build_status() {
	sudo mv $BUILD_STATUS_FILE $HOME
}

build_all() {
	install_apt_pkgs
	install_gtkdatabox
	install_libiio
	install_libad9361
	install_libad9166
	move_build_status
}

for arg in $@; do
	$arg
done
