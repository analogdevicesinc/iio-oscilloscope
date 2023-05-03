#/bin/bash
set -xe

export SRCDIR=/home/docker/iio-oscilloscope
cd $SRCDIR

# set osc version

sed "s/UNSET_VERSION/${OSC_BUILD_VER}/" osc.iss > updated-osc.iss

# build libiio

./CI/docker/build_mingw.sh build_libiio

# build libad9361

./CI/docker/build_mingw.sh build_libad9361

# build libad9166 

./CI/docker/build_mingw.sh build_libad9166

# build osc

./CI/docker/build_mingw.sh build_osc

# create dir hierarchy

./CI/docker/install_prep.sh

# run innosetup 

/c/innosetup/iscc //p $SRCDIR/updated-osc.iss

mv ./artifact/adi-osc-setup.exe /home/docker/artifact
