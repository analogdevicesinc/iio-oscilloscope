#!/bin/bash

if [[ -z "$1" ]]; then
  echo "Provide the MinGW version (i686 or x86_64)"
  exit
fi

CURDIR=$(realpath "$0")
export CURDIR=$(dirname "${CURDIR}")

[ -z "$BRANCH" ] && BRANCH="master"

export MINGW_VERSION=$1
cd ${CURDIR}
. ./build.sh.inc

