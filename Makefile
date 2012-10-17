DESTDIR=/usr/local
PREFIX=/usr/local

LDFLAGS=`pkg-config --libs gtk+-2.0 gtkdatabox`
CFLAGS=`pkg-config --cflags gtk+-2.0 gtkdatabox`
CFLAGS+=-Wall -std=gnu90 -D_GNU_SOURCE -O2 -DPREFIX='"$(PREFIX)"'

osc: osc.c int_fft.c iio_utils.c iio_widget.c
	$(CC) $+ $(CFLAGS) $(LDFLAGS) -o $@

install:
	install -d $(DESTDIR)/bin
	install -d $(DESTDIR)/share/osc/
	install ./osc $(DESTDIR)/bin/
	install ./osc.glade $(DESTDIR)/share/osc/
	install ./icons/ADIlogo.png $(DESTDIR)/share/osc/

	xdg-icon-resource install --noupdate --size 16 ./icons/osc16.png adi-osc
	xdg-icon-resource install --noupdate --size 32 ./icons/osc32.png adi-osc
	xdg-icon-resource install --noupdate --size 64 ./icons/osc64.png adi-osc
	xdg-icon-resource install --noupdate --size 128 ./icons/osc128.png adi-osc
	xdg-icon-resource install --size 256 ./icons/osc256.png adi-osc
#	xdg-icon-resource install --size scalable ./osc.svg adi-osc
	xdg-desktop-menu install adi-osc.desktop

clean:
	rm -rf osc *.o
