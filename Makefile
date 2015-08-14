PREFIX ?= /usr/local

TMP = temp_resources
PSHARE=$(DESTDIR)$(PREFIX)/share/osc
PLIB=$(DESTDIR)$(PREFIX)/lib/osc

# this is where the master fru files are (assuming they are installed at all)
FRU_FILES=$(PREFIX)/lib/fmc-tools/

CC := $(CROSS_COMPILE)gcc

SYSROOT := $(shell $(CC) -print-sysroot)
MULTIARCH := $(shell $(CC) -print-multiarch)

GIT_BRANCH := $(shell git name-rev --name-only HEAD | sed 's:.*/::')
GIT_HASH := $(shell git describe --abbrev=7 --dirty --always)
GIT_VERSION := $(shell git rev-parse --short HEAD)
GIT_COMMIT_TIMESTAMP := $(shell git show -s --pretty=format:"%ct" HEAD)

WITH_MINGW := $(if $(shell echo | $(CC) -dM -E - |grep __MINGW32__),y)
EXPORT_SYMBOLS := -Wl,--export-all-symbols
EXPORT_SYMBOLS := $(if $(WITH_MINGW),$(EXPORT_SYMBOLS))

PKG_CONFIG_PATH := $(SYSROOT)/usr/share/pkgconfig:$(SYSROOT)/usr/lib/pkgconfig:$(SYSROOT)$(PREFIX)/lib/pkgconfig:$(SYSROOT)/usr/lib/$(MULTIARCH)/pkgconfig
PKG_CONFIG := env PKG_CONFIG_SYSROOT_DIR="$(SYSROOT)" \
	PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" pkg-config

DEPENDENCIES := glib-2.0 gtk+-2.0 gthread-2.0 gtkdatabox fftw3 libiio libxml-2.0 libcurl jansson

LDFLAGS := $(shell $(PKG_CONFIG) --libs $(DEPENDENCIES)) \
	$(if $(WITH_MINGW),-lwinpthread) \
	-L$(SYSROOT)/usr/lib -lmatio -lz -lm -lad9361

ifeq ($(WITH_MINGW),y)
	LDFLAGS += -Wl,--subsystem,windows
endif

CFLAGS := $(shell $(PKG_CONFIG) --cflags $(DEPENDENCIES)) \
	-I$(SYSROOT)/usr/include $(if $(WITH_MINGW),-mwindows,-fPIC) \
	-Wall -Wclobbered -Wempty-body -Wignored-qualifiers -Wmissing-field-initializers \
	-Wmissing-parameter-type -Wold-style-declaration -Woverride-init \
	-Wsign-compare -Wtype-limits -Wuninitialized -Wunused-but-set-parameter \
	-Werror -g -std=gnu90 -D_GNU_SOURCE -O2 -DPREFIX='"$(PREFIX)"' \
	-DFRU_FILES=\"$(FRU_FILES)\" -DGIT_VERSION=\"$(GIT_VERSION)\" \
	-DGIT_COMMIT_TIMESTAMP='"$(GIT_COMMIT_TIMESTAMP)"' \
	-DOSC_VERSION=\"$(GIT_BRANCH)-g$(GIT_HASH)\" \
	-D_POSIX_C_SOURCE=200809L

DEBUG ?= 0
ifeq ($(DEBUG),1)
	CFLAGS += -DDEBUG
else
	CFLAGS += -DNDEBUG
endif

SO := $(if $(WITH_MINGW),dll,so)
EXE := $(if $(WITH_MINGW),.exe)

OSC := osc$(EXE)
LIBOSC := libosc.$(SO)

PLUGINS=\
	plugins/fmcomms1.$(SO) \
	plugins/fmcomms2.$(SO) \
	plugins/fmcomms5.$(SO) \
	plugins/fmcomms6.$(SO) \
	plugins/fmcomms2_adv.$(SO) \
	plugins/ad6676.$(SO) \
	plugins/pr_config.$(SO) \
	plugins/fmcadc3.$(SO) \
	plugins/daq2.$(SO) \
	plugins/ad9739a.$(SO) \
	plugins/AD5628_1.$(SO) \
	plugins/AD7303.$(SO) \
	plugins/cn0357.$(SO) \
	plugins/motor_control.$(SO) \
	plugins/dmm.$(SO) \
	plugins/debug.$(SO) \
	$(if $(WITH_MINGW),,plugins/spectrum_analyzer.so) \
	$(if $(WITH_MINGW),,plugins/scpi.so)

ifdef V
	CMD:=
	SUM:=@\#
else
	CMD:=@
	SUM:=@echo
endif

OSC_OBJS := osc.o oscplot.o datatypes.o int_fft.o iio_widget.o fru.o dialogs.o \
	trigger_dialog.o xml_utils.o libini/libini.o libini2.o phone_home.o \
	plugins/dac_data_manager.o plugins/fir_filter.o \
	$(if $(WITH_MINGW),,eeprom.o)

all: $(OSC) $(PLUGINS)

analyze: $(OSC_OBJS:%.o=%.c) $(PLUGINS:%.so=%.c) oscmain.c
	clang --analyze $(CFLAGS) $^

$(LIBOSC): $(OSC_OBJS)
	$(SUM) "  LD      $@"
	$(CMD)$(CC) $+ $(CFLAGS) $(LDFLAGS) -ldl -shared -o $@ $(EXPORT_SYMBOLS)

$(OSC): oscmain.o $(if $(WITH_MINGW),oscicon.o) $(LIBOSC)
	$(SUM) "  LD      $@"
	$(CMD)$(CC) $^ $(LDFLAGS) -L. -losc -o $@

oscicon.o: oscicon.rc
	$(SUM) "  GEN     $@"
	$(CMD)$(CROSS_COMPILE)windres $< $@

%.o: %.c
	$(SUM) "  CC      $@"
	$(CMD)$(CC) $(CFLAGS) $< -c -o $@

%.$(SO): %.c $(LIBOSC)
	$(SUM) "  LD      $@"
	$(CMD)$(CC) $(CFLAGS) $< $(LDFLAGS) -L. -losc -shared -o $@

# Dependencies
osc.o: iio_widget.h int_fft.h osc_plugin.h osc.h libini2.h
oscmain.o: config.h osc.h
oscplot.o: oscplot.h osc.h datatypes.h iio_widget.h libini2.h
datatypes.o: datatypes.h
iio_widget.o: iio_widget.h
fru.o: fru.h
dialogs.o: fru.h osc.h
trigger_dialog.o: fru.h osc.h iio_widget.h
xml_utils.o: xml_utils.h
phone_home.o: phone_home.h
plugins/dac_data_manager.o: plugins/dac_data_manager.h
plugins/ad9361_multichip_sync.o: plugins/ad9361_multichip_sync.h

install-common-files: $(OSC) $(PLUGINS)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -d $(DESTDIR)$(PREFIX)/share/osc/
	install -d $(DESTDIR)$(PREFIX)/lib/osc/
	install -d $(DESTDIR)$(PREFIX)/lib/osc/xmls
	install -d $(DESTDIR)$(PREFIX)/lib/osc/filters
	install -d $(DESTDIR)$(PREFIX)/lib/osc/waveforms
	install -d $(DESTDIR)$(PREFIX)/lib/osc/profiles
	install -d $(DESTDIR)$(PREFIX)/lib/osc/block_diagrams
	install ./$(OSC) $(DESTDIR)$(PREFIX)/bin/
	install ./$(LIBOSC) $(DESTDIR)$(PREFIX)/$(if $(WITH_MINGW),bin,lib)/
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

install-all: install-common-files
	xdg-icon-resource install --noupdate --size 16 ./icons/osc16.png adi-osc
	xdg-icon-resource install --noupdate --size 32 ./icons/osc32.png adi-osc
	xdg-icon-resource install --noupdate --size 64 ./icons/osc64.png adi-osc
	xdg-icon-resource install --noupdate --size 128 ./icons/osc128.png adi-osc
	xdg-icon-resource install --size 256 ./icons/osc256.png adi-osc
	xdg-desktop-menu install adi-osc.desktop
	ldconfig

uninstall-common-files:
	rm -rf $(PLIB) $(PSHARE) $(DESTDIR)$(PREFIX)/bin/$(OSC) $(DESTDIR)$(PREFIX)/lib/$(LIBOSC)

uninstall-all: uninstall-common-files
	xdg-icon-resource uninstall --noupdate --size 16 adi-osc
	xdg-icon-resource uninstall --noupdate --size 32 adi-osc
	xdg-icon-resource uninstall --noupdate --size 64 adi-osc
	xdg-icon-resource uninstall --noupdate --size 128 adi-osc
	xdg-icon-resource uninstall --size 256 adi-osc
	xdg-desktop-menu uninstall osc.desktop
	ldconfig

install: $(if $(DEBIAN_INSTALL),install-common-files,install-all)

uninstall: $(if $(DEBIAN_INSTALL),uninstall-common-files,uninstall-all)

clean:
	$(SUM) "  CLEAN    ."
	$(CMD)rm -rf $(OSC) $(LIBOSC) $(PLUGINS) *.o libini/*.o plugins/*.o *.plist
