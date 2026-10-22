#!/bin/bash

set -xe

install_deps() {
	if command -v apt-get >/dev/null 2>&1; then
		sudo apt-get install -y libglib2.0-dev libgtk-3-dev libmatio-dev libfftw3-dev \
			libxml2-dev bison flex libavahi-common-dev libavahi-client-dev \
			libcurl4-openssl-dev libjansson-dev cmake libaio-dev libserialport-dev \
			libcdk5-dev libusb-1.0-0-dev autotools-dev autoconf
	elif command -v dnf >/dev/null 2>&1; then
		sudo dnf install -y glib2-devel gtk3-devel matio-devel fftw-devel libxml2-devel \
			bison flex avahi-devel libcurl-devel jansson-devel cmake libaio-devel \
			libserialport-devel cdk-devel libusb1-devel autoconf automake
	elif command -v zypper >/dev/null 2>&1; then
		sudo zypper in -y --allow-downgrade glib2-devel gtk3-devel matio-devel fftw3-devel \
			libxml2-devel bison flex libavahi-devel libcurl-devel libjansson-devel cmake \
			libaio-devel libserialport-devel cdk-devel libusb-1_0-devel autoconf automake
	fi
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
		sudo dpkg -i ./download/*.deb || sudo apt-get install -f -y
	fi
	if ls ./download/*.rpm >/dev/null 2>&1; then
		sudo rpm -i --force --nodeps ./download/*.rpm
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
