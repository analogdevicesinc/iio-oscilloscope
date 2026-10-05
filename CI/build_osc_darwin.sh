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
		openssl@3.0 \
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
	mkdir -p build download
	# Some artifacts are tarred; unpack any .tar into download/ first.
	find build download -name "*.tar" -exec tar xf {} -C download \;
	find build download -name "*.pkg" -exec sudo installer -pkg {} -target / \;
}

bridge_framework() {
	local module="$1" fw="$2"
	local fwdir="/Library/Frameworks/${fw}.framework/Versions/Current"
	local libdir="${BREW_PREFIX}/lib"

	if [ ! -d "${fwdir}" ]; then
		echo "  ${fw}.framework not installed; skipping ${module}"
		return 0
	fi

	sudo mkdir -p "${libdir}/pkgconfig"
	sudo ln -sf "${fwdir}/${fw}" "${libdir}/lib${fw}.dylib"
	sudo tee "${libdir}/pkgconfig/${module}.pc" >/dev/null <<EOF
Name: ${module}
Description: ${module} (macOS framework bridge)
Version: 0
Cflags: -I${fwdir}/Headers
Libs: -L${libdir} -l${fw}
EOF

	echo "  bridged ${module} -> ${fwdir}/${fw}"
}

build_osc() {
	# Homebrew prefix is arch-specific: /opt/homebrew (Apple Silicon) vs
	# /usr/local (Intel). Resolve it once, before bridging, and thread it
	# through so the arm64 and x86_64 artifacts each build against the right
	# prefix. Fall back to the arch default if brew is not on PATH.
	export BREW_PREFIX="$(brew --prefix 2>/dev/null \
		|| { [ "$(uname -m)" = arm64 ] && echo /opt/homebrew || echo /usr/local; })"

	bridge_framework libiio    iio
	bridge_framework libad9361 ad9361
	bridge_framework libad9166 ad9166

	export PKG_CONFIG_PATH="${BREW_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH}"
	export CMAKE_PREFIX_PATH="${BREW_PREFIX}:${CMAKE_PREFIX_PATH}"
	echo "BREW_PREFIX=${BREW_PREFIX}"
	echo "PKG_CONFIG_PATH=${PKG_CONFIG_PATH}"

	pc_modules="glib-2.0 gtk+-3.0 gthread-2.0 gtkdatabox fftw3 libxml-2.0 \
		libcurl jansson matio libiio"
	pc_dirs() {  # $1 = --libs-only-L | --cflags-only-I; strip flag, dedupe, colon-join
		pkg-config "$1" $pc_modules 2>/dev/null \
			| tr ' ' '\n' | sed -n 's/^-.//p' | sort -u | tr '\n' ':'
	}
	export LIBRARY_PATH="$(pc_dirs --libs-only-L)${BREW_PREFIX}/lib:${LIBRARY_PATH}"
	export CPATH="$(pc_dirs --cflags-only-I)${BREW_PREFIX}/include:${CPATH}"
	echo "LIBRARY_PATH=${LIBRARY_PATH}"
	echo "CPATH=${CPATH}"

	mkdir -p build && cd build
	cmake -DCMAKE_C_COMPILER="/usr/bin/gcc" ..
	make -j9

	# Assert the artifact matches the runner arch, so a stray universal/cross
	# dep or wrong-arch .pkg fails loudly instead of shipping a broken binary.
	expected="$(uname -m)"           # arm64 | x86_64
	got="$(lipo -archs osc)"         # e.g. "arm64" or "x86_64"
	echo "built osc arch: ${got} (expected ${expected})"
	[ "${got}" = "${expected}" ] || { echo "arch mismatch"; exit 1; }
}

$@
