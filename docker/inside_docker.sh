#/bin/bash
set -xe
OSC_BRANCH=$1

export SRCDIR=/home/docker/iio-oscilloscope

# build osc

./build_mingw.sh build_osc $OSC_BRANCH

# create dir hierarchy

./install_prep.sh

# run innosetup 

/c/innosetup/iscc //p $SRCDIR/osc.iss
