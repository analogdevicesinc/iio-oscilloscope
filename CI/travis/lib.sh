#!/bin/bash

export NUM_JOBS=4
export WORKDIR="${PWD}/deps"
mkdir -p "$WORKDIR"
if [ "$TRAVIS" == "true" ] ; then
	export STAGINGDIR=/usr/local
else
	export STAGINGDIR="${WORKDIR}/staging"
fi
