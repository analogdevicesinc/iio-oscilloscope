#/bin/bash
set -xe

export SRCDIR=/home/docker/iio-oscilloscope

# build osc

./build_mingw.sh build_osc

# create dir hierarchy

./install_prep.sh

# run innosetup 

/c/innosetup/iscc //p $SRCDIR/osc.iss
