#!/bin/bash

set -xe

install_deps() {
	sudo apt-get install -y libglib2.0-dev libgtk-3-dev libmatio-dev libfftw3-dev \
		libxml2-dev bison flex libavahi-common-dev libavahi-client-dev \
		libcurl4-openssl-dev libjansson-dev cmake libaio-dev libserialport-dev \
		libcdk5-dev libusb-1.0-0-dev autotools-dev autoconf
}

install_gtkdatabox() {
	wget https://downloads.sourceforge.net/project/gtkdatabox/gtkdatabox-1/gtkdatabox-1.0.0.tar.gz
	tar xvf gtkdatabox-1.0.0.tar.gz
	cd gtkdatabox-1.0.0
	./configure
	sudo make install
}

install_adi_pkgs() {
	if ls ./download/*.deb >/dev/null 2>&1; then
		sudo apt-get install -y ./download/*.deb
	fi
}

build_osc() {
	git config --global --add safe.directory '*'
	export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
	mkdir build && cd build
	cmake ../
	make -j9
}

$@
