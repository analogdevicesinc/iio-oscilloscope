#!/bin/bash
# Compile + package build of iio-oscilloscope on RPM distros (Fedora, openSUSE).
#
# Runs as root inside a distro container (no sudo). Build dependencies are
# installed by the workflow ("Install base packages"); the ADI libraries
# (libiio, libad9361, libad9166) are downloaded from Cloudsmith as .rpm and
# installed by install_adi_rpms. gtkdatabox is not packaged for these distros,
# and matio is missing on openSUSE Leap, so those are built from source here.
#
# Invoke one or more functions, e.g.:
#   ./CI/build_osc_rpm.sh install_adi_rpms
#   ./CI/build_osc_rpm.sh install_gtkdatabox build_osc

set -xe

STAGING_AREA="${STAGING_AREA:-/tmp/osc-staging}"
JOBS="-j$(nproc)"

# Source-built libs land in /usr/local; cmake uses lib64 on these distros,
# autotools (gtkdatabox) uses lib -- cover both for pkg-config and the linker.
export PKG_CONFIG_PATH="/usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig:${PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="/usr/local/lib64:/usr/local/lib:${LD_LIBRARY_PATH}"

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

# matio (MAT-file I/O) -- osc links -lmatio. Fedora ships matio-devel, but
# openSUSE Leap does not (science OBS add-on only), so build it from source
# there. --enable-mat73=no drops the HDF5 dependency (osc doesn't need MAT 7.3).
install_matio() {
	mkdir -p "$STAGING_AREA"
	cd "$STAGING_AREA"
	[ -f matio-1.5.28.tar.gz ] || \
		wget https://github.com/tbeu/matio/releases/download/v1.5.28/matio-1.5.28.tar.gz
	tar xf matio-1.5.28.tar.gz
	cd matio-1.5.28
	./configure --enable-mat73=no
	make $JOBS
	make install
	ldconfig
}

# Install the ADI libraries (libiio, libad9361, libad9166) that the workflow
# downloaded from Cloudsmith into download/. Runtime deps resolve from the distro
# repos; gpg checks are off because the Cloudsmith artifacts are unsigned.
install_adi_rpms() {
	if command -v dnf >/dev/null; then
		dnf install -y --nogpgcheck ./download/*.rpm
	else
		zypper --no-gpg-checks install -y ./download/*.rpm
	fi
	ldconfig
}

build_osc() {
	git config --global --add safe.directory "$(pwd)"
	mkdir -p build && cd build
	# CPACK_SYSTEM_NAME (set by the workflow to the matrix artifact-name, e.g.
	# Linux-Fedora-42) ends up in the package filename; skip the -D if unset.
	cpack_name_arg=""
	[ -n "${CPACK_SYSTEM_NAME:-}" ] && cpack_name_arg="-DCPACK_SYSTEM_NAME=${CPACK_SYSTEM_NAME}"
	cmake -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}" \
		-DENABLE_PACKAGING=ON -DCPACK_GENERATOR="TGZ;RPM" $cpack_name_arg ../
	make $JOBS
	# Build the .rpm (+ .tar.gz) artifact (RPM distros only; this script never
	# runs on apt distros).
	make package
}

for arg in "$@"; do
	"$arg"
done
