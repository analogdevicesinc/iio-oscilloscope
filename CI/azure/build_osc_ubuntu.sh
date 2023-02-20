#!/bin/bash

set -xe

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
        	libserialport-dev \
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

install_adi_debs() {
	sudo dpkg -i ./download/*.deb
}

build_osc() {
	mkdir build && cd build
	cmake ../
	make -j9
}

$@
