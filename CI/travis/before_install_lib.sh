#!/bin/sh -e

. ./CI/travis/lib.sh

# All these functions get used only if no PACKAGE_TO_INSTALL is defined
# or INSTALL_FROM_SW_DOWNLOADS != 1
# They are useful only for local builds, where this should build iio-osc
# locally and also building some deps

WORKDIR="${PWD}/deps"
mkdir -p "$WORKDIR"
STAGINGDIR="${STAGINGDIR:-${WORKDIR}/staging}"

# arbitrary number of jobs
NUM_JOBS=3

__make() {
	$configure --prefix="$STAGINGDIR" $SILENCED
	CFLAGS=-I${STAGINGDIR}/include LDFLAGS=-L${STAGINGDIR}/lib $make -j${NUM_JOBS} $SILENCED
	$SUDO $make install
}

__cmake() {
	local args="$1"
	mkdir -p build
	cd build # build

	cmake -DCMAKE_PREFIX_PATH="$STAGINGDIR" -DCMAKE_INSTALL_PREFIX="$STAGINGDIR" \
		-DCMAKE_EXE_LINKER_FLAGS="-L${STAGINGDIR}/lib" \
		$args .. $SILENCED
	CFLAGS=-I${STAGINGDIR}/include LDFLAGS=-L${STAGINGDIR}/lib make -j${NUM_JOBS} $SILENCED
	make install

	cd ..
}

__build_common() {
	local dir="$1"
	local buildfunc="$2"
	local getfunc="$3"
	local subdir="$4"
	local args="$5"

	cd "$WORKDIR" # deps dir

	# if we have this folder, we may not need to download it
	[ -d "$dir" ] || $getfunc

	cd "$dir" # this dep dir
	[ -z "$subdir" ] || cd "$subdir" # in case there is a build subdir or smth

	$buildfunc "$args"

	cd ..
	cd ..
	[ -z "$subdir" ] || cd ..
}

git_clone() {
	[ -d "$WORKDIR/$dir" ] || {
		[ -z "$branch" ] || branch="-b $branch"
		git clone $branch "$url" "$dir"
	}
}

wget_and_untar() {
	[ -d "$WORKDIR/$dir" ] || {
		local tar_file="${dir}.tar.gz"
		wget --no-check-certificate "$url" -O "$tar_file"
		tar -xvf "$tar_file" > /dev/null
	}
}

cmake_build_git() {
	local dir="$1"
	local url="$2"
	local branch="$3"
	local args="$4"

	__build_common "$dir" "__cmake" "git_clone" "" "$args"
}

make_build_wget() {
	local dir="$1"
	local url="$2"
	local configure="${3:-./configure}"
	local make="${4:-make}"

	__build_common "$dir" "__make" "wget_and_untar"
}
