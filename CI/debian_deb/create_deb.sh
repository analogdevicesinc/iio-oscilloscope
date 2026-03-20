#!/bin/bash

log "Installing dependencies..."
apt-get update -qq
apt-get install -y apt-transport-https libiio-dev \
  build-essential cmake ninja-build pkg-config git wget curl \
  bison flex python3-pip squashfs-tools fuse3 file \
  libgtk-3-dev libglib2.0-dev libxml2-dev libcurl4-gnutls-dev \
  libfftw3-dev libjansson-dev zlib1g-dev libssh2-1-dev libaio-dev \
  libusb-1.0-0-dev libserialport-dev libavahi-client-dev \
  libavahi-common-dev libpcre2-dev libmatio-dev
curl -1sLf 'https://dl.cloudsmith.io/public/adi/kuiper/setup.deb.sh' | bash
apt-get update
apt-get install -y libad9361-dev libad9166-dev
wget https://downloads.sourceforge.net/project/gtkdatabox/gtkdatabox-1/gtkdatabox-1.0.0.tar.gz
tar xvf gtkdatabox-1.0.0.tar.gz
cd gtkdatabox-1.0.0 && ./configure && make install
cd ..
echo "export LD_LIBRARY_PATH=\"/usr/local/lib\"" >> /etc/bash.bashrc
pwd
git config --global --add safe.directory /workspace/iio-oscilloscope
./CI/appimage_arm64/create_appimage.sh
