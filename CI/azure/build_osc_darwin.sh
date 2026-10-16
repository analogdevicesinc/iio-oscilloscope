#!/bin/bash

set -xe

install_deps() {
	export APT_PKGS="cmake \
		gcc \
		fftw \
		libmatio \
		libxml2 \
		pkg-config \
		libusb \
		gtk+3 \
		gtkdatabox \
		jansson \
		glib \
		curl \
		openssl@3 \
		gettext \
		libserialport \
		python
        "

	__brew_install_if_not_exists() {
		brew ls --versions "$1" || \
		brew install "$1"
	}

	brew_install_if_not_exists() {
		while [ -n "$1" ] ; do
			__brew_install_if_not_exists "$1" || return 1
			shift
		done
	}

	brew_install_if_not_exists $APT_PKGS
	brew cleanup
}

install_adi_pkgs() {
	for p in ./download/*.pkg; do
		[ -e "$p" ] || continue
		sudo installer -pkg "$p" -target /
	done
	# ADI libiio installs as an iio.framework (no libiio.pc) at the same path on
	# both arches; bridge it to a plain lib + .pc for pkg_check_modules(libiio).
	fw=/Library/Frameworks/iio.framework
	sudo mkdir -p /usr/local/lib/pkgconfig
	sudo ln -sf "$fw/iio" /usr/local/lib/libiio.dylib
	printf 'Name: libiio\nVersion: 0.26\nDescription: libiio\nCflags: -I%s/Headers\nLibs: -L/usr/local/lib -liio\n' "$fw" | sudo tee /usr/local/lib/pkgconfig/libiio.pc >/dev/null
}

build_osc() {
	# find the shim libiio.pc from install_adi_pkgs (arm64 brew pkg-config does
	# not search /usr/local by default).
	export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
	mkdir -p build && cd build
	# _DARWIN_C_SOURCE re-exposes dladdr/Dl_info (hidden by the project's strict
	# _POSIX_C_SOURCE on macOS); -include strings.h declares strncasecmp.
	cmake -DCMAKE_C_COMPILER="/usr/bin/gcc" \
		-DCMAKE_C_FLAGS="-D_DARWIN_C_SOURCE -include strings.h" ../
	make -j9
}

$@
