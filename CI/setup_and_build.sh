#!/bin/bash

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")

apt-get update -qq
apt-get install -y \
        build-essential cmake ninja-build pkg-config \
        autoconf automake libtool \
        git wget curl \
        bison flex \
        python3-pip \
        squashfs-tools \
        makeself \
        libgtk-3-dev \
        libgtkdatabox-dev \
        libglib2.0-dev \
        libxml2-dev \
        libcurl4-gnutls-dev \
        libfftw3-dev \
        libjansson-dev \
        zlib1g-dev \
        libssh2-1-dev \
        libaio-dev \
        libusb-1.0-0-dev \
        libserialport-dev \
        libavahi-client-dev \
        libavahi-common-dev \
        libpcre2-dev \
        libmatio-dev \
        libiio-dev \
        file \
        fuse \
        apt-transport-https \
        fuse3 \
        gnupg \
        gettext-base debhelper jq
curl -1sLf 'https://dl.cloudsmith.io/public/adi/kuiper/setup.deb.sh' | bash
apt-get update
apt-get install -y libad9361-dev libad9166-dev
REPO_ROOT=$(realpath "${SCRIPT_DIR}/..")
git config --global --add safe.directory "${REPO_ROOT}"
ARCH="$1"
"${REPO_ROOT}/CI/appimage_${ARCH}/create_appimage.sh"
"${REPO_ROOT}/packaging/build-iio-osc-deb.sh" "${ARCH}"
 
