DESTDIR=/usr/local
PREFIX=/usr/local

LDFLAGS=`pkg-config --libs gtk+-2.0 gtkdatabox fftw3`
LDFLAGS+=`xml2-config --libs`
CFLAGS=`pkg-config --cflags gtk+-2.0 gtkdatabox fftw3`
CFLAGS+=`xml2-config --cflags`
CFLAGS+=-pthread
CFLAGS+=-Wall -g -std=gnu90 -D_GNU_SOURCE -O2 -DPREFIX='"$(PREFIX)"'

osc: osc.c oscplot.c iio_widget.c iio_utils.c datatypes.c
	$(CC) $+ $(CFLAGS) $(LDFLAGS) -o $@

clean:
	rm -rf osc *.o
