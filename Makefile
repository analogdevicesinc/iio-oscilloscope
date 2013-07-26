LDFLAGS=`pkg-config --libs gtk+-2.0 gtkdatabox`
CFLAGS=`pkg-config --cflags gtk+-2.0 gtkdatabox`
CFLAGS+=-Wall -g -std=gnu90 -D_GNU_SOURCE -O2

osc: osc.c oscplot.c iio_widget.c iio_utils.c datatypes.c
	$(CC) $+ $(CFLAGS) $(LDFLAGS) -o $@

clean:
	rm -rf osc *.o
