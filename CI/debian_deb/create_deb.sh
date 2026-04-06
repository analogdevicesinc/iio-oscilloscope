#!/bin/bash

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
        apt-transport-https\
        fuse3 \
        libxml2-dev \
        gnupg
curl -1sLf 'https://dl.cloudsmith.io/public/adi/kuiper/setup.deb.sh' | bash
apt-get update
apt-get install -y libad9361-dev libad9166-dev
git config --global --add safe.directory /workspace/iio-oscilloscope
ARCH="$1"
./CI/appimage_${ARCH}/create_appimage.sh
./CI/packaging/build-iio-osc-deb.sh ${ARCH}
 
