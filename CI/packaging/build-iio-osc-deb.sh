#!/bin/bash -e

# Script that generates IIO Oscilloscope .deb package and source files using debhelper

SCRIPT_DIR=$(dirname "$(readlink -f "$0")")
APPIMAGE_ARCH=$1

if [ -z "$APPIMAGE_ARCH" ]; then
    echo "Usage: $0 <appimage_arch>"
    echo "  appimage_arch:  Configuration file (aarch64 or armhf)"
    exit 1
fi

# Find the repo root
REPO_ROOT=$(git -C "${SCRIPT_DIR}" rev-parse --show-toplevel)

# Extract version from CMakeLists.txt
OSC_VERSION_MAJOR=$(grep 'set(OSC_VERSION_MAJOR' "${REPO_ROOT}/CMakeLists.txt" | grep -o '[0-9]\+')
OSC_VERSION_MINOR=$(grep 'set(OSC_VERSION_MINOR' "${REPO_ROOT}/CMakeLists.txt" | grep -o '[0-9]\+')
export VERSION="${OSC_VERSION_MAJOR}.${OSC_VERSION_MINOR}"
export RELEASE="V${VERSION}"


# Source the config file corresponding to the .deb package
set -a
source configs/iio-osc-${APPIMAGE_ARCH}.conf
set +a

export IIO_OSC_APPIMAGE=iio-oscilloscope.AppImage

WORK_DIR="${PACKAGE}-${VERSION}"
mv "iio-oscilloscope" ${WORK_DIR}

# Remove any existing debian directory from the source
rm -rf ${WORK_DIR}/debian

# Copy our debian directory and assets into the extracted source tree
cp -r CI/packaging/debian ${WORK_DIR}/

# Substitute variables in templates
cd ${WORK_DIR}

# Export all variables for envsubst and debian/rules
export VERSION ARCHITECTURE DEPENDS MAINTAINER HOMEPAGE DESCRIPTION LONG_DESCRIPTION PACKAGE RELEASE APPIMAGE_ARCH IIO_OSC_APPIMAGE
export DATE=$(date -R)

# Fetch release notes from GitHub (if it's a release tag)
echo "Fetching release notes from GitHub..."
RELEASE_NOTES=$(curl -s https://api.github.com/repos/analogdevicesinc/iio-oscilloscope/releases/tags/${RELEASE} \
  | jq -r '.body // "No release notes available"' \
  | sed 's/^/  * /')
# If release notes are empty or error occurred, use default
if [ -z "$RELEASE_NOTES" ]; then
    RELEASE_NOTES="  * New upstream release ${VERSION}"
fi

export RELEASE_NOTES

# Substitute templates
envsubst < debian/control.template > debian/control
envsubst < debian/changelog.template > debian/changelog

# Remove template files
rm debian/*.template

# Make sure debian/rules is executable
chmod +x debian/rules

# Build package using debhelper (builds both binary and source packages)
echo "Building packages..."
dpkg-buildpackage -us -uc -a ${ARCHITECTURE}

cd ..

echo ""
echo "Build complete! Generated files:"
echo "  - ${PACKAGE}_${VERSION}.orig.tar.gz (upstream source)"
echo "  - ${PACKAGE}_${VERSION}-1.debian.tar.xz (debian files)"
echo "  - ${PACKAGE}_${VERSION}-1.dsc (source descriptor)"
echo "  - ${PACKAGE}_${VERSION}-1_${ARCHITECTURE}.deb (binary package)"
echo "  - ${PACKAGE}_${VERSION}-1_${ARCHITECTURE}.buildinfo"
echo "  - ${PACKAGE}_${VERSION}-1_${ARCHITECTURE}.changes"
