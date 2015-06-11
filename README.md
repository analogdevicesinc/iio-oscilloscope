IIO Oscilloscope
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

The main documenation for the IIO 'scope (including checkout/build instructions)
can be found at:
https://github.com/analogdevicesinc/iio-oscilloscope/wiki

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
