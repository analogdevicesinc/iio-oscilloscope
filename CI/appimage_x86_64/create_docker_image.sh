#!/bin/bash -ex

SRC_SCRIPT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

docker build -t cristianbindea/osc-ubuntu20:testing -f docker/Dockerfile .

# # build the image using old backend
# DOCKER_BUILDKIT=0 docker build -t cristianbindea/osc-ubuntu20:testing -f docker/Dockerfile .
