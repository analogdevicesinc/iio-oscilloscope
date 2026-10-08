#!/bin/bash

set -xe

# Build dependencies are installed by the workflow's "Install base packages" step.

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
	# Containerized CI mounts the workspace from the host, so git sees it as
	# owned by another user and refuses to run (dubious ownership). Mark it safe
	# so CMake's git calls (GIT_COMMIT_TIMESTAMP, GIT_VERSION, ...) succeed
	# instead of emitting an empty -DGIT_COMMIT_TIMESTAMP= that breaks dialogs.c.
	git config --global --add safe.directory "$(pwd)"
	mkdir build && cd build
	cmake -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-RelWithDebInfo}" ../
	make -j9
}

$@
