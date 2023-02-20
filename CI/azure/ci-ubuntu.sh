#!/bin/bash

set -x
uname -a

echo "Current location is:" | pwd
ls -al

mkdir -p build
cd build
cmake ..

echo "Current location is:" | pwd
ls -al

make
