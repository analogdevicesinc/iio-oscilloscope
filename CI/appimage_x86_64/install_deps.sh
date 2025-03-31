#!/bin/bash

set -xe

WORKDIR="/home/$USER"
LIBIIO_BRANCH="libiio-v0"
LIBAD9361_BRANCH="main"
LIBAD9166_BRANCH="main"

install_apt_pkgs() {
	export APT_PKGS="libglib2.0-dev \
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
        	autoconf
        "
	sudo apt-get update
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y $APT_PKGS
}

install_gtkdatabox() {
	wget https://downloads.sourceforge.net/project/gtkdatabox/gtkdatabox-1/gtkdatabox-1.0.0.tar.gz
        tar xvf gtkdatabox-1.0.0.tar.gz
        cd gtkdatabox-1.0.0
        ./configure
	sudo make install
}

install_libiio() {
	git clone https://github.com/analogdevicesinc/libiio
	cd libiio
	git checkout $LIBIIO_BRANCH
	mkdir build
	cd build
	cmake -DWITH_SERIAL_BACKEND=ON ../
	make
	sudo make install
	popd
}

install_libad9361() {
	pushd $WORKDIR
	git clone https://github.com/analogdevicesinc/libad9361-iio
	cd libad9361-iio
	git checkout $LIBAD9361_BRANCH
	mkdir build
	cd build
	cmake ../
	make
	sudo make install
	popd
	}

install_libad9166 () {
	pushd $WORKDIR
	git clone https://github.com/analogdevicesinc/libad9166-iio
	cd libad9166-iio
	git checkout $LIBAD9166_BRANCH
	mkdir build
	cd build
	cmake ../
	make
	sudo make install
	popd
}

install_libserialport() {
	pushd $WORKDIR
	git clone https://github.com/sigrokproject/libserialport
	cd libserialport
	./autogen.sh
        ./configure
        make
        sudo make install
	
}
build_osc() {
	mkdir build && cd build
	cmake ../
	make -j9
}
