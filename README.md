[![windows-mingw build](https://github.com/analogdevicesinc/iio-oscilloscope/actions/workflows/buildmingw.yml/badge.svg?branch=master)](https://github.com/analogdevicesinc/iio-oscilloscope/actions/workflows/buildmingw.yml?query=branch%3Amaster+)
[![deb build](https://github.com/analogdevicesinc/iio-oscilloscope/actions/workflows/build-deb-ubuntu-22.04.yml/badge.svg?branch=master)](https://github.com/analogdevicesinc/iio-oscilloscope/actions/workflows/build-deb-ubuntu-22.04.yml?query=branch%3Amaster+)

IIO Oscilloscope [![GitHub Release](https://img.shields.io/github/release/analogdevicesinc/iio-oscilloscope.svg)](https://github.com/analogdevicesinc/iio-oscilloscope/releases/latest)  [![Application License](https://img.shields.io/badge/license-GPL2-blue.svg)](https://github.com/analogdevicesinc/libiio/blob/master/COPYING_GPL.txt)
================

The IIO Oscilloscope is a GTK+ application, which demonstrates how to
interface various IIO devices to different visualization methods within a
Linux system.

The application supports plotting of the captured data in three different modes:
  - time domain
  - frequency domain and
  - constellation (X vs Y)

The IIO 'scope supports a plugin architecture which many people use to view
and modify settings of the attached IIO device(s).

The main documentation for the IIO 'scope (including checkout/build instructions)
can be found at:
https://wiki.analog.com/resources/tools-software/linux-software/iio_oscilloscope#installation

Source can be found at:
https://github.com/analogdevicesinc/iio-oscilloscope

Bugs can be reported at:
https://github.com/analogdevicesinc/iio-oscilloscope/issues

The IIO 'scope uses:
  - FFTW, a C subroutine library for computing the discrete Fourier transform
    (DFT) in one or more dimensions, of arbitrary input size, and of both real
    and complex data
    http://www.fftw.org/
  - inih, a simple .INI file parser written in C
    http://code.google.com/p/inih/

The IIO 'scope is copyright its authors, and is released under the GPL 2.0
