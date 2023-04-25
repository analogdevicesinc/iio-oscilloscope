#/bin/bash
set -xe

export SRCDIR=/home/docker/iio-oscilloscope

# set osc version

export OSC_BUILD_VER=`git describe --tags --always HEAD`
sed "s/UNSET_VERSION/${OSC_BUILD_VER}/" osc.iss > updated-osc.iss

# build osc

./build_mingw.sh build_osc

# create dir hierarchy

./install_prep.sh

# run innosetup 

/c/innosetup/iscc //p $SRCDIR/updated-osc.iss
