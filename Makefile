TMP = temp_resources
DESTDIR=/usr/local
PREFIX=/usr/local
PSHARE=$(PREFIX)/share/osc
PLIB=$(PREFIX)/lib/osc

# this is where the master fru files are (assuming they are installed at all)
FRU_FILES=$(PREFIX)/lib/fmc-tools/

CC := $(CROSS_COMPILE)gcc

SYSROOT := $(shell $(CC) -print-sysroot)
MULTIARCH := $(shell $(CC) -print-multiarch)

WITH_MINGW := $(if $(shell echo | $(CC) -dM -E - |grep __MINGW32__),y)
EXPORT_SYMBOLS := -Wl,--export-all-symbols
EXPORT_SYMBOLS := $(if $(WITH_MINGW),$(EXPORT_SYMBOLS))

PKG_CONFIG_PATH := $(SYSROOT)/usr/share/pkgconfig:$(SYSROOT)/usr/lib/pkgconfig:$(SYSROOT)/usr/lib/$(MULTIARCH)/pkgconfig
PKG_CONFIG := env PKG_CONFIG_SYSROOT_DIR="$(SYSROOT)" \
	PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config

DEPENDENCIES := glib-2.0 gtk+-2.0 gthread-2.0 gtkdatabox fftw3 libiio libxml-2.0

LDFLAGS := $(shell $(PKG_CONFIG) --libs $(DEPENDENCIES)) \
	-L$(SYSROOT)/usr/lib -lmatio -lz -lm

CFLAGS := $(shell $(PKG_CONFIG) --cflags $(DEPENDENCIES)) \
	-I$(SYSROOT)/usr/include $(if $(WITH_MINGW),,-fPIC) \
	-Wall -g -std=gnu90 -D_GNU_SOURCE -O2 -DPREFIX='"$(PREFIX)"'

#CFLAGS+=-DDEBUG
#CFLAGS += -DNOFFTW

# TODO: Make the debug and scpi plugins available on MinGW
PLUGINS=\
	plugins/fmcomms1.so \
	plugins/fmcomms2.so \
	plugins/fmcomms5.so \
	plugins/fmcomms6.so \
	plugins/fmcomms2_adv.so \
	plugins/pr_config.so \
	plugins/daq2.so \
	plugins/AD5628_1.so \
	plugins/AD7303.so \
	plugins/cn0357.so \
	plugins/motor_control.so \
	plugins/dmm.so \
	$(if $(WITH_MINGW),,plugins/debug.so) \
	$(if $(WITH_MINGW),,plugins/scpi.so)

all: osc $(PLUGINS)

libosc.so: osc.o oscplot.o datatypes.o int_fft.o iio_widget.o fru.o dialogs.o trigger_dialog.o xml_utils.o libini/libini.o libini2.o dac_data_manager.o
	$(CC) $+ $(CFLAGS) $(LDFLAGS) -ldl -shared -o $@ $(EXPORT_SYMBOLS)

osc: libosc.so oscmain.o
	$(CC) $+ $(LDFLAGS) -o $@

osc.o: osc.c iio_widget.h int_fft.h osc_plugin.h osc.h libini2.h
	$(CC) osc.c -c $(CFLAGS)

oscplot.o: oscplot.c oscplot.h osc.h datatypes.h iio_widget.h libini2.h
	$(CC) oscplot.c -c $(CFLAGS)

datatypes.o: datatypes.c datatypes.h
	$(CC) datatypes.c -c $(CFLAGS)

int_fft.o: int_fft.c
	$(CC) int_fft.c -c $(CFLAGS)

iio_widget.o: iio_widget.c iio_widget.h
	$(CC) iio_widget.c -c $(CFLAGS)

fru.o: fru.c fru.h
	$(CC) fru.c -c $(CFLAGS)

dialogs.o: dialogs.c fru.h osc.h
	$(CC) dialogs.c -c $(CFLAGS) -DFRU_FILES=\"$(FRU_FILES)\"

trigger_dialog.o: trigger_dialog.c fru.h osc.h iio_widget.h
	$(CC) trigger_dialog.c -c $(CFLAGS)

xml_utils.o: xml_utils.c xml_utils.h
	$(CC) xml_utils.c -c $(CFLAGS)

dac_data_manager.o: plugins/dac_data_manager.c plugins/dac_data_manager.h
	$(CC) plugins/dac_data_manager.c -c $(CFLAGS)

%.so: libosc.so %.c
	$(CC) $+ $(CFLAGS) $(LDFLAGS) -shared -o $@

install:
	install -d $(DESTDIR)/bin
	install -d $(DESTDIR)/share/osc/
	install -d $(DESTDIR)/lib/osc/
	install -d $(DESTDIR)/lib/osc/xmls
	install -d $(DESTDIR)/lib/osc/filters
	install -d $(DESTDIR)/lib/osc/waveforms
	install -d $(DESTDIR)/lib/osc/profiles
	install -d $(DESTDIR)/lib/osc/block_diagrams
	install -d $(HOME)/.config/autostart/
	install ./osc $(DESTDIR)/bin/
	install ./libosc.so $(DESTDIR)/lib/
	install ./*.glade $(PSHARE)
	install ./icons/ADIlogo.png $(PSHARE)
	install ./icons/IIOlogo.png $(PSHARE)
	install ./icons/osc128.png $(PSHARE)
	install ./icons/osc_capture.png $(PSHARE)
	install ./icons/osc_generator.png $(PSHARE)
	install ./icons/ch_color_icon.png $(PSHARE)
	install $(PLUGINS) $(PLIB)
	install ./xmls/* $(PLIB)/xmls
	install ./filters/* $(PLIB)/filters
	install ./waveforms/* $(PLIB)/waveforms
	install ./profiles/* $(PLIB)/profiles
	install ./block_diagrams/* $(PLIB)/block_diagrams
	install adi-osc.desktop $(HOME)/.config/autostart/osc.desktop

	xdg-icon-resource install --noupdate --size 16 ./icons/osc16.png adi-osc
	xdg-icon-resource install --noupdate --size 32 ./icons/osc32.png adi-osc
	xdg-icon-resource install --noupdate --size 64 ./icons/osc64.png adi-osc
	xdg-icon-resource install --noupdate --size 128 ./icons/osc128.png adi-osc
	xdg-icon-resource install --size 256 ./icons/osc256.png adi-osc
	xdg-desktop-menu install adi-osc.desktop

clean:
	rm -rf osc libosc.so *.o libini/*.o plugins/*.so

uninstall:
	rm -rf $(PLIB) $(PSHARE) $(DESTDIR)/bin/osc $(DESTDIR)/lib/libosc.so
	rm -rf $(HOME)/.osc_profile.ini
	rm -rf $(HOME)/.config/autostart/adi-osc.desktop
	xdg-icon-resource uninstall --noupdate --size 16 adi-osc
	xdg-icon-resource uninstall --noupdate --size 32 adi-osc
	xdg-icon-resource uninstall --noupdate --size 64 adi-osc
	xdg-icon-resource uninstall --noupdate --size 128 adi-osc
	xdg-icon-resource uninstall --size 256 adi-osc
	xdg-desktop-menu uninstall osc.desktop
