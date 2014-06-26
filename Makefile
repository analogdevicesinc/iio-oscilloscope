DESTDIR=/usr/local
PREFIX=/usr/local
PSHARE=$(PREFIX)/share/osc-legacy
PLIB=$(PREFIX)/lib/osc-legacy

# this is where the master fru files are (assuming they are installed at all)
FRU_FILES=$(PREFIX)/lib/fmc-tools/


LDFLAGS=`pkg-config --libs gtk+-2.0 gthread-2.0 gtkdatabox fftw3`
LDFLAGS+=`xml2-config --libs`
LDFLAGS+=-lmatio -lz
CFLAGS=`pkg-config --cflags gtk+-2.0 gthread-2.0 gtkdatabox fftw3`
CFLAGS+=`xml2-config --cflags`
CFLAGS+=-Wall -g -std=gnu90 -D_GNU_SOURCE -O2 -DPREFIX='"$(PREFIX)"'

#CFLAGS+=-DDEBUG
#CFLAGS += -DNOFFTW

PLUGINS=\
	plugins/fmcomms1.so \
	plugins/fmcomms2.so \
	plugins/fmcomms2_adv.so \
	plugins/debug.so \
	plugins/daq2.so \
	plugins/AD5628_1.so \
	plugins/AD7303.so \
	plugins/motor_control.so \
	plugins/dmm.so \
	plugins/scpi.so

all: osc_legacy $(PLUGINS)

osc_legacy: osc.o int_fft.o iio_utils.o iio_widget.o fru.o dialogs.o trigger_dialog.o xml_utils.o ./ini/ini.c libini.o
	$(CC) $+ $(LDFLAGS) -ldl -rdynamic -o $@

osc.o: osc.c iio_widget.h iio_utils.h int_fft.h osc_plugin.h osc.h
	$(CC) osc.c -c $(CFLAGS)

int_fft.o: int_fft.c
	$(CC) int_fft.c -c $(CFLAGS)

iio_utils.o: iio_utils.c iio_utils.h
	$(CC) iio_utils.c -c $(CFLAGS) -DIIO_THREADS

iio_widget.o: iio_widget.c iio_widget.h iio_utils.h
	$(CC) iio_widget.c -c $(CFLAGS)

fru.o: fru.c fru.h
	$(CC) fru.c -c $(CFLAGS)

dialogs.o: dialogs.c fru.h osc.h iio_utils.h
	$(CC) dialogs.c -c $(CFLAGS) -DFRU_FILES=\"$(FRU_FILES)\"

trigger_dialog.o: trigger_dialog.c fru.h osc.h iio_utils.h iio_widget.h
	$(CC) trigger_dialog.c -c $(CFLAGS)

xml_utils.o: xml_utils.c xml_utils.h
	$(CC) xml_utils.c -c $(CFLAGS)


%.so: %.c
	$(CC) $+ $(CFLAGS) $(LDFLAGS) -shared -fPIC -o $@

install:
	install -d $(DESTDIR)/bin
	install -d $(DESTDIR)/share/osc-legacy/
	install -d $(DESTDIR)/lib/osc-legacy/
	install -d $(DESTDIR)/lib/osc-legacy/xmls
	install -d $(DESTDIR)/lib/osc-legacy/filters
	install -d $(DESTDIR)/lib/osc-legacy/waveforms
	install -d $(DESTDIR)/lib/osc-legacy/profiles
	install -d $(DESTDIR)/lib/osc-legacy/block_diagrams
	install ./osc_legacy $(DESTDIR)/bin/
	install ./*.glade $(PSHARE)
	install ./icons/ADIlogo.png $(PSHARE)
	install ./icons/IIOlogo.png $(PSHARE)
	install ./icons/osc128.png $(PSHARE)
	install $(PLUGINS) $(PLIB)
	install ./xmls/* $(PLIB)/xmls
	install ./filters/* $(PLIB)/filters
	install ./waveforms/* $(PLIB)/waveforms
	install ./profiles/* $(PLIB)/profiles
	install ./block_diagrams/* $(PLIB)/block_diagrams

	xdg-icon-resource install --noupdate --size 16 ./icons/osc16.png adi-osc-legacy
	xdg-icon-resource install --noupdate --size 32 ./icons/osc32.png adi-osc-legacy
	xdg-icon-resource install --noupdate --size 64 ./icons/osc64.png adi-osc-legacy
	xdg-icon-resource install --noupdate --size 128 ./icons/osc128.png adi-osc-legacy
	xdg-icon-resource install --size 256 ./icons/osc256.png adi-osc-legacy
	xdg-desktop-menu install adi-osc-legacy.desktop

clean:
	rm -rf osc_legacy *.o plugins/*.so

uninstall:
	rm -rf  $(PLIB) $(DESTDIR)/bin/osc_legacy
	xdg-desktop-menu uninstall adi-osc-legacy.desktop
