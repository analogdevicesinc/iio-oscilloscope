#!/bin/bash

NUM_JOBS=4
WORKDIR="${PWD}/deps"
mkdir -p "$WORKDIR"
if [ "$TRAVIS" == "true" ] ; then
	STAGINGDIR=/usr/local
else
	STAGINGDIR="${WORKDIR}/staging"
fi
