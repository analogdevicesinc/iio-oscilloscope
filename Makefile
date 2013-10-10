DESTDIR=/usr/local
PREFIX=/usr/local
PSHARE=$(PREFIX)/share/osc
PLIB=$(PREFIX)/lib/osc

# this is where the master fru files are (assuming they are installed at all)
FRU_FILES=$(PREFIX)/lib/fmc-tools/


LDFLAGS=`pkg-config --libs gtk+-2.0 gthread-2.0 gtkdatabox fftw3`
LDFLAGS+=`xml2-config --libs`
CFLAGS=`pkg-config --cflags gtk+-2.0 gthread-2.0 gtkdatabox fftw3`
CFLAGS+=`xml2-config --cflags`
CFLAGS+=-Wall -g -std=gnu90 -D_GNU_SOURCE -O2 -DPREFIX='"$(PREFIX)"' -lmatio -lz

#CFLAGS+=-DDEBUG
#CFLAGS += -DNOFFTW

PLUGINS=\
	plugins/fmcomms1.so \
	plugins/fmcomms2.so \
	plugins/debug.so \
	plugins/AD5628_1.so \
	plugins/AD7303.so

all: osc $(PLUGINS)

osc: osc.c int_fft.c iio_utils.c iio_widget.c fru.c dialogs.c trigger_dialog.c xml_utils.c ./ini/ini.c
	$(CC) $+ $(CFLAGS) $(LDFLAGS) -DFRU_FILES=\"$(FRU_FILES)\" -ldl -rdynamic -o $@

%.so: %.c
	$(CC) $+ $(CFLAGS) $(LDFLAGS) -shared -fPIC -o $@

install:
	install -d $(DESTDIR)/bin
	install -d $(DESTDIR)/share/osc/
	install -d $(DESTDIR)/lib/osc/
	install -d $(DESTDIR)/lib/osc/xmls
	install -d $(DESTDIR)/lib/osc/filters
	install -d $(DESTDIR)/lib/osc/waveforms
	install ./osc $(DESTDIR)/bin/
	install ./*.glade $(PSHARE)
	install ./icons/ADIlogo.png $(PSHARE)
	install ./icons/IIOlogo.png $(PSHARE)
	install ./icons/osc128.png $(PSHARE)
	install $(PLUGINS) $(PLIB)
	install ./xmls/* $(PLIB)/xmls
	install ./filters/* $(PLIB)/filters
	install ./waveforms/* $(PLIB)/waveforms

	xdg-icon-resource install --noupdate --size 16 ./icons/osc16.png adi-osc
	xdg-icon-resource install --noupdate --size 32 ./icons/osc32.png adi-osc
	xdg-icon-resource install --noupdate --size 64 ./icons/osc64.png adi-osc
	xdg-icon-resource install --noupdate --size 128 ./icons/osc128.png adi-osc
	xdg-icon-resource install --size 256 ./icons/osc256.png adi-osc
#	xdg-icon-resource install --size scalable ./osc.svg adi-osc
	xdg-desktop-menu install adi-osc.desktop

clean:
	rm -rf osc *.o plugins/*.so
