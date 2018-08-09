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
#include <malloc.h>
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

#define PHY_DEVICE "adrv9009-phy"
#define DDS_DEVICE "axi-adrv9009-tx-hpc"
#define CAP_DEVICE "axi-adrv9009-rx-hpc"
#define THIS_DRIVER "ADRV9009 Advanced"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

static struct iio_context *ctx;
static struct iio_device *dev;
static bool can_update_widgets;
static gint this_page;
static GtkNotebook *nbook;
static gboolean plugin_detached;
static GtkBuilder *builder;

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
	"debug.adrv9009-phy.adi,rxagc-peak-agc-under-range-low-interval_ns",
	"debug.adrv9009-phy.adi,rxagc-peak-agc-under-range-mid-interval",
	"debug.adrv9009-phy.adi,rxagc-peak-agc-under-range-high-interval",
	"debug.adrv9009-phy.adi,rxagc-peak-apd-high-thresh",
	"debug.adrv9009-phy.adi,rxagc-peak-apd-low-gain-mode-high-thresh",
	"debug.adrv9009-phy.adi,rxagc-peak-apd-low-thresh",
	"debug.adrv9009-phy.adi,rxagc-peak-apd-low-gain-mode-low-thresh",
	"debug.adrv9009-phy.adi,rxagc-peak-apd-upper-thresh-peak-exceeded-cnt",
	"debug.adrv9009-phy.adi,rxagc-peak-apd-lower-thresh-peak-exceeded-cnt",
	"debug.adrv9009-phy.adi,rxagc-peak-apd-gain-step-attack",
	"debug.adrv9009-phy.adi,rxagc-peak-apd-gain-step-recovery",
	"debug.adrv9009-phy.adi,rxagc-peak-enable-hb2-overload",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-overload-duration-cnt",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-overload-thresh-cnt",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-high-thresh",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-under-range-low-thresh",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-under-range-mid-thresh",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-under-range-high-thresh",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-upper-thresh-peak-exceeded-cnt",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-lower-thresh-peak-exceeded-cnt",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-gain-step-high-recovery",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-gain-step-low-recovery",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-gain-step-mid-recovery",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-gain-step-attack",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-overload-power-mode",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-ovrg-sel",
	"debug.adrv9009-phy.adi,rxagc-peak-hb2-thresh-config",

	"debug.adrv9009-phy.adi,rxagc-power-power-enable-measurement",
	"debug.adrv9009-phy.adi,rxagc-power-power-use-rfir-out",
	"debug.adrv9009-phy.adi,rxagc-power-power-use-bbdc2",
	"debug.adrv9009-phy.adi,rxagc-power-under-range-high-power-thresh",
	"debug.adrv9009-phy.adi,rxagc-power-under-range-low-power-thresh",
	"debug.adrv9009-phy.adi,rxagc-power-under-range-high-power-gain-step-recovery",
	"debug.adrv9009-phy.adi,rxagc-power-under-range-low-power-gain-step-recovery",
	"debug.adrv9009-phy.adi,rxagc-power-power-measurement-duration",
	"debug.adrv9009-phy.adi,rxagc-power-rx1-tdd-power-meas-duration",
	"debug.adrv9009-phy.adi,rxagc-power-rx1-tdd-power-meas-delay",
	"debug.adrv9009-phy.adi,rxagc-power-rx2-tdd-power-meas-duration",
	"debug.adrv9009-phy.adi,rxagc-power-rx2-tdd-power-meas-delay",
	"debug.adrv9009-phy.adi,rxagc-power-upper0-power-thresh",
	"debug.adrv9009-phy.adi,rxagc-power-upper1-power-thresh",
	"debug.adrv9009-phy.adi,rxagc-power-power-log-shift",

	"debug.adrv9009-phy.adi,rxagc-agc-peak-wait-time",
	"debug.adrv9009-phy.adi,rxagc-agc-rx1-max-gain-index",
	"debug.adrv9009-phy.adi,rxagc-agc-rx1-min-gain-index",
	"debug.adrv9009-phy.adi,rxagc-agc-rx2-max-gain-index",
	"debug.adrv9009-phy.adi,rxagc-agc-rx2-min-gain-index",
	"debug.adrv9009-phy.adi,rxagc-agc-gain-update-counter_us",
	"debug.adrv9009-phy.adi,rxagc-agc-rx1-attack-delay",
	"debug.adrv9009-phy.adi,rxagc-agc-rx2-attack-delay",
	"debug.adrv9009-phy.adi,rxagc-agc-slow-loop-settling-delay",
	"debug.adrv9009-phy.adi,rxagc-agc-low-thresh-prevent-gain",
	"debug.adrv9009-phy.adi,rxagc-agc-change-gain-if-thresh-high",
	"debug.adrv9009-phy.adi,rxagc-agc-peak-thresh-gain-control-mode",
	"debug.adrv9009-phy.adi,rxagc-agc-reset-on-rxon",
	"debug.adrv9009-phy.adi,rxagc-agc-enable-sync-pulse-for-gain-counter",
	"debug.adrv9009-phy.adi,rxagc-agc-enable-ip3-optimization-thresh",
	"debug.adrv9009-phy.adi,rxagc-ip3-over-range-thresh",
	"debug.adrv9009-phy.adi,rxagc-ip3-over-range-thresh-index",
	"debug.adrv9009-phy.adi,rxagc-ip3-peak-exceeded-cnt",
	"debug.adrv9009-phy.adi,rxagc-agc-enable-fast-recovery-loop",

	"debug.adrv9009-phy.adi,aux-dac-enables",
	"debug.adrv9009-phy.adi,aux-dac-vref0",
	"debug.adrv9009-phy.adi,aux-dac-resolution0",
	"debug.adrv9009-phy.adi,aux-dac-values0",
	"debug.adrv9009-phy.adi,aux-dac-vref1",
	"debug.adrv9009-phy.adi,aux-dac-resolution1",
	"debug.adrv9009-phy.adi,aux-dac-values1",
	"debug.adrv9009-phy.adi,aux-dac-vref2",
	"debug.adrv9009-phy.adi,aux-dac-resolution2",
	"debug.adrv9009-phy.adi,aux-dac-values2",
	"debug.adrv9009-phy.adi,aux-dac-vref3",
	"debug.adrv9009-phy.adi,aux-dac-resolution3",
	"debug.adrv9009-phy.adi,aux-dac-values3",
	"debug.adrv9009-phy.adi,aux-dac-vref4",
	"debug.adrv9009-phy.adi,aux-dac-resolution4",
	"debug.adrv9009-phy.adi,aux-dac-values4",
	"debug.adrv9009-phy.adi,aux-dac-vref5",
	"debug.adrv9009-phy.adi,aux-dac-resolution5",
	"debug.adrv9009-phy.adi,aux-dac-values5",
	"debug.adrv9009-phy.adi,aux-dac-vref6",
	"debug.adrv9009-phy.adi,aux-dac-resolution6",
	"debug.adrv9009-phy.adi,aux-dac-values6",
	"debug.adrv9009-phy.adi,aux-dac-vref7",
	"debug.adrv9009-phy.adi,aux-dac-resolution7",
	"debug.adrv9009-phy.adi,aux-dac-values7",
	"debug.adrv9009-phy.adi,aux-dac-vref8",
	"debug.adrv9009-phy.adi,aux-dac-resolution8",
	"debug.adrv9009-phy.adi,aux-dac-values8",
	"debug.adrv9009-phy.adi,aux-dac-vref9",
	"debug.adrv9009-phy.adi,aux-dac-resolution9",
	"debug.adrv9009-phy.adi,aux-dac-values9",
	"debug.adrv9009-phy.adi,aux-dac-values10",
	"debug.adrv9009-phy.adi,aux-dac-values11",
	"debug.adrv9009-phy.adi,jesd204-framer-a-bank-id",
	"debug.adrv9009-phy.adi,jesd204-framer-a-device-id",
	"debug.adrv9009-phy.adi,jesd204-framer-a-lane0-id",
	"debug.adrv9009-phy.adi,jesd204-framer-a-m",
	"debug.adrv9009-phy.adi,jesd204-framer-a-k",
	"debug.adrv9009-phy.adi,jesd204-framer-a-f",
	"debug.adrv9009-phy.adi,jesd204-framer-a-np",
	"debug.adrv9009-phy.adi,jesd204-framer-a-scramble",
	"debug.adrv9009-phy.adi,jesd204-framer-a-external-sysref",
	"debug.adrv9009-phy.adi,jesd204-framer-a-serializer-lanes-enabled",
	"debug.adrv9009-phy.adi,jesd204-framer-a-serializer-lane-crossbar",
	"debug.adrv9009-phy.adi,jesd204-framer-a-lmfc-offset",
	"debug.adrv9009-phy.adi,jesd204-framer-a-new-sysref-on-relink",
	"debug.adrv9009-phy.adi,jesd204-framer-a-syncb-in-select",
	"debug.adrv9009-phy.adi,jesd204-framer-a-over-sample",
	"debug.adrv9009-phy.adi,jesd204-framer-a-syncb-in-lvds-mode",
	"debug.adrv9009-phy.adi,jesd204-framer-a-syncb-in-lvds-pn-invert",
	"debug.adrv9009-phy.adi,jesd204-framer-a-enable-manual-lane-xbar",
	"debug.adrv9009-phy.adi,jesd204-framer-b-bank-id",
	"debug.adrv9009-phy.adi,jesd204-framer-b-device-id",
	"debug.adrv9009-phy.adi,jesd204-framer-b-lane0-id",
	"debug.adrv9009-phy.adi,jesd204-framer-b-m",
	"debug.adrv9009-phy.adi,jesd204-framer-b-k",
	"debug.adrv9009-phy.adi,jesd204-framer-b-f",
	"debug.adrv9009-phy.adi,jesd204-framer-b-np",
	"debug.adrv9009-phy.adi,jesd204-framer-b-scramble",
	"debug.adrv9009-phy.adi,jesd204-framer-b-external-sysref",
	"debug.adrv9009-phy.adi,jesd204-framer-b-serializer-lanes-enabled",
	"debug.adrv9009-phy.adi,jesd204-framer-b-serializer-lane-crossbar",
	"debug.adrv9009-phy.adi,jesd204-framer-b-lmfc-offset",
	"debug.adrv9009-phy.adi,jesd204-framer-b-new-sysref-on-relink",
	"debug.adrv9009-phy.adi,jesd204-framer-b-syncb-in-select",
	"debug.adrv9009-phy.adi,jesd204-framer-b-over-sample",
	"debug.adrv9009-phy.adi,jesd204-framer-b-syncb-in-lvds-mode",
	"debug.adrv9009-phy.adi,jesd204-framer-b-syncb-in-lvds-pn-invert",
	"debug.adrv9009-phy.adi,jesd204-framer-b-enable-manual-lane-xbar",

	"debug.adrv9009-phy.adi,jesd204-deframer-a-bank-id",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-device-id",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-lane0-id",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-m",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-k",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-scramble",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-external-sysref",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-deserializer-lanes-enabled",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-deserializer-lane-crossbar",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-lmfc-offset",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-new-sysref-on-relink",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-syncb-out-select",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-np",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-syncb-out-lvds-mode",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-syncb-out-lvds-pn-invert",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-syncb-out-cmos-slew-rate",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-syncb-out-cmos-drive-level",
	"debug.adrv9009-phy.adi,jesd204-deframer-a-enable-manual-lane-xbar",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-bank-id",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-device-id",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-lane0-id",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-m",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-k",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-scramble",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-external-sysref",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-deserializer-lanes-enabled",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-deserializer-lane-crossbar",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-lmfc-offset",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-new-sysref-on-relink",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-syncb-out-select",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-np",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-syncb-out-lvds-mode",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-syncb-out-lvds-pn-invert",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-syncb-out-cmos-slew-rate",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-syncb-out-cmos-drive-level",
	"debug.adrv9009-phy.adi,jesd204-deframer-b-enable-manual-lane-xbar",

	"debug.adrv9009-phy.adi,jesd204-ser-amplitude",
	"debug.adrv9009-phy.adi,jesd204-ser-pre-emphasis",
	"debug.adrv9009-phy.adi,jesd204-ser-invert-lane-polarity",
	"debug.adrv9009-phy.adi,jesd204-des-invert-lane-polarity",
	"debug.adrv9009-phy.adi,jesd204-des-eq-setting",
	"debug.adrv9009-phy.adi,jesd204-sysref-lvds-mode",
	"debug.adrv9009-phy.adi,jesd204-sysref-lvds-pn-invert",

	"debug.adrv9009-phy.adi,arm-gpio-config-orx1-tx-sel0-pin-gpio-pin-sel",
	"debug.adrv9009-phy.adi,arm-gpio-config-orx1-tx-sel0-pin-polarity",
	"debug.adrv9009-phy.adi,arm-gpio-config-orx1-tx-sel0-pin-enable",

	"debug.adrv9009-phy.adi,arm-gpio-config-orx1-tx-sel1-pin-gpio-pin-sel",
	"debug.adrv9009-phy.adi,arm-gpio-config-orx1-tx-sel1-pin-polarity",
	"debug.adrv9009-phy.adi,arm-gpio-config-orx1-tx-sel1-pin-enable",
	"debug.adrv9009-phy.adi,arm-gpio-config-orx2-tx-sel0-pin-gpio-pin-sel",
	"debug.adrv9009-phy.adi,arm-gpio-config-orx2-tx-sel0-pin-polarity",
	"debug.adrv9009-phy.adi,arm-gpio-config-orx2-tx-sel0-pin-enable",

	"debug.adrv9009-phy.adi,arm-gpio-config-orx2-tx-sel1-pin-gpio-pin-sel",
	"debug.adrv9009-phy.adi,arm-gpio-config-orx2-tx-sel1-pin-polarity",
	"debug.adrv9009-phy.adi,arm-gpio-config-orx2-tx-sel1-pin-enable",
	"debug.adrv9009-phy.adi,arm-gpio-config-en-tx-tracking-cals-gpio-pin-sel",
	"debug.adrv9009-phy.adi,arm-gpio-config-en-tx-tracking-cals-polarity",
	"debug.adrv9009-phy.adi,arm-gpio-config-en-tx-tracking-cals-enable",
	"debug.adrv9009-phy.adi,orx-lo-cfg-disable-aux-pll-relocking",
	"debug.adrv9009-phy.adi,orx-lo-cfg-gpio-select",

	"debug.adrv9009-phy.adi,fhm-config-fhm-gpio-pin",
	"debug.adrv9009-phy.adi,fhm-config-fhm-min-freq_mhz",
	"debug.adrv9009-phy.adi,fhm-config-fhm-max-freq_mhz",
	"debug.adrv9009-phy.adi,fhm-mode-fhm-enable",
	"debug.adrv9009-phy.adi,fhm-mode-enable-mcs-sync",
	"debug.adrv9009-phy.adi,fhm-mode-fhm-trigger-mode",
	"debug.adrv9009-phy.adi,fhm-mode-fhm-exit-mode",
	"debug.adrv9009-phy.adi,fhm-mode-fhm-init-frequency_hz",

	"debug.adrv9009-phy.adi,rx1-gain-ctrl-pin-inc-step",
	"debug.adrv9009-phy.adi,rx1-gain-ctrl-pin-dec-step",
	"debug.adrv9009-phy.adi,rx1-gain-ctrl-pin-rx-gain-inc-pin",
	"debug.adrv9009-phy.adi,rx1-gain-ctrl-pin-rx-gain-dec-pin",
	"debug.adrv9009-phy.adi,rx1-gain-ctrl-pin-enable",
	"debug.adrv9009-phy.adi,rx2-gain-ctrl-pin-inc-step",
	"debug.adrv9009-phy.adi,rx2-gain-ctrl-pin-dec-step",
	"debug.adrv9009-phy.adi,rx2-gain-ctrl-pin-rx-gain-inc-pin",
	"debug.adrv9009-phy.adi,rx2-gain-ctrl-pin-rx-gain-dec-pin",
	"debug.adrv9009-phy.adi,rx2-gain-ctrl-pin-enable",

	"debug.adrv9009-phy.adi,tx1-atten-ctrl-pin-step-size",
	"debug.adrv9009-phy.adi,tx1-atten-ctrl-pin-tx-atten-inc-pin",
	"debug.adrv9009-phy.adi,tx1-atten-ctrl-pin-tx-atten-dec-pin",
	"debug.adrv9009-phy.adi,tx1-atten-ctrl-pin-enable",
	"debug.adrv9009-phy.adi,tx2-atten-ctrl-pin-step-size",
	"debug.adrv9009-phy.adi,tx2-atten-ctrl-pin-tx-atten-inc-pin",
	"debug.adrv9009-phy.adi,tx2-atten-ctrl-pin-tx-atten-dec-pin",
	"debug.adrv9009-phy.adi,tx2-atten-ctrl-pin-enable",

	"debug.adrv9009-phy.adi,tx-pa-protection-avg-duration",
	"debug.adrv9009-phy.adi,tx-pa-protection-tx-atten-step",
	"debug.adrv9009-phy.adi,tx-pa-protection-tx1-power-threshold",
	"debug.adrv9009-phy.adi,tx-pa-protection-tx2-power-threshold",
	"debug.adrv9009-phy.adi,tx-pa-protection-peak-count",
	"debug.adrv9009-phy.adi,tx-pa-protection-tx1-peak-threshold",
	"debug.adrv9009-phy.adi,tx-pa-protection-tx2-peak-threshold",

	"debug.adrv9009-phy.adi,rx-profile-rx-fir-decimation",
	"debug.adrv9009-phy.adi,rx-profile-rx-dec5-decimation",
	"debug.adrv9009-phy.adi,rx-profile-rhb1-decimation",
	"debug.adrv9009-phy.adi,rx-profile-rx-output-rate_khz",
	"debug.adrv9009-phy.adi,rx-profile-rf-bandwidth_hz",
	"debug.adrv9009-phy.adi,rx-profile-rx-bbf3d-bcorner_khz",

	"debug.adrv9009-phy.adi,rx-profile-rx-ddc-mode",

	"debug.adrv9009-phy.adi,rx-nco-shifter-band-a-input-band-width_khz",
	"debug.adrv9009-phy.adi,rx-nco-shifter-band-a-input-center-freq_khz",
	"debug.adrv9009-phy.adi,rx-nco-shifter-band-a-nco1-freq_khz",
	"debug.adrv9009-phy.adi,rx-nco-shifter-band-a-nco2-freq_khz",
	"debug.adrv9009-phy.adi,rx-nco-shifter-band-binput-band-width_khz",
	"debug.adrv9009-phy.adi,rx-nco-shifter-band-binput-center-freq_khz",
	"debug.adrv9009-phy.adi,rx-nco-shifter-band-bnco1-freq_khz",
	"debug.adrv9009-phy.adi,rx-nco-shifter-band-bnco2-freq_khz",

	"debug.adrv9009-phy.adi,rx-gain-control-gain-mode",
	"debug.adrv9009-phy.adi,rx-gain-control-rx1-gain-index",
	"debug.adrv9009-phy.adi,rx-gain-control-rx2-gain-index",
	"debug.adrv9009-phy.adi,rx-gain-control-rx1-max-gain-index",
	"debug.adrv9009-phy.adi,rx-gain-control-rx1-min-gain-index",
	"debug.adrv9009-phy.adi,rx-gain-control-rx2-max-gain-index",
	"debug.adrv9009-phy.adi,rx-gain-control-rx2-min-gain-index",
	"debug.adrv9009-phy.adi,rx-settings-framer-sel",

	"debug.adrv9009-phy.adi,rx-settings-rx-channels",
	"debug.adrv9009-phy.adi,orx-profile-rx-fir-decimation",
	"debug.adrv9009-phy.adi,orx-profile-rx-dec5-decimation",
	"debug.adrv9009-phy.adi,orx-profile-rhb1-decimation",
	"debug.adrv9009-phy.adi,orx-profile-orx-output-rate_khz",
	"debug.adrv9009-phy.adi,orx-profile-rf-bandwidth_hz",
	"debug.adrv9009-phy.adi,orx-profile-rx-bbf3d-bcorner_khz",
	"debug.adrv9009-phy.adi,orx-profile-orx-ddc-mode",

	"debug.adrv9009-phy.adi,orx-gain-control-gain-mode",
	"debug.adrv9009-phy.adi,orx-gain-control-orx1-gain-index",
	"debug.adrv9009-phy.adi,orx-gain-control-orx2-gain-index",
	"debug.adrv9009-phy.adi,orx-gain-control-orx1-max-gain-index",
	"debug.adrv9009-phy.adi,orx-gain-control-orx1-min-gain-index",
	"debug.adrv9009-phy.adi,orx-gain-control-orx2-max-gain-index",
	"debug.adrv9009-phy.adi,orx-gain-control-orx2-min-gain-index",

	"debug.adrv9009-phy.adi,obs-settings-framer-sel",
	"debug.adrv9009-phy.adi,obs-settings-obs-rx-channels-enable",
	"debug.adrv9009-phy.adi,obs-settings-obs-rx-lo-source",

	"debug.adrv9009-phy.adi,tx-profile-dac-div",

	"debug.adrv9009-phy.adi,tx-profile-tx-fir-interpolation",
	"debug.adrv9009-phy.adi,tx-profile-thb1-interpolation",
	"debug.adrv9009-phy.adi,tx-profile-thb2-interpolation",
	"debug.adrv9009-phy.adi,tx-profile-thb3-interpolation",
	"debug.adrv9009-phy.adi,tx-profile-tx-int5-interpolation",
	"debug.adrv9009-phy.adi,tx-profile-tx-input-rate_khz",
	"debug.adrv9009-phy.adi,tx-profile-primary-sig-bandwidth_hz",
	"debug.adrv9009-phy.adi,tx-profile-rf-bandwidth_hz",
	"debug.adrv9009-phy.adi,tx-profile-tx-dac3d-bcorner_khz",
	"debug.adrv9009-phy.adi,tx-profile-tx-bbf3d-bcorner_khz",
	"debug.adrv9009-phy.adi,tx-settings-deframer-sel",
	"debug.adrv9009-phy.adi,tx-settings-tx-channels",
	"debug.adrv9009-phy.adi,tx-settings-tx-atten-step-size",
	"debug.adrv9009-phy.adi,tx-settings-tx1-atten_md-b",
	"debug.adrv9009-phy.adi,tx-settings-tx2-atten_md-b",
	"debug.adrv9009-phy.adi,tx-settings-dis-tx-data-if-pll-unlock",
	"debug.adrv9009-phy.adi,dig-clocks-device-clock_khz",
	"debug.adrv9009-phy.adi,dig-clocks-clk-pll-vco-freq_khz",
	"debug.adrv9009-phy.adi,dig-clocks-clk-pll-hs-div",
	"debug.adrv9009-phy.adi,dig-clocks-rf-pll-use-external-lo",
	"debug.adrv9009-phy.adi,dig-clocks-rf-pll-phase-sync-mode",

	"debug.adrv9009-phy.adi,default-initial-calibrations-mask",
};

static void reload_settings(void)
{
	struct osc_plugin *plugin;
	GSList *node;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;

		if (plugin && !strncmp(plugin->name, "adrv9009", 12)) {
			if (plugin->handle_external_request) {
				g_usleep(1 * G_USEC_PER_SEC);
				plugin->handle_external_request("Reload Settings");
			}
		}
	}
}

static void signal_handler_cb(GtkWidget *widget, gpointer data)
{
	struct w_info *item = data;
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
			iio_device_debug_attr_read_longlong(dev, item->name, &mask);
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

		iio_device_debug_attr_read_longlong(dev, str, &mask);

		if (val) {
			mask |= (1 << bit);
		} else {
			mask &= ~(1 << bit);
		}

		iio_device_debug_attr_write_longlong(dev, str, mask);

		return;

	default:
		return;
	}

	iio_device_debug_attr_write_longlong(dev, item->name, val);

	if (!strcmp(item->name, "initialize")) {
		reload_settings();
	}
}

static void bist_tone_cb(GtkWidget *widget, gpointer data)
{
	GtkBuilder *builder = data;
	unsigned enable, tx1_freq, tx2_freq;
	char temp[40];

	tx1_freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON(
	                gtk_builder_get_object(builder, "tx1_nco_freq"))) * 1000;
	tx2_freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON(
	                gtk_builder_get_object(builder, "tx2_nco_freq"))) * 1000;

	enable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
	                GTK_WIDGET(gtk_builder_get_object(builder, "tx_nco_enable"))));

	sprintf(temp, "%d %d %d", enable, tx1_freq, tx2_freq);

	iio_device_debug_attr_write(dev, "bist_tone", "0 0 0");
	iio_device_debug_attr_write(dev, "bist_tone", temp);
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
static void connect_widget(GtkBuilder *builder, struct w_info *item, long long val)
{
	char *signal = NULL;
	GtkWidget *widget;
	widget = GTK_WIDGET(gtk_builder_get_object(builder, item->name));
	signal = set_widget_value(widget, item, val);
	g_builder_connect_signal(builder, item->name, signal,
	                         G_CALLBACK(signal_handler_cb), item);
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
	GtkBuilder *builder = (GtkBuilder *) d;
	char str[80];
	int bit, ret;

	for (i = 0; i < nb_items; i++) {
		ret = sscanf(attrs[i].name, "%[^'#']#%d", str, &bit);

		if (!strcmp(str, attr)) {
			connect_widget(builder, &attrs[i], atoll(value));

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

static int connect_widgets(GtkBuilder *builder)
{
	return iio_device_debug_attr_read_all(dev, __connect_widget, builder);
}

static int update_widgets(GtkBuilder *builder)
{
	return iio_device_debug_attr_read_all(dev, __update_widget, builder);
}

static void change_page_cb(GtkNotebook *notebook, GtkNotebookPage *page,
                           guint page_num, gpointer user_data)
{
	GtkWidget *tohide = user_data;

	if (page_num == 15)
		gtk_widget_hide(tohide); /* Hide Init button in BIST Tab */
	else
		gtk_widget_show(tohide);
}

static int handle_external_request(const char *request)
{
	int ret = 0;

	if (!strcmp(request, "Trigger MCS")) {
		GtkWidget *mcs_btn;

		mcs_btn = GTK_WIDGET(gtk_builder_get_object(builder, "mcs_sync"));
		g_signal_emit_by_name(mcs_btn, "clicked", NULL);
		ret = 1;
	} else if (!strcmp(request, "RELOAD")) {
		if (can_update_widgets)
			update_widgets(builder);
	}

	return ret;
}

static int adrv9009adv_handle_driver(const char *attrib, const char *value)
{
	int ret = 0;

	if (MATCH_ATTRIB("SYNC_RELOAD") && atoi(value)) {
		if (can_update_widgets)
			update_widgets(builder);

		reload_settings();
	} else {
		fprintf(stderr, "Unknown token in ini file; key:'%s' value:'%s'\n",
		        attrib, value);
		return -EINVAL;
	}

	return ret;
}

static int adrv9009adv_handle(int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
	                                 adrv9009adv_handle_driver);
}

static void load_profile(const char *ini_fn)
{

	update_from_ini(ini_fn, THIS_DRIVER, dev,
	                adrv9009_adv_sr_attribs,
	                ARRAY_SIZE(adrv9009_adv_sr_attribs));

	if (can_update_widgets)
		update_widgets(builder);

}

static GtkWidget *adrv9009adv_init(GtkWidget *notebook, const char *ini_fn)
{
	GtkWidget *adrv9009adv_panel;

	ctx = osc_create_context();

	if (!ctx)
		return NULL;

	dev = iio_context_find_device(ctx, PHY_DEVICE);

	if (ini_fn)
		load_profile(ini_fn);

	builder = gtk_builder_new();

	if (osc_load_glade_file(builder, "adrv9009_adv") < 0)
		return NULL;

	adrv9009adv_panel = GTK_WIDGET(gtk_builder_get_object(builder, "adrv9009adv_panel"));
	nbook = GTK_NOTEBOOK(gtk_builder_get_object(builder, "notebook"));

	connect_widgets(builder);

	g_builder_connect_signal(builder, "tx1_nco_freq", "value-changed",
	                         G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "tx2_nco_freq", "value-changed",
	                         G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "tx_nco_enable", "toggled",
	                         G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "notebook", "switch-page",
	                         G_CALLBACK(change_page_cb),
	                         GTK_WIDGET(gtk_builder_get_object(builder, "initialize")));

	can_update_widgets = true;

	return adrv9009adv_panel;
}

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void save_profile(const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");

	if (f) {
		save_to_ini(f, THIS_DRIVER, dev, adrv9009_adv_sr_attribs,
		            ARRAY_SIZE(adrv9009_adv_sr_attribs));
		fclose(f);
	}
}

static void context_destroy(const char *ini_fn)
{
	save_profile(ini_fn);
	osc_destroy_context(ctx);
}

static bool adrv9009adv_identify(void)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	dev = iio_context_find_device(osc_ctx, PHY_DEVICE);

	return !!dev && iio_device_get_debug_attrs_count(dev);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = adrv9009adv_identify,
	.init = adrv9009adv_init,
	.handle_item = adrv9009adv_handle,
	.handle_external_request = handle_external_request,
	.update_active_page = update_active_page,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
