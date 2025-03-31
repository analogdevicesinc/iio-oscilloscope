#!/bin/bash

set -xe

SRC_DIR=$(git rev-parse --show-toplevel 2>/dev/null ) || \
SRC_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && cd ../../ && pwd )
SRC_SCRIPT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

export NO_STRIP=1
BUILD_FOLDER=$SRC_DIR/build
JOBS="-j14"

git config --global --add safe.directory $SRC_DIR

mkdir -p $BUILD_FOLDER

pushd $BUILD_FOLDER
cmake ../
make $JOBS
popd