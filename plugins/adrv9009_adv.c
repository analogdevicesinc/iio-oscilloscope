/**
 * Copyright (C) 2018 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <glib.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <iio.h>

#include "../libini2.h"
#include "../osc.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../iio_widget.h"
#include "../datatypes.h"
#include "../iio_utils.h"

#define PHY_DEVICE "adrv9009-phy"
#define DDS_DEVICE "axi-adrv9009-tx-hpc"
#define CAP_DEVICE "axi-adrv9009-rx-hpc"
#define THIS_DRIVER "ADRV9009 Advanced"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

struct plugin_private {
	/* References to IIO structures */
	struct iio_context *ctx;
	struct iio_device *dev;

	/* Misc */
	bool can_update_widgets;
	gint this_page;
	GtkNotebook *nbook;
	gboolean plugin_detached;

	/* Associated GTK builder */
	GtkBuilder *builder;

	/* Save/Restore attributes */
	char **sr_attribs;
	size_t sr_attribs_count;

	/* Information upon which the plugin was constructed */
	struct osc_plugin_context plugin_ctx;
};

enum adrv9009adv_wtype {
	CHECKBOX,
	SPINBUTTON,
	COMBOBOX,
	BUTTON,
	CHECKBOX_MASK,
	SPINBUTTON_S8,
	SPINBUTTON_S16,
};

struct w_info {
	enum adrv9009adv_wtype type;
	const char *const name;
	const unsigned char *const lut;
	const unsigned char lut_len;
};

struct plugin_and_w_info {
		struct osc_plugin *plugin;
		struct w_info *item;
};

static struct w_info attrs[] = {
	{SPINBUTTON, "adi,rxagc-peak-agc-under-range-low-interval_ns", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-agc-under-range-mid-interval", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-agc-under-range-high-interval", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-apd-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-apd-low-gain-mode-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-apd-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-apd-low-gain-mode-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-apd-upper-thresh-peak-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-apd-lower-thresh-peak-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-apd-gain-step-attack", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-apd-gain-step-recovery", NULL, 0},
	{CHECKBOX, "adi,rxagc-peak-enable-hb2-overload", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-overload-duration-cnt", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-overload-thresh-cnt", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-under-range-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-under-range-mid-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-under-range-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-upper-thresh-peak-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-lower-thresh-peak-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-gain-step-high-recovery", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-gain-step-low-recovery", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-gain-step-mid-recovery", NULL, 0},
	{SPINBUTTON, "adi,rxagc-peak-hb2-gain-step-attack", NULL, 0},
	{CHECKBOX, "adi,rxagc-peak-hb2-overload-power-mode", NULL, 0},
	{CHECKBOX, "adi,rxagc-peak-hb2-ovrg-sel", NULL, 0},
	{CHECKBOX, "adi,rxagc-peak-hb2-thresh-config", NULL, 0},

	{CHECKBOX, "adi,rxagc-power-power-enable-measurement", NULL, 0},
	{CHECKBOX, "adi,rxagc-power-power-use-rfir-out", NULL, 0},
	{CHECKBOX, "adi,rxagc-power-power-use-bbdc2", NULL, 0},
	{SPINBUTTON, "adi,rxagc-power-under-range-high-power-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-power-under-range-low-power-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-power-under-range-high-power-gain-step-recovery", NULL, 0},
	{SPINBUTTON, "adi,rxagc-power-under-range-low-power-gain-step-recovery", NULL, 0},
	{SPINBUTTON, "adi,rxagc-power-power-measurement-duration", NULL, 0},
	{SPINBUTTON, "adi,rxagc-power-rx1-tdd-power-meas-duration", NULL, 0},
	{SPINBUTTON, "adi,rxagc-power-rx1-tdd-power-meas-delay", NULL, 0},
	{SPINBUTTON, "adi,rxagc-power-rx2-tdd-power-meas-duration", NULL, 0},
	{SPINBUTTON, "adi,rxagc-power-rx2-tdd-power-meas-delay", NULL, 0},
	{SPINBUTTON, "adi,rxagc-power-upper0-power-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-power-upper1-power-thresh", NULL, 0},
	{CHECKBOX, "adi,rxagc-power-power-log-shift", NULL, 0},

	{SPINBUTTON, "adi,rxagc-agc-peak-wait-time", NULL, 0},
	{SPINBUTTON, "adi,rxagc-agc-rx1-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rxagc-agc-rx1-min-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rxagc-agc-rx2-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rxagc-agc-rx2-min-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rxagc-agc-gain-update-counter_us", NULL, 0},
	{SPINBUTTON, "adi,rxagc-agc-rx1-attack-delay", NULL, 0},
	{SPINBUTTON, "adi,rxagc-agc-rx2-attack-delay", NULL, 0},
	{SPINBUTTON, "adi,rxagc-agc-slow-loop-settling-delay", NULL, 0},
	{CHECKBOX, "adi,rxagc-agc-low-thresh-prevent-gain", NULL, 0},
	{CHECKBOX, "adi,rxagc-agc-change-gain-if-thresh-high", NULL, 0},
	{CHECKBOX, "adi,rxagc-agc-peak-thresh-gain-control-mode", NULL, 0},
	{CHECKBOX, "adi,rxagc-agc-reset-on-rxon", NULL, 0},
	{CHECKBOX, "adi,rxagc-agc-enable-sync-pulse-for-gain-counter", NULL, 0},
	{CHECKBOX, "adi,rxagc-agc-enable-ip3-optimization-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-ip3-over-range-thresh", NULL, 0},
	{SPINBUTTON, "adi,rxagc-ip3-over-range-thresh-index", NULL, 0},
	{SPINBUTTON, "adi,rxagc-ip3-peak-exceeded-cnt", NULL, 0},
	{CHECKBOX, "adi,rxagc-agc-enable-fast-recovery-loop", NULL, 0},

	{CHECKBOX_MASK, "adi,aux-dac-enables#0", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enables#1", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enables#2", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enables#3", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enables#4", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enables#5", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enables#6", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enables#7", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enables#8", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enables#9", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enables#10", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enables#11", NULL, 0},

	{COMBOBOX, "adi,aux-dac-vref0", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,aux-dac-resolution0", (unsigned char[]){0,1,2}, 3},
	{SPINBUTTON, "adi,aux-dac-values0", NULL, 0},
	{COMBOBOX, "adi,aux-dac-vref1", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,aux-dac-resolution1", (unsigned char[]){0,1,2}, 3},
	{SPINBUTTON, "adi,aux-dac-values1", NULL, 0},
	{COMBOBOX, "adi,aux-dac-vref2", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,aux-dac-resolution2", (unsigned char[]){0,1,2}, 3},
	{SPINBUTTON, "adi,aux-dac-values2", NULL, 0},
	{COMBOBOX, "adi,aux-dac-vref3", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,aux-dac-resolution3", (unsigned char[]){0,1,2}, 3},
	{SPINBUTTON, "adi,aux-dac-values3", NULL, 0},
	{COMBOBOX, "adi,aux-dac-vref4", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,aux-dac-resolution4", (unsigned char[]){0,1,2}, 3},
	{SPINBUTTON, "adi,aux-dac-values4", NULL, 0},
	{COMBOBOX, "adi,aux-dac-vref5", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,aux-dac-resolution5", (unsigned char[]){0,1,2}, 3},
	{SPINBUTTON, "adi,aux-dac-values5", NULL, 0},
	{COMBOBOX, "adi,aux-dac-vref6", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,aux-dac-resolution6", (unsigned char[]){0,1,2}, 3},
	{SPINBUTTON, "adi,aux-dac-values6", NULL, 0},
	{COMBOBOX, "adi,aux-dac-vref7", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,aux-dac-resolution7", (unsigned char[]){0,1,2}, 3},
	{SPINBUTTON, "adi,aux-dac-values7", NULL, 0},
	{COMBOBOX, "adi,aux-dac-vref8", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,aux-dac-resolution8", (unsigned char[]){0,1,2}, 3},
	{SPINBUTTON, "adi,aux-dac-values8", NULL, 0},
	{COMBOBOX, "adi,aux-dac-vref9", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,aux-dac-resolution9", (unsigned char[]){0,1,2}, 3},
	{SPINBUTTON, "adi,aux-dac-values9", NULL, 0},
	{SPINBUTTON, "adi,aux-dac-values10", NULL, 0},
	{SPINBUTTON, "adi,aux-dac-values11", NULL, 0},
	{SPINBUTTON, "adi,jesd204-framer-a-bank-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-framer-a-device-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-framer-a-lane0-id", NULL, 0},
	{COMBOBOX, "adi,jesd204-framer-a-m", (unsigned char[]){0,2,4}, 3},
	{SPINBUTTON, "adi,jesd204-framer-a-k", NULL, 0},
	{COMBOBOX, "adi,jesd204-framer-a-f", (unsigned char[]){1, 2, 3, 4, 6, 8}, 6},
	{COMBOBOX, "adi,jesd204-framer-a-np", (unsigned char[]){12,16,24}, 3},
	{CHECKBOX, "adi,jesd204-framer-a-scramble", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-a-external-sysref", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-framer-a-serializer-lanes-enabled#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-framer-a-serializer-lanes-enabled#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-framer-a-serializer-lanes-enabled#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-framer-a-serializer-lanes-enabled#3", NULL, 0},
	{SPINBUTTON, "adi,jesd204-framer-a-serializer-lane-crossbar", NULL, 0},
	{SPINBUTTON, "adi,jesd204-framer-a-lmfc-offset", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-a-new-sysref-on-relink", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-a-syncb-in-select", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-a-over-sample", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-a-syncb-in-lvds-mode", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-a-syncb-in-lvds-pn-invert", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-a-enable-manual-lane-xbar", NULL, 0},
	{SPINBUTTON, "adi,jesd204-framer-b-bank-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-framer-b-device-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-framer-b-lane0-id", NULL, 0},
	{COMBOBOX, "adi,jesd204-framer-b-m", (unsigned char[]){0,2,4}, 3},
	{SPINBUTTON, "adi,jesd204-framer-b-k", NULL, 0},
	{COMBOBOX, "adi,jesd204-framer-b-f", (unsigned char[]){1, 2, 3, 4, 6, 8}, 6},
	{COMBOBOX, "adi,jesd204-framer-b-np", (unsigned char[]){12,16,24}, 3},
	{CHECKBOX, "adi,jesd204-framer-b-scramble", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-b-external-sysref", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-framer-b-serializer-lanes-enabled#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-framer-b-serializer-lanes-enabled#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-framer-b-serializer-lanes-enabled#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-framer-b-serializer-lanes-enabled#3", NULL, 0},

	{SPINBUTTON, "adi,jesd204-framer-b-serializer-lane-crossbar", NULL, 0},
	{SPINBUTTON, "adi,jesd204-framer-b-lmfc-offset", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-b-new-sysref-on-relink", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-b-syncb-in-select", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-b-over-sample", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-b-syncb-in-lvds-mode", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-b-syncb-in-lvds-pn-invert", NULL, 0},
	{CHECKBOX, "adi,jesd204-framer-b-enable-manual-lane-xbar", NULL, 0},

	{SPINBUTTON, "adi,jesd204-deframer-a-bank-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-a-device-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-a-lane0-id", NULL, 0},
	{COMBOBOX, "adi,jesd204-deframer-a-m", (unsigned char[]){0,2,4}, 3},
	{SPINBUTTON, "adi,jesd204-deframer-a-k", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-a-scramble", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-a-external-sysref", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-a-deserializer-lanes-enabled#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-a-deserializer-lanes-enabled#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-a-deserializer-lanes-enabled#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-a-deserializer-lanes-enabled#3", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-a-deserializer-lane-crossbar", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-a-lmfc-offset", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-a-new-sysref-on-relink", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-a-syncb-out-select", NULL, 0},
	{COMBOBOX, "adi,jesd204-deframer-a-np", (unsigned char[]){12, 16}, 2},
	{CHECKBOX, "adi,jesd204-deframer-a-syncb-out-lvds-mode", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-a-syncb-out-lvds-pn-invert", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-a-syncb-out-cmos-slew-rate", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-a-syncb-out-cmos-drive-level", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-a-enable-manual-lane-xbar", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-b-bank-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-b-device-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-b-lane0-id", NULL, 0},
	{COMBOBOX, "adi,jesd204-deframer-b-m", (unsigned char[]){0,2,4}, 3},
	{SPINBUTTON, "adi,jesd204-deframer-b-k", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-b-scramble", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-b-external-sysref", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-b-deserializer-lanes-enabled#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-b-deserializer-lanes-enabled#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-b-deserializer-lanes-enabled#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-b-deserializer-lanes-enabled#3", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-b-deserializer-lane-crossbar", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-b-lmfc-offset", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-b-new-sysref-on-relink", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-b-syncb-out-select", NULL, 0},
	{COMBOBOX, "adi,jesd204-deframer-b-np", (unsigned char[]){12, 16}, 2},
	{CHECKBOX, "adi,jesd204-deframer-b-syncb-out-lvds-mode", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-b-syncb-out-lvds-pn-invert", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-b-syncb-out-cmos-slew-rate", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-b-syncb-out-cmos-drive-level", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-b-enable-manual-lane-xbar", NULL, 0},

	{SPINBUTTON, "adi,jesd204-ser-amplitude", NULL, 0},
	{SPINBUTTON, "adi,jesd204-ser-pre-emphasis", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-ser-invert-lane-polarity#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-ser-invert-lane-polarity#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-ser-invert-lane-polarity#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-ser-invert-lane-polarity#3", NULL, 0},

	{CHECKBOX_MASK, "adi,jesd204-des-invert-lane-polarity#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-des-invert-lane-polarity#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-des-invert-lane-polarity#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-des-invert-lane-polarity#3", NULL, 0},

	{SPINBUTTON, "adi,jesd204-des-eq-setting", NULL, 0},
	{CHECKBOX, "adi,jesd204-sysref-lvds-mode", NULL, 0},
	{CHECKBOX, "adi,jesd204-sysref-lvds-pn-invert", NULL, 0},

	{SPINBUTTON, "adi,arm-gpio-config-orx1-tx-sel0-pin-gpio-pin-sel", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-config-orx1-tx-sel0-pin-polarity", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-config-orx1-tx-sel0-pin-enable", NULL, 0},

	{SPINBUTTON, "adi,arm-gpio-config-orx1-tx-sel1-pin-gpio-pin-sel", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-config-orx1-tx-sel1-pin-polarity", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-config-orx1-tx-sel1-pin-enable", NULL, 0},
	{SPINBUTTON, "adi,arm-gpio-config-orx2-tx-sel0-pin-gpio-pin-sel", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-config-orx2-tx-sel0-pin-polarity", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-config-orx2-tx-sel0-pin-enable", NULL, 0},

	{SPINBUTTON, "adi,arm-gpio-config-orx2-tx-sel1-pin-gpio-pin-sel", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-config-orx2-tx-sel1-pin-polarity", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-config-orx2-tx-sel1-pin-enable", NULL, 0},
	{SPINBUTTON, "adi,arm-gpio-config-en-tx-tracking-cals-gpio-pin-sel", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-config-en-tx-tracking-cals-polarity", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-config-en-tx-tracking-cals-enable", NULL, 0},
	{CHECKBOX, "adi,orx-lo-cfg-disable-aux-pll-relocking", NULL, 0},
	{SPINBUTTON, "adi,orx-lo-cfg-gpio-select", NULL, 0},

	{SPINBUTTON, "adi,fhm-config-fhm-gpio-pin", NULL, 0},
	{SPINBUTTON, "adi,fhm-config-fhm-min-freq_mhz", NULL, 0},
	{SPINBUTTON, "adi,fhm-config-fhm-max-freq_mhz", NULL, 0},
	{CHECKBOX, "adi,fhm-mode-fhm-enable", NULL, 0},
	{CHECKBOX, "adi,fhm-mode-enable-mcs-sync", NULL, 0},
	{CHECKBOX, "adi,fhm-mode-fhm-trigger-mode", NULL, 0},
	{CHECKBOX, "adi,fhm-mode-fhm-exit-mode", NULL, 0},
	{SPINBUTTON, "adi,fhm-mode-fhm-init-frequency_hz", NULL, 0},

	{SPINBUTTON, "adi,rx1-gain-ctrl-pin-inc-step", NULL, 0},
	{SPINBUTTON, "adi,rx1-gain-ctrl-pin-dec-step", NULL, 0},
	{COMBOBOX, "adi,rx1-gain-ctrl-pin-rx-gain-inc-pin", (unsigned char[]){0,10}, 2},
	{COMBOBOX, "adi,rx1-gain-ctrl-pin-rx-gain-dec-pin", (unsigned char[]){1,11}, 2},
	{CHECKBOX, "adi,rx1-gain-ctrl-pin-enable", NULL, 0},
	{SPINBUTTON, "adi,rx2-gain-ctrl-pin-inc-step", NULL, 0},
	{SPINBUTTON, "adi,rx2-gain-ctrl-pin-dec-step", NULL, 0},
	{COMBOBOX, "adi,rx2-gain-ctrl-pin-rx-gain-inc-pin", (unsigned char[]){3,13}, 2},
	{COMBOBOX, "adi,rx2-gain-ctrl-pin-rx-gain-dec-pin", (unsigned char[]){4,14}, 2},
	{CHECKBOX, "adi,rx2-gain-ctrl-pin-enable", NULL, 0},

	{SPINBUTTON, "adi,tx1-atten-ctrl-pin-step-size", NULL, 0},
	{COMBOBOX, "adi,tx1-atten-ctrl-pin-tx-atten-inc-pin", (unsigned char[]){4,12}, 2},
	{COMBOBOX, "adi,tx1-atten-ctrl-pin-tx-atten-dec-pin", (unsigned char[]){5,13}, 2},
	{CHECKBOX, "adi,tx1-atten-ctrl-pin-enable", NULL, 0},
	{SPINBUTTON, "adi,tx2-atten-ctrl-pin-step-size", NULL, 0},
	{COMBOBOX, "adi,tx2-atten-ctrl-pin-tx-atten-inc-pin", (unsigned char[]){6,14}, 2},
	{COMBOBOX, "adi,tx2-atten-ctrl-pin-tx-atten-dec-pin", (unsigned char[]){7,15}, 2},
	{CHECKBOX, "adi,tx2-atten-ctrl-pin-enable", NULL, 0},

	{SPINBUTTON, "adi,tx-pa-protection-avg-duration", NULL, 0},
	{SPINBUTTON, "adi,tx-pa-protection-tx-atten-step", NULL, 0},
	{SPINBUTTON, "adi,tx-pa-protection-tx1-power-threshold", NULL, 0},
	{SPINBUTTON, "adi,tx-pa-protection-tx2-power-threshold", NULL, 0},
	{SPINBUTTON, "adi,tx-pa-protection-peak-count", NULL, 0},
	{SPINBUTTON, "adi,tx-pa-protection-tx1-peak-threshold", NULL, 0},
	{SPINBUTTON, "adi,tx-pa-protection-tx2-peak-threshold", NULL, 0},

	{COMBOBOX, "adi,rx-profile-rx-fir-decimation", (unsigned char[]){1,2,4}, 3},
	{COMBOBOX, "adi,rx-profile-rx-dec5-decimation", (unsigned char[]){4,5}, 2},
	{COMBOBOX, "adi,rx-profile-rhb1-decimation", (unsigned char[]){1,2}, 2},
	{SPINBUTTON, "adi,rx-profile-rx-output-rate_khz", NULL, 0},
	{SPINBUTTON, "adi,rx-profile-rf-bandwidth_hz", NULL, 0},
	{SPINBUTTON, "adi,rx-profile-rx-bbf3d-bcorner_khz", NULL, 0},

	{COMBOBOX, "adi,rx-profile-rx-ddc-mode", (unsigned char[]){0,1,2,3,4,5,6,7}, 8},

	{SPINBUTTON, "adi,rx-nco-shifter-band-a-input-band-width_khz", NULL, 0},
	{SPINBUTTON, "adi,rx-nco-shifter-band-a-input-center-freq_khz", NULL, 0},
	{SPINBUTTON, "adi,rx-nco-shifter-band-a-nco1-freq_khz", NULL, 0},
	{SPINBUTTON, "adi,rx-nco-shifter-band-a-nco2-freq_khz", NULL, 0},
	{SPINBUTTON, "adi,rx-nco-shifter-band-binput-band-width_khz", NULL, 0},
	{SPINBUTTON, "adi,rx-nco-shifter-band-binput-center-freq_khz", NULL, 0},
	{SPINBUTTON, "adi,rx-nco-shifter-band-bnco1-freq_khz", NULL, 0},
	{SPINBUTTON, "adi,rx-nco-shifter-band-bnco2-freq_khz", NULL, 0},

	{COMBOBOX, "adi,rx-gain-control-gain-mode", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,rx-gain-control-rx1-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx-gain-control-rx2-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx-gain-control-rx1-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx-gain-control-rx1-min-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx-gain-control-rx2-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx-gain-control-rx2-min-gain-index", NULL, 0},
	{COMBOBOX, "adi,rx-settings-framer-sel", (unsigned char[]){0,1,2}, 3},

	{COMBOBOX, "adi,rx-settings-rx-channels", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,orx-profile-rx-fir-decimation", (unsigned char[]){1,2,4}, 3},
	{COMBOBOX, "adi,orx-profile-rx-dec5-decimation", (unsigned char[]){4,5}, 2},
	{COMBOBOX, "adi,orx-profile-rhb1-decimation", (unsigned char[]){1,2}, 2},
	{SPINBUTTON, "adi,orx-profile-orx-output-rate_khz", NULL, 0},
	{SPINBUTTON, "adi,orx-profile-rf-bandwidth_hz", NULL, 0},
	{SPINBUTTON, "adi,orx-profile-rx-bbf3d-bcorner_khz", NULL, 0},
	{COMBOBOX, "adi,orx-profile-orx-ddc-mode", (unsigned char[]){7}, 1},

	{COMBOBOX, "adi,orx-gain-control-gain-mode", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,orx-gain-control-orx1-gain-index", NULL, 0},
	{SPINBUTTON, "adi,orx-gain-control-orx2-gain-index", NULL, 0},
	{SPINBUTTON, "adi,orx-gain-control-orx1-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,orx-gain-control-orx1-min-gain-index", NULL, 0},
	{SPINBUTTON, "adi,orx-gain-control-orx2-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,orx-gain-control-orx2-min-gain-index", NULL, 0},

	{COMBOBOX, "adi,obs-settings-framer-sel", (unsigned char[]){0,1,2}, 3},
	{COMBOBOX, "adi,obs-settings-obs-rx-channels-enable", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,obs-settings-obs-rx-lo-source", (unsigned char[]){0,1}, 2},

	{COMBOBOX, "adi,tx-profile-dac-div", (unsigned char[]){1,2}, 2},

	{COMBOBOX, "adi,tx-profile-tx-fir-interpolation", (unsigned char[]){1,2,4}, 3},
	{COMBOBOX, "adi,tx-profile-thb1-interpolation", (unsigned char[]){1,2}, 2},
	{COMBOBOX, "adi,tx-profile-thb2-interpolation", (unsigned char[]){1,2}, 2},
	{COMBOBOX, "adi,tx-profile-thb3-interpolation", (unsigned char[]){1,2}, 2},
	{COMBOBOX, "adi,tx-profile-tx-int5-interpolation", (unsigned char[]){1,5}, 2},
	{SPINBUTTON, "adi,tx-profile-tx-input-rate_khz", NULL, 0},
	{SPINBUTTON, "adi,tx-profile-primary-sig-bandwidth_hz", NULL, 0},
	{SPINBUTTON, "adi,tx-profile-rf-bandwidth_hz", NULL, 0},
	{SPINBUTTON, "adi,tx-profile-tx-dac3d-bcorner_khz", NULL, 0},
	{SPINBUTTON, "adi,tx-profile-tx-bbf3d-bcorner_khz", NULL, 0},
	{COMBOBOX, "adi,tx-settings-deframer-sel", (unsigned char[]){0,1,2}, 3},
	{COMBOBOX, "adi,tx-settings-tx-channels", (unsigned char[]){0,1,2,3}, 4},
	{COMBOBOX, "adi,tx-settings-tx-atten-step-size", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,tx-settings-tx1-atten_md-b", NULL, 0},
	{SPINBUTTON, "adi,tx-settings-tx2-atten_md-b", NULL, 0},
	{COMBOBOX, "adi,tx-settings-dis-tx-data-if-pll-unlock", (unsigned char[]){0,1,2}, 3},
	{SPINBUTTON, "adi,dig-clocks-device-clock_khz", NULL, 0},
	{SPINBUTTON, "adi,dig-clocks-clk-pll-vco-freq_khz", NULL, 0},
	{COMBOBOX, "adi,dig-clocks-clk-pll-hs-div", (unsigned char[]){0,1,2,3,4}, 5},
	{CHECKBOX, "adi,dig-clocks-rf-pll-use-external-lo", NULL, 0},
	{COMBOBOX, "adi,dig-clocks-rf-pll-phase-sync-mode", (unsigned char[]){0,1,2,3}, 4},

	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#8", NULL, 0},
	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#9", NULL, 0},
	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#10", NULL, 0},
	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#14", NULL, 0},
	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#15", NULL, 0},
	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#23", NULL, 0},

	{COMBOBOX, "bist_framer_a_prbs", (unsigned char[]){0, 1, 2, 3, 4, 5, 6, 7, 8, 14, 15}, 11},
	{COMBOBOX, "bist_framer_b_prbs", (unsigned char[]){0, 1, 2, 3, 4, 5, 6, 7, 8, 14, 15}, 11},

	{BUTTON, "initialize", NULL, 0},
};

static const char *adrv9009_adv_sr_attribs[] = {
	".adi,rxagc-peak-agc-under-range-low-interval_ns",
	".adi,rxagc-peak-agc-under-range-mid-interval",
	".adi,rxagc-peak-agc-under-range-high-interval",
	".adi,rxagc-peak-apd-high-thresh",
	".adi,rxagc-peak-apd-low-gain-mode-high-thresh",
	".adi,rxagc-peak-apd-low-thresh",
	".adi,rxagc-peak-apd-low-gain-mode-low-thresh",
	".adi,rxagc-peak-apd-upper-thresh-peak-exceeded-cnt",
	".adi,rxagc-peak-apd-lower-thresh-peak-exceeded-cnt",
	".adi,rxagc-peak-apd-gain-step-attack",
	".adi,rxagc-peak-apd-gain-step-recovery",
	".adi,rxagc-peak-enable-hb2-overload",
	".adi,rxagc-peak-hb2-overload-duration-cnt",
	".adi,rxagc-peak-hb2-overload-thresh-cnt",
	".adi,rxagc-peak-hb2-high-thresh",
	".adi,rxagc-peak-hb2-under-range-low-thresh",
	".adi,rxagc-peak-hb2-under-range-mid-thresh",
	".adi,rxagc-peak-hb2-under-range-high-thresh",
	".adi,rxagc-peak-hb2-upper-thresh-peak-exceeded-cnt",
	".adi,rxagc-peak-hb2-lower-thresh-peak-exceeded-cnt",
	".adi,rxagc-peak-hb2-gain-step-high-recovery",
	".adi,rxagc-peak-hb2-gain-step-low-recovery",
	".adi,rxagc-peak-hb2-gain-step-mid-recovery",
	".adi,rxagc-peak-hb2-gain-step-attack",
	".adi,rxagc-peak-hb2-overload-power-mode",
	".adi,rxagc-peak-hb2-ovrg-sel",
	".adi,rxagc-peak-hb2-thresh-config",

	".adi,rxagc-power-power-enable-measurement",
	".adi,rxagc-power-power-use-rfir-out",
	".adi,rxagc-power-power-use-bbdc2",
	".adi,rxagc-power-under-range-high-power-thresh",
	".adi,rxagc-power-under-range-low-power-thresh",
	".adi,rxagc-power-under-range-high-power-gain-step-recovery",
	".adi,rxagc-power-under-range-low-power-gain-step-recovery",
	".adi,rxagc-power-power-measurement-duration",
	".adi,rxagc-power-rx1-tdd-power-meas-duration",
	".adi,rxagc-power-rx1-tdd-power-meas-delay",
	".adi,rxagc-power-rx2-tdd-power-meas-duration",
	".adi,rxagc-power-rx2-tdd-power-meas-delay",
	".adi,rxagc-power-upper0-power-thresh",
	".adi,rxagc-power-upper1-power-thresh",
	".adi,rxagc-power-power-log-shift",

	".adi,rxagc-agc-peak-wait-time",
	".adi,rxagc-agc-rx1-max-gain-index",
	".adi,rxagc-agc-rx1-min-gain-index",
	".adi,rxagc-agc-rx2-max-gain-index",
	".adi,rxagc-agc-rx2-min-gain-index",
	".adi,rxagc-agc-gain-update-counter_us",
	".adi,rxagc-agc-rx1-attack-delay",
	".adi,rxagc-agc-rx2-attack-delay",
	".adi,rxagc-agc-slow-loop-settling-delay",
	".adi,rxagc-agc-low-thresh-prevent-gain",
	".adi,rxagc-agc-change-gain-if-thresh-high",
	".adi,rxagc-agc-peak-thresh-gain-control-mode",
	".adi,rxagc-agc-reset-on-rxon",
	".adi,rxagc-agc-enable-sync-pulse-for-gain-counter",
	".adi,rxagc-agc-enable-ip3-optimization-thresh",
	".adi,rxagc-ip3-over-range-thresh",
	".adi,rxagc-ip3-over-range-thresh-index",
	".adi,rxagc-ip3-peak-exceeded-cnt",
	".adi,rxagc-agc-enable-fast-recovery-loop",

	".adi,aux-dac-enables",
	".adi,aux-dac-vref0",
	".adi,aux-dac-resolution0",
	".adi,aux-dac-values0",
	".adi,aux-dac-vref1",
	".adi,aux-dac-resolution1",
	".adi,aux-dac-values1",
	".adi,aux-dac-vref2",
	".adi,aux-dac-resolution2",
	".adi,aux-dac-values2",
	".adi,aux-dac-vref3",
	".adi,aux-dac-resolution3",
	".adi,aux-dac-values3",
	".adi,aux-dac-vref4",
	".adi,aux-dac-resolution4",
	".adi,aux-dac-values4",
	".adi,aux-dac-vref5",
	".adi,aux-dac-resolution5",
	".adi,aux-dac-values5",
	".adi,aux-dac-vref6",
	".adi,aux-dac-resolution6",
	".adi,aux-dac-values6",
	".adi,aux-dac-vref7",
	".adi,aux-dac-resolution7",
	".adi,aux-dac-values7",
	".adi,aux-dac-vref8",
	".adi,aux-dac-resolution8",
	".adi,aux-dac-values8",
	".adi,aux-dac-vref9",
	".adi,aux-dac-resolution9",
	".adi,aux-dac-values9",
	".adi,aux-dac-values10",
	".adi,aux-dac-values11",
	".adi,jesd204-framer-a-bank-id",
	".adi,jesd204-framer-a-device-id",
	".adi,jesd204-framer-a-lane0-id",
	".adi,jesd204-framer-a-m",
	".adi,jesd204-framer-a-k",
	".adi,jesd204-framer-a-f",
	".adi,jesd204-framer-a-np",
	".adi,jesd204-framer-a-scramble",
	".adi,jesd204-framer-a-external-sysref",
	".adi,jesd204-framer-a-serializer-lanes-enabled",
	".adi,jesd204-framer-a-serializer-lane-crossbar",
	".adi,jesd204-framer-a-lmfc-offset",
	".adi,jesd204-framer-a-new-sysref-on-relink",
	".adi,jesd204-framer-a-syncb-in-select",
	".adi,jesd204-framer-a-over-sample",
	".adi,jesd204-framer-a-syncb-in-lvds-mode",
	".adi,jesd204-framer-a-syncb-in-lvds-pn-invert",
	".adi,jesd204-framer-a-enable-manual-lane-xbar",
	".adi,jesd204-framer-b-bank-id",
	".adi,jesd204-framer-b-device-id",
	".adi,jesd204-framer-b-lane0-id",
	".adi,jesd204-framer-b-m",
	".adi,jesd204-framer-b-k",
	".adi,jesd204-framer-b-f",
	".adi,jesd204-framer-b-np",
	".adi,jesd204-framer-b-scramble",
	".adi,jesd204-framer-b-external-sysref",
	".adi,jesd204-framer-b-serializer-lanes-enabled",
	".adi,jesd204-framer-b-serializer-lane-crossbar",
	".adi,jesd204-framer-b-lmfc-offset",
	".adi,jesd204-framer-b-new-sysref-on-relink",
	".adi,jesd204-framer-b-syncb-in-select",
	".adi,jesd204-framer-b-over-sample",
	".adi,jesd204-framer-b-syncb-in-lvds-mode",
	".adi,jesd204-framer-b-syncb-in-lvds-pn-invert",
	".adi,jesd204-framer-b-enable-manual-lane-xbar",

	".adi,jesd204-deframer-a-bank-id",
	".adi,jesd204-deframer-a-device-id",
	".adi,jesd204-deframer-a-lane0-id",
	".adi,jesd204-deframer-a-m",
	".adi,jesd204-deframer-a-k",
	".adi,jesd204-deframer-a-scramble",
	".adi,jesd204-deframer-a-external-sysref",
	".adi,jesd204-deframer-a-deserializer-lanes-enabled",
	".adi,jesd204-deframer-a-deserializer-lane-crossbar",
	".adi,jesd204-deframer-a-lmfc-offset",
	".adi,jesd204-deframer-a-new-sysref-on-relink",
	".adi,jesd204-deframer-a-syncb-out-select",
	".adi,jesd204-deframer-a-np",
	".adi,jesd204-deframer-a-syncb-out-lvds-mode",
	".adi,jesd204-deframer-a-syncb-out-lvds-pn-invert",
	".adi,jesd204-deframer-a-syncb-out-cmos-slew-rate",
	".adi,jesd204-deframer-a-syncb-out-cmos-drive-level",
	".adi,jesd204-deframer-a-enable-manual-lane-xbar",
	".adi,jesd204-deframer-b-bank-id",
	".adi,jesd204-deframer-b-device-id",
	".adi,jesd204-deframer-b-lane0-id",
	".adi,jesd204-deframer-b-m",
	".adi,jesd204-deframer-b-k",
	".adi,jesd204-deframer-b-scramble",
	".adi,jesd204-deframer-b-external-sysref",
	".adi,jesd204-deframer-b-deserializer-lanes-enabled",
	".adi,jesd204-deframer-b-deserializer-lane-crossbar",
	".adi,jesd204-deframer-b-lmfc-offset",
	".adi,jesd204-deframer-b-new-sysref-on-relink",
	".adi,jesd204-deframer-b-syncb-out-select",
	".adi,jesd204-deframer-b-np",
	".adi,jesd204-deframer-b-syncb-out-lvds-mode",
	".adi,jesd204-deframer-b-syncb-out-lvds-pn-invert",
	".adi,jesd204-deframer-b-syncb-out-cmos-slew-rate",
	".adi,jesd204-deframer-b-syncb-out-cmos-drive-level",
	".adi,jesd204-deframer-b-enable-manual-lane-xbar",

	".adi,jesd204-ser-amplitude",
	".adi,jesd204-ser-pre-emphasis",
	".adi,jesd204-ser-invert-lane-polarity",
	".adi,jesd204-des-invert-lane-polarity",
	".adi,jesd204-des-eq-setting",
	".adi,jesd204-sysref-lvds-mode",
	".adi,jesd204-sysref-lvds-pn-invert",

	".adi,arm-gpio-config-orx1-tx-sel0-pin-gpio-pin-sel",
	".adi,arm-gpio-config-orx1-tx-sel0-pin-polarity",
	".adi,arm-gpio-config-orx1-tx-sel0-pin-enable",

	".adi,arm-gpio-config-orx1-tx-sel1-pin-gpio-pin-sel",
	".adi,arm-gpio-config-orx1-tx-sel1-pin-polarity",
	".adi,arm-gpio-config-orx1-tx-sel1-pin-enable",
	".adi,arm-gpio-config-orx2-tx-sel0-pin-gpio-pin-sel",
	".adi,arm-gpio-config-orx2-tx-sel0-pin-polarity",
	".adi,arm-gpio-config-orx2-tx-sel0-pin-enable",

	".adi,arm-gpio-config-orx2-tx-sel1-pin-gpio-pin-sel",
	".adi,arm-gpio-config-orx2-tx-sel1-pin-polarity",
	".adi,arm-gpio-config-orx2-tx-sel1-pin-enable",
	".adi,arm-gpio-config-en-tx-tracking-cals-gpio-pin-sel",
	".adi,arm-gpio-config-en-tx-tracking-cals-polarity",
	".adi,arm-gpio-config-en-tx-tracking-cals-enable",
	".adi,orx-lo-cfg-disable-aux-pll-relocking",
	".adi,orx-lo-cfg-gpio-select",

	".adi,fhm-config-fhm-gpio-pin",
	".adi,fhm-config-fhm-min-freq_mhz",
	".adi,fhm-config-fhm-max-freq_mhz",
	".adi,fhm-mode-fhm-enable",
	".adi,fhm-mode-enable-mcs-sync",
	".adi,fhm-mode-fhm-trigger-mode",
	".adi,fhm-mode-fhm-exit-mode",
	".adi,fhm-mode-fhm-init-frequency_hz",

	".adi,rx1-gain-ctrl-pin-inc-step",
	".adi,rx1-gain-ctrl-pin-dec-step",
	".adi,rx1-gain-ctrl-pin-rx-gain-inc-pin",
	".adi,rx1-gain-ctrl-pin-rx-gain-dec-pin",
	".adi,rx1-gain-ctrl-pin-enable",
	".adi,rx2-gain-ctrl-pin-inc-step",
	".adi,rx2-gain-ctrl-pin-dec-step",
	".adi,rx2-gain-ctrl-pin-rx-gain-inc-pin",
	".adi,rx2-gain-ctrl-pin-rx-gain-dec-pin",
	".adi,rx2-gain-ctrl-pin-enable",

	".adi,tx1-atten-ctrl-pin-step-size",
	".adi,tx1-atten-ctrl-pin-tx-atten-inc-pin",
	".adi,tx1-atten-ctrl-pin-tx-atten-dec-pin",
	".adi,tx1-atten-ctrl-pin-enable",
	".adi,tx2-atten-ctrl-pin-step-size",
	".adi,tx2-atten-ctrl-pin-tx-atten-inc-pin",
	".adi,tx2-atten-ctrl-pin-tx-atten-dec-pin",
	".adi,tx2-atten-ctrl-pin-enable",

	".adi,tx-pa-protection-avg-duration",
	".adi,tx-pa-protection-tx-atten-step",
	".adi,tx-pa-protection-tx1-power-threshold",
	".adi,tx-pa-protection-tx2-power-threshold",
	".adi,tx-pa-protection-peak-count",
	".adi,tx-pa-protection-tx1-peak-threshold",
	".adi,tx-pa-protection-tx2-peak-threshold",

	".adi,rx-profile-rx-fir-decimation",
	".adi,rx-profile-rx-dec5-decimation",
	".adi,rx-profile-rhb1-decimation",
	".adi,rx-profile-rx-output-rate_khz",
	".adi,rx-profile-rf-bandwidth_hz",
	".adi,rx-profile-rx-bbf3d-bcorner_khz",

	".adi,rx-profile-rx-ddc-mode",

	".adi,rx-nco-shifter-band-a-input-band-width_khz",
	".adi,rx-nco-shifter-band-a-input-center-freq_khz",
	".adi,rx-nco-shifter-band-a-nco1-freq_khz",
	".adi,rx-nco-shifter-band-a-nco2-freq_khz",
	".adi,rx-nco-shifter-band-binput-band-width_khz",
	".adi,rx-nco-shifter-band-binput-center-freq_khz",
	".adi,rx-nco-shifter-band-bnco1-freq_khz",
	".adi,rx-nco-shifter-band-bnco2-freq_khz",

	".adi,rx-gain-control-gain-mode",
	".adi,rx-gain-control-rx1-gain-index",
	".adi,rx-gain-control-rx2-gain-index",
	".adi,rx-gain-control-rx1-max-gain-index",
	".adi,rx-gain-control-rx1-min-gain-index",
	".adi,rx-gain-control-rx2-max-gain-index",
	".adi,rx-gain-control-rx2-min-gain-index",
	".adi,rx-settings-framer-sel",

	".adi,rx-settings-rx-channels",
	".adi,orx-profile-rx-fir-decimation",
	".adi,orx-profile-rx-dec5-decimation",
	".adi,orx-profile-rhb1-decimation",
	".adi,orx-profile-orx-output-rate_khz",
	".adi,orx-profile-rf-bandwidth_hz",
	".adi,orx-profile-rx-bbf3d-bcorner_khz",
	".adi,orx-profile-orx-ddc-mode",

	".adi,orx-gain-control-gain-mode",
	".adi,orx-gain-control-orx1-gain-index",
	".adi,orx-gain-control-orx2-gain-index",
	".adi,orx-gain-control-orx1-max-gain-index",
	".adi,orx-gain-control-orx1-min-gain-index",
	".adi,orx-gain-control-orx2-max-gain-index",
	".adi,orx-gain-control-orx2-min-gain-index",

	".adi,obs-settings-framer-sel",
	".adi,obs-settings-obs-rx-channels-enable",
	".adi,obs-settings-obs-rx-lo-source",

	".adi,tx-profile-dac-div",

	".adi,tx-profile-tx-fir-interpolation",
	".adi,tx-profile-thb1-interpolation",
	".adi,tx-profile-thb2-interpolation",
	".adi,tx-profile-thb3-interpolation",
	".adi,tx-profile-tx-int5-interpolation",
	".adi,tx-profile-tx-input-rate_khz",
	".adi,tx-profile-primary-sig-bandwidth_hz",
	".adi,tx-profile-rf-bandwidth_hz",
	".adi,tx-profile-tx-dac3d-bcorner_khz",
	".adi,tx-profile-tx-bbf3d-bcorner_khz",
	".adi,tx-settings-deframer-sel",
	".adi,tx-settings-tx-channels",
	".adi,tx-settings-tx-atten-step-size",
	".adi,tx-settings-tx1-atten_md-b",
	".adi,tx-settings-tx2-atten_md-b",
	".adi,tx-settings-dis-tx-data-if-pll-unlock",
	".adi,dig-clocks-device-clock_khz",
	".adi,dig-clocks-clk-pll-vco-freq_khz",
	".adi,dig-clocks-clk-pll-hs-div",
	".adi,dig-clocks-rf-pll-use-external-lo",
	".adi,dig-clocks-rf-pll-phase-sync-mode",

	".adi,default-initial-calibrations-mask",
};

static void reload_settings()
{
	struct osc_plugin *plugin;
	GSList *node;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;

		if (plugin && !strncmp(plugin->name, "adrv9009", 12)) { // TO DO: How do we know the first plugin we find is not this one?
			if (plugin->handle_external_request) {
				g_usleep(1 * G_USEC_PER_SEC);
				plugin->handle_external_request(plugin, "Reload Settings");
			}
		}
	}
}

static void signal_handler_cb(GtkWidget *widget, gpointer data)
{
	struct plugin_and_w_info *plugin_and_item = data;
	struct osc_plugin *plugin = plugin_and_item->plugin;
	struct w_info *item = plugin_and_item->item;
	long long val;
	char str[80];
	int bit, ret;
	long long mask;

	switch (item->type) {
	case CHECKBOX:
		val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		break;

	case BUTTON:
		val = 1;
		break;

	case SPINBUTTON:
		val = (long long) gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));

		if (item->lut_len) {
			iio_device_debug_attr_read_longlong(plugin->priv->dev, item->name, &mask);
			mask &= ~((1 << item->lut_len) - 1);
			mask |= val & ((1 << item->lut_len) - 1);
			val = mask;
		}

		break;

	case COMBOBOX:
		val = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
		val = item->lut[val];
		break;

	case CHECKBOX_MASK:

		/* Format is: adi,gpo-manual-mode-enable-mask#2
		 * # separates item name from bit number
		 */
		val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		ret = sscanf(item->name, "%[^'#']#%d", str, &bit);

		if (ret != 2)
			return;

		iio_device_debug_attr_read_longlong(plugin->priv->dev, str, &mask);

		if (val) {
			mask |= (1 << bit);
		} else {
			mask &= ~(1 << bit);
		}

		iio_device_debug_attr_write_longlong(plugin->priv->dev, str, mask);

		return;

	default:
		return;
	}

	iio_device_debug_attr_write_longlong(plugin->priv->dev, item->name, val);

	if (!strcmp(item->name, "initialize")) {
		reload_settings();
	}
}

static void bist_tone_cb(GtkWidget *widget, gpointer data)
{
	struct osc_plugin *plugin = data;
	unsigned enable, tx1_freq, tx2_freq;
	char temp[40];

	tx1_freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON(
	                gtk_builder_get_object(plugin->priv->builder, "tx1_nco_freq"))) * 1000;
	tx2_freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON(
	                gtk_builder_get_object(plugin->priv->builder, "tx2_nco_freq"))) * 1000;

	enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
	                GTK_WIDGET(gtk_builder_get_object(plugin->priv->builder, "tx_nco_enable"))));

	sprintf(temp, "%u %u %u", enable, tx1_freq, tx2_freq);

	iio_device_debug_attr_write(plugin->priv->dev, "bist_tone", "0 0 0");
	iio_device_debug_attr_write(plugin->priv->dev, "bist_tone", temp);
}

static char *set_widget_value(GtkWidget *widget, struct w_info *item, long long val)
{
	char str[80];
	int bit, ret, i;
	short val_s16;
	char val_s8;

	switch (item->type) {
	case CHECKBOX:
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), !!val);
		return "toggled";

	case BUTTON:
		return "clicked";

	case SPINBUTTON:
		if (item->lut_len) {
			val &= ((1 << item->lut_len) - 1);
		}

		gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), val);
		return "value-changed";

	case SPINBUTTON_S8:
		val_s8 = (char)val;
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), val_s8);
		return "value-changed";

	case SPINBUTTON_S16:
		val_s16 = (short)val;
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), val_s16);
		return "value-changed";

	case COMBOBOX:
		for (i = 0; i < item->lut_len; i++)
			if (val == item->lut[i])
				gtk_combo_box_set_active(GTK_COMBO_BOX(widget), i);

		return "changed";

	case CHECKBOX_MASK:
		ret = sscanf(item->name, "%[^'#']#%d", str, &bit);

		if (ret != 2)
			break;

		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), !!(val & (1 << bit)));
		return "toggled";

	}

	return NULL;
}

static void connect_widget_close_cb(gpointer data, GClosure *closure)
{
	g_free(data);
}

static void connect_widget(struct osc_plugin *plugin, struct w_info *item, long long val)
{
	char *signal;
	GtkWidget *widget;
	struct plugin_and_w_info *data_to_pass = g_new(struct plugin_and_w_info, 1);
	data_to_pass->plugin = plugin;
	data_to_pass->item = item;
	widget = GTK_WIDGET(gtk_builder_get_object(plugin->priv->builder, item->name));
	signal = set_widget_value(widget, item, val);
	g_builder_connect_signal_data(plugin->priv->builder, item->name, signal,
	                         G_CALLBACK(signal_handler_cb), data_to_pass,
				 connect_widget_close_cb, 0);
}

static void update_widget(GtkBuilder *builder, struct w_info *item, long long val)
{
	GtkWidget *widget;

	widget = GTK_WIDGET(gtk_builder_get_object(builder, item->name));
	set_widget_value(widget, item, val);
}

static int __connect_widget(struct iio_device *dev, const char *attr,
                            const char *value, size_t len, void *d)
{
	unsigned int i, nb_items = ARRAY_SIZE(attrs);
	struct osc_plugin *plugin = d;
	char str[80];
	int bit, ret;

	for (i = 0; i < nb_items; i++) {
		ret = sscanf(attrs[i].name, "%[^'#']#%d", str, &bit);

		if (!strcmp(str, attr)) {
			connect_widget(plugin, &attrs[i], atoll(value));

			if (ret == 1 && attrs[i].lut_len == 0) {
				return 0;

			}
		}
	}

	return 0;
}

static int __update_widget(struct iio_device *dev, const char *attr,
                           const char *value, size_t len, void *d)
{
	unsigned int i, nb_items = ARRAY_SIZE(attrs);
	GtkBuilder *builder = (GtkBuilder *) d;
	char str[80];
	int bit, ret;

	for (i = 0; i < nb_items; i++) {

		ret = sscanf(attrs[i].name, "%[^'#']#%d", str, &bit);

		if (!strcmp(str, attr)) {
			update_widget(builder, &attrs[i], atoll(value));

			if (ret == 1 && attrs[i].lut_len == 0) {
				return 0;

			}
		}
	}

	return 0;
}

static int connect_widgets(struct osc_plugin *plugin)
{
	return iio_device_debug_attr_read_all(plugin->priv->dev, __connect_widget, plugin);
}

static int update_widgets(struct osc_plugin *plugin)
{
	return iio_device_debug_attr_read_all(plugin->priv->dev, __update_widget, plugin->priv->builder);
}

static void change_page_cb(GtkNotebook *notebook, GtkNotebookTab *page,
                           guint page_num, gpointer user_data)
{
	GtkWidget *tohide = user_data;

	if (page_num == 15)
		gtk_widget_hide(tohide); /* Hide Init button in BIST Tab */
	else
		gtk_widget_show(tohide);
}

static int handle_external_request(struct osc_plugin *plugin, const char *request)
{
	int ret = 0;

	if (!strcmp(request, "Trigger MCS")) {
		GtkWidget *mcs_btn;

		mcs_btn = GTK_WIDGET(gtk_builder_get_object(plugin->priv->builder, "mcs_sync"));
		g_signal_emit_by_name(mcs_btn, "clicked", NULL);
		ret = 1;
	} else if (!strcmp(request, "RELOAD")) {
		if (plugin && plugin->priv->can_update_widgets)
			update_widgets(plugin);
	}

	return ret;
}

static int adrv9009adv_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	int ret = 0;

	if (MATCH_ATTRIB("SYNC_RELOAD") && atoi(value)) {
		if (plugin->priv->can_update_widgets)
			update_widgets(plugin);

		reload_settings();
	} else {
		fprintf(stderr, "Unknown token in ini file; key:'%s' value:'%s'\n",
		        attrib, value);
		return -EINVAL;
	}

	return ret;
}

static int adrv9009adv_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(plugin->priv->ctx, line, attrib, value,
		adrv9009adv_handle_driver, plugin);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{

	update_from_ini(ini_fn, plugin->name, plugin->priv->dev,
	        (const char * const*)plugin->priv->sr_attribs, plugin->priv->sr_attribs_count);

	if (plugin->priv->can_update_widgets)
		update_widgets(plugin);

}

static GtkWidget *adrv9009adv_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	struct plugin_private *priv = plugin->priv;
	GtkWidget *adrv9009adv_panel;

	struct iio_context *ctx = osc_create_context();

	if (!ctx)
		return NULL;

	/* get_data_for_possible_plugin_instances() is responsibile to set the first device name to be of type adrv9009-phy */
	const char *dev_name = g_list_first(priv->plugin_ctx.required_devices)->data;
	struct iio_device *dev = iio_context_find_device(ctx, dev_name);

	/* build the list of save/restore attributes */
	plugin->priv->sr_attribs_count = ARRAY_SIZE(adrv9009_adv_sr_attribs);
	plugin->priv->sr_attribs = g_new(char *, plugin->priv->sr_attribs_count);
	size_t n = 0;
	for (; n < plugin->priv->sr_attribs_count; n++) {
		plugin->priv->sr_attribs[n] = g_strconcat(
			"debug.", dev_name, adrv9009_adv_sr_attribs[n], NULL);
	}

	if (ini_fn)
		load_profile(plugin, ini_fn);

	GtkBuilder *builder = gtk_builder_new();

	if (osc_load_glade_file(builder, "adrv9009_adv") < 0)
		return NULL;

	/* At this point the function cannot fail - initialize priv */
	priv->ctx = ctx;
	priv->dev = dev;
	priv->builder = builder;

	adrv9009adv_panel = GTK_WIDGET(gtk_builder_get_object(builder, "adrv9009adv_panel"));
	priv->nbook = GTK_NOTEBOOK(gtk_builder_get_object(builder, "notebook"));

	connect_widgets(plugin);

	g_builder_connect_signal(builder, "tx1_nco_freq", "value-changed",
	                         G_CALLBACK(bist_tone_cb), plugin);

	g_builder_connect_signal(builder, "tx2_nco_freq", "value-changed",
	                         G_CALLBACK(bist_tone_cb), plugin);

	g_builder_connect_signal(builder, "tx_nco_enable", "toggled",
	                         G_CALLBACK(bist_tone_cb), plugin);

	g_builder_connect_signal(builder, "notebook", "switch-page",
	                         G_CALLBACK(change_page_cb),
	                         GTK_WIDGET(gtk_builder_get_object(builder, "initialize")));

	priv->can_update_widgets = true;

	return adrv9009adv_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	plugin->priv->this_page = active_page;
	plugin->priv->plugin_detached = is_detached;
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");

	if (f) {
		save_to_ini(f, plugin->name, plugin->priv->dev,
			(const char * const*)plugin->priv->sr_attribs,
			plugin->priv->sr_attribs_count);
		fclose(f);
	}
}

gpointer copy_gchar_array(gconstpointer src, gpointer data)
{
	return (gpointer)g_strdup(src);
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	save_profile(plugin, ini_fn);

	size_t n = 0;
	for (; n < plugin->priv->sr_attribs_count; n++) {
		g_free(plugin->priv->sr_attribs[n]);
	}
	g_free(plugin->priv->sr_attribs);

	osc_plugin_context_free_resources(&plugin->priv->plugin_ctx);
	
	osc_destroy_context(plugin->priv->ctx);
	
	g_free(plugin->priv);
}

struct osc_plugin * create_plugin(struct osc_plugin_context *plugin_ctx)
{
	struct osc_plugin *plugin= g_new0(struct osc_plugin, 1);

	if (!plugin_ctx ) {
		fprintf(stderr, "Cannot create plugin because an invalid plugin context was provided\n");
		return NULL;
	}

	plugin->priv = g_new0(struct plugin_private, 1);
	plugin->priv->plugin_ctx.plugin_name = g_strdup(plugin_ctx->plugin_name);
	plugin->priv->plugin_ctx.required_devices = g_list_copy_deep(plugin_ctx->required_devices, (GCopyFunc)copy_gchar_array, NULL);

	plugin->name = plugin->priv->plugin_ctx.plugin_name;
	plugin->dynamically_created = TRUE;
	plugin->init = adrv9009adv_init;
	plugin->handle_item = adrv9009adv_handle;
	plugin->handle_external_request = handle_external_request;
	plugin->update_active_page = update_active_page;
	plugin->save_profile = save_profile;
	plugin->load_profile = load_profile;
	plugin->destroy = context_destroy;

	return plugin;
}

/* Informs how many plugins can be instantiated and gives context for each possible plugin instance */
GArray* get_data_for_possible_plugin_instances(void)
{
	GArray *data = g_array_new(FALSE, TRUE, sizeof(struct osc_plugin_context *));
	struct iio_context *osc_ctx = get_context_from_osc();
	GArray *devices = get_iio_devices_starting_with(osc_ctx, PHY_DEVICE);
	guint i = 0;

	for (; i < devices->len; i++) {
		struct osc_plugin_context *context = g_new(struct osc_plugin_context, 1);
		struct iio_device *dev = g_array_index(devices, struct iio_device*, i);

		/* Construct the name of the plugin */
		char *name;
		if (devices->len > 1)
			name = g_strdup_printf("%s-%i", THIS_DRIVER, i);
		else
			name = g_strdup(THIS_DRIVER);

		context->required_devices = NULL;
		context->required_devices = g_list_append(context->required_devices, g_strdup(iio_device_get_name(dev)));
		context->plugin_name = name;
		g_array_append_val(data, context);
	}

	g_array_free(devices, FALSE);

	return data;
}
