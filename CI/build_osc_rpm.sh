#!/bin/bash
# Compile-smoke build of iio-oscilloscope on RPM distros (Fedora, openSUSE).
#
# Runs as root inside a vanilla distro container (no sudo). gtkdatabox and the
# ADI libraries (libiio, libad9361, libad9166) are not in Fedora/openSUSE repos,
# so they are built from source here -- mirroring the apt paths
# (build_osc_ubuntu.sh) and the x86 AppImage path (appimage_x86_64/install_deps.sh).
#
# Invoke one or more functions, e.g.:
#   ./CI/build_osc_rpm.sh install_fedora_pkgs
#   ./CI/build_osc_rpm.sh install_gtkdatabox install_adi_from_source build_osc

set -xe

STAGING_AREA="${STAGING_AREA:-/tmp/osc-staging}"
JOBS="-j$(nproc)"

# Source-built libs land in /usr/local; cmake uses lib64 on these distros,
# autotools (gtkdatabox) uses lib -- cover both for pkg-config and the linker.
export PKG_CONFIG_PATH="/usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig:${PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="/usr/local/lib64:/usr/local/lib:${LD_LIBRARY_PATH}"

install_fedora_pkgs() {
	# rpm-build provides rpmbuild, required by CPack's RPM generator (make package).
	dnf install -y \
		gcc gcc-c++ make cmake git flex bison rpm-build \
		autoconf automake libtool pkgconf-pkg-config patch wget tar \
		glib2-devel gtk3-devel fftw-devel libxml2-devel libcurl-devel \
		jansson-devel matio-devel libserialport-devel libusbx-devel \
		libaio-devel avahi-devel cdk-devel
}

install_opensuse_pkgs() {
	# rpm-build provides rpmbuild, required by CPack's RPM generator (make package).
	zypper --gpg-auto-import-keys ref
	zypper in -y --allow-downgrade \
		gcc gcc-c++ make cmake git flex bison rpm-build \
		autoconf automake libtool pkg-config patch wget tar \
		glib2-devel gtk3-devel fftw3-devel libxml2-devel libcurl-devel \
		libjansson-devel matio-devel libserialport-devel libusb-1_0-devel \
		libaio-devel libavahi-devel cdk-devel
}

install_gtkdatabox() {
	mkdir -p "$STAGING_AREA"
	cd "$STAGING_AREA"
	[ -f gtkdatabox-1.0.0.tar.gz ] || \
		wget https://downloads.sourceforge.net/project/gtkdatabox/gtkdatabox-1/gtkdatabox-1.0.0.tar.gz
	tar xf gtkdatabox-1.0.0.tar.gz
	cd gtkdatabox-1.0.0
	./configure
	make $JOBS
	make install
	ldconfig
}

# clone (shallow) + cmake + install one ADI library from GitHub.
_build_adi_lib() {
	local repo="$1" branch="$2"
	[ -d "$repo" ] || git clone --depth 1 -b "$branch" \
		"https://github.com/analogdevicesinc/$repo.git" "$repo"
	cmake -S "$repo" -B "$repo/build"
	make -C "$repo/build" $JOBS
	make -C "$repo/build" install
	ldconfig
}

install_adi_from_source() {
	mkdir -p "$STAGING_AREA"
	cd "$STAGING_AREA"
	# libiio is required by osc; libad9361/libad9166 are optional (they only
	# gate a few plugins) but built too for parity with the apt/AppImage paths.
	_build_adi_lib libiio libiio-v0
	_build_adi_lib libad9361-iio main
	_build_adi_lib libad9166-iio main
}

build_osc() {
	git config --global --add safe.directory "$(pwd)"
	mkdir -p build && cd build
	# CPACK_SYSTEM_NAME (set by the workflow to the matrix artifact-name, e.g.
	# Linux-Fedora-42) ends up in the package filename; skip the -D if unset.
	cpack_name_arg=""
	[ -n "${CPACK_SYSTEM_NAME:-}" ] && cpack_name_arg="-DCPACK_SYSTEM_NAME=${CPACK_SYSTEM_NAME}"
	cmake -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}" \
		-DENABLE_PACKAGING=ON $cpack_name_arg ../
	make $JOBS
	# Build the .rpm (+ .tar.gz) artifact. The generator is chosen per-distro
	# in cmake/LinuxPackaging.cmake.
	make package
}

for arg in "$@"; do
	"$arg"
done
