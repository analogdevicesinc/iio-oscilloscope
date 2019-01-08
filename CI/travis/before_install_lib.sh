#!/bin/sh -xe

. ./CI/travis/lib.sh

__cmake() {
	local args="$1"
	mkdir -p build
	cd build # build

	if [ "$TRAVIS" = "true" ] || [ "$INSIDE_DOCKER" = "1" ] ; then
		cmake $args ..
		make -j${NUM_JOBS}
		sudo make install
	else
		cmake -DCMAKE_PREFIX_PATH="$STAGINGDIR" -DCMAKE_INSTALL_PREFIX="$STAGINGDIR" \
			-DCMAKE_EXE_LINKER_FLAGS="-L${STAGINGDIR}/lib" \
			$args .. $SILENCED
		CFLAGS=-I${STAGINGDIR}/include LDFLAGS=-L${STAGINGDIR}/lib make -j${NUM_JOBS} $SILENCED
		make install
	fi

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

cmake_build_git() {
	local dir="$1"
	local url="$2"
	local branch="$3"
	local args="$4"

	__build_common "$dir" "__cmake" "git_clone" "" "$args"
}
