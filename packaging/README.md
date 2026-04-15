# IIO Oscilloscope Debian Package Builder

This directory contains the debhelper-based build system for creating Debian packages (.deb) from IIO Oscilloscope AppImages.

## Prerequisites

- debhelper (>= 13)
- wget
- jq (for fetching release notes)
- AppImage files in `../iio-osc/` directory

## Directory Structure

```
iio-osc-build/
├── build-iio-osc-deb.sh       # Main build script
├── configs/                    # Package configuration files
│   ├── iio-osc-aarch64.conf   # ARM64 architecture config
│   └── iio-osc-armhf.conf     # ARMHF architecture config
├── debian/                     # Debhelper control files
│   ├── control.template        # Package metadata template
│   ├── changelog.template      # Changelog template
│   ├── rules                   # Build rules (Makefile)
│   ├── postinst               # Post-installation script
│   ├── postrm                 # Post-removal script
│   ├── copyright              # Copyright information
│   ├── iio-oscilloscope.desktop.template  # Desktop entry
│   └── source/
│       ├── format             # Source package format
│       └── include-binaries   # Binary files to include
└── assets/
    └── iio-oscilloscope.png   # Application icon

```

## Building Packages

### Build for ARM64 (aarch64)

```bash
cd iio-osc-build
./build-iio-osc-deb.sh iio-osc-aarch64
```

### Build for ARMHF

```bash
cd iio-osc-build
./build-iio-osc-deb.sh iio-osc-armhf
```

### Specify a Different Release/Branch

```bash
# Use a specific tag
./build-iio-osc-deb.sh iio-osc-aarch64 v1.0.0

# Use default branch (libiio-v0)
./build-iio-osc-deb.sh iio-osc-aarch64 libiio-v0
```

## How It Works

1. **Configuration Loading**: Sources the config file from `configs/` which defines package metadata
2. **Source Download**: Downloads source code from GitHub (libiio-v0 branch by default)
3. **AppImage Integration**: Copies the local AppImage from `../iio-osc/` directory
4. **Template Substitution**: Replaces variables in control, changelog, and desktop file templates
5. **Package Building**: Uses debhelper to build both binary (.deb) and source packages

## Generated Files

After a successful build, you'll have:

- `iio-oscilloscope_VERSION.orig.tar.gz` - Upstream source archive
- `iio-oscilloscope_VERSION-1.debian.tar.xz` - Debian packaging files
- `iio-oscilloscope_VERSION-1.dsc` - Source package descriptor
- `iio-oscilloscope_VERSION-1_ARCH.deb` - Binary package (installable)
- `iio-oscilloscope_VERSION-1_ARCH.buildinfo` - Build information
- `iio-oscilloscope_VERSION-1_ARCH.changes` - Changes file

## Package Installation

Once installed, the package:

- Installs the AppImage to `/usr/bin/iio-oscilloscope.AppImage`
- Creates bash aliases: `iio-oscilloscope` and `osc`
- Adds a desktop entry for GUI launcher integration
- Installs an icon to `/usr/share/iio-oscilloscope/icons/`

## Customization

### Changing Dependencies

Edit the `DEPENDS` variable in `configs/iio-osc-*.conf`

### Changing Maintainer

Edit the `MAINTAINER` variable in the config files

### Using a Different Icon

Replace `assets/iio-oscilloscope.png` with your preferred icon

## Notes

- The AppImage files must be present in `../iio-osc/` directory before building
- The script expects files named: `iio-oscilloscope-aarch64.AppImage` and `iio-oscilloscope-armhf.AppImage`
- Source code is fetched from the `libiio-v0` branch by default
