/**
 * Copyright (C) 2016-2017 Analog Devices, Inc.
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


#define PHY_DEVICE "ad9371-phy"
#define DDS_DEVICE "axi-ad9371-tx-hpc"
#define CAP_DEVICE "axi-ad9371-rx-hpc"
#define THIS_DRIVER "AD9371 Advanced"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

static struct iio_context *ctx;
static struct iio_device *dev;

OscPlot *plot_xcorr_4ch;

static bool can_update_widgets;

static gint this_page;
static GtkNotebook *nbook;
static gboolean plugin_detached;
static GtkBuilder *builder;

enum ad9371adv_wtype {
	CHECKBOX,
	SPINBUTTON,
	COMBOBOX,
	BUTTON,
	CHECKBOX_MASK,
	SPINBUTTON_S8,
	SPINBUTTON_S16,
};

struct w_info {
	enum ad9371adv_wtype type;
	const char * const name;
	const unsigned char * const lut;
	const unsigned char lut_len;
};

static struct w_info attrs[] = {
	{SPINBUTTON, "adi,jesd204-rx-framer-bank-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-rx-framer-device-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-rx-framer-lane0-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-rx-framer-m", NULL, 0},
	{SPINBUTTON, "adi,jesd204-rx-framer-k", NULL, 0},
	{CHECKBOX, "adi,jesd204-rx-framer-scramble", NULL, 0},
	{CHECKBOX, "adi,jesd204-rx-framer-external-sysref", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-rx-framer-serializer-lanes-enabled#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-rx-framer-serializer-lanes-enabled#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-rx-framer-serializer-lanes-enabled#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-rx-framer-serializer-lanes-enabled#3", NULL, 0},
	{SPINBUTTON, "adi,jesd204-rx-framer-serializer-lane-crossbar", NULL, 0},
	{SPINBUTTON, "adi,jesd204-rx-framer-serializer-amplitude", NULL, 0},
	{SPINBUTTON, "adi,jesd204-rx-framer-pre-emphasis", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-rx-framer-invert-lane-polarity#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-rx-framer-invert-lane-polarity#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-rx-framer-invert-lane-polarity#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-rx-framer-invert-lane-polarity#3", NULL, 0},
	{SPINBUTTON, "adi,jesd204-rx-framer-lmfc-offset", NULL, 0},
	{CHECKBOX, "adi,jesd204-rx-framer-new-sysref-on-relink", NULL, 0},
	{CHECKBOX, "adi,jesd204-rx-framer-enable-auto-chan-xbar", NULL, 0},
	{CHECKBOX, "adi,jesd204-rx-framer-obs-rx-syncb-select", NULL, 0},
	{CHECKBOX, "adi,jesd204-rx-framer-rx-syncb-mode", NULL, 0},
	{CHECKBOX, "adi,jesd204-rx-framer-over-sample", NULL, 0},

	{SPINBUTTON, "adi,jesd204-obs-framer-bank-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-obs-framer-device-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-obs-framer-lane0-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-obs-framer-m", NULL, 0},
	{SPINBUTTON, "adi,jesd204-obs-framer-k", NULL, 0},
	{CHECKBOX, "adi,jesd204-obs-framer-scramble", NULL, 0},
	{CHECKBOX, "adi,jesd204-obs-framer-external-sysref", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-obs-framer-serializer-lanes-enabled#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-obs-framer-serializer-lanes-enabled#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-obs-framer-serializer-lanes-enabled#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-obs-framer-serializer-lanes-enabled#3", NULL, 0},
	{SPINBUTTON, "adi,jesd204-obs-framer-serializer-lane-crossbar", NULL, 0},
	{SPINBUTTON, "adi,jesd204-obs-framer-serializer-amplitude", NULL, 0},
	{SPINBUTTON, "adi,jesd204-obs-framer-pre-emphasis", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-obs-framer-invert-lane-polarity#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-obs-framer-invert-lane-polarity#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-obs-framer-invert-lane-polarity#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-obs-framer-invert-lane-polarity#3", NULL, 0},
	{SPINBUTTON, "adi,jesd204-obs-framer-lmfc-offset", NULL, 0},
	{CHECKBOX, "adi,jesd204-obs-framer-new-sysref-on-relink", NULL, 0},
	{CHECKBOX, "adi,jesd204-obs-framer-enable-auto-chan-xbar", NULL, 0},
	{CHECKBOX, "adi,jesd204-obs-framer-obs-rx-syncb-select", NULL, 0},
	{CHECKBOX, "adi,jesd204-obs-framer-rx-syncb-mode", NULL, 0},
	{CHECKBOX, "adi,jesd204-obs-framer-over-sample", NULL, 0},

	{SPINBUTTON, "adi,jesd204-deframer-bank-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-device-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-lane0-id", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-m", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-k", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-scramble", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-external-sysref", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-deserializer-lanes-enabled#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-deserializer-lanes-enabled#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-deserializer-lanes-enabled#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-deserializer-lanes-enabled#3", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-deserializer-lane-crossbar", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-eq-setting", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-invert-lane-polarity#0", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-invert-lane-polarity#1", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-invert-lane-polarity#2", NULL, 0},
	{CHECKBOX_MASK, "adi,jesd204-deframer-invert-lane-polarity#3", NULL, 0},
	{SPINBUTTON, "adi,jesd204-deframer-lmfc-offset", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-new-sysref-on-relink", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-enable-auto-chan-xbar", NULL, 0},
	{CHECKBOX, "adi,jesd204-deframer-tx-syncb-mode", NULL, 0},

	{COMBOBOX, "adi,rx-gain-mode", (unsigned char[]){0, 2, 3}, 3},
	{SPINBUTTON, "adi,rx1-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx2-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx1-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx1-min-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx2-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx2-min-gain-index", NULL, 0},

	{COMBOBOX, "adi,orx-gain-mode", (unsigned char[]){0, 2, 3}, 3},
	{SPINBUTTON, "adi,orx1-gain-index", NULL, 0},
	{SPINBUTTON, "adi,orx2-gain-index", NULL, 0},
	{SPINBUTTON, "adi,orx-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,orx-min-gain-index", NULL, 0},

	{COMBOBOX, "adi,sniffer-gain-mode", (unsigned char[]){0, 2, 3}, 3},
	{SPINBUTTON, "adi,sniffer-gain-index", NULL, 0},
	{SPINBUTTON, "adi,sniffer-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,sniffer-min-gain-index", NULL, 0},

	{SPINBUTTON, "adi,rx-peak-agc-apd-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-apd-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-hb2-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-hb2-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-hb2-very-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-apd-high-thresh-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-apd-low-thresh-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-hb2-high-thresh-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-hb2-low-thresh-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-hb2-very-low-thresh-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-apd-high-gain-step-attack", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-apd-low-gain-step-recovery", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-hb2-high-gain-step-attack", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-hb2-low-gain-step-recovery", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-hb2-very-low-gain-step-recovery", NULL, 0},
	{CHECKBOX, "adi,rx-peak-agc-apd-fast-attack", NULL, 0},
	{CHECKBOX, "adi,rx-peak-agc-hb2-fast-attack", NULL, 0},
	{CHECKBOX, "adi,rx-peak-agc-hb2-overload-detect-enable", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-hb2-overload-duration-cnt", NULL, 0},
	{SPINBUTTON, "adi,rx-peak-agc-hb2-overload-thresh-cnt", NULL, 0},

	{SPINBUTTON, "adi,obs-peak-agc-apd-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-apd-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-hb2-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-hb2-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-hb2-very-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-apd-high-thresh-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-apd-low-thresh-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-hb2-high-thresh-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-hb2-low-thresh-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-hb2-very-low-thresh-exceeded-cnt", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-apd-high-gain-step-attack", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-apd-low-gain-step-recovery", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-hb2-high-gain-step-attack", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-hb2-low-gain-step-recovery", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-hb2-very-low-gain-step-recovery", NULL, 0},
	{CHECKBOX, "adi,obs-peak-agc-apd-fast-attack", NULL, 0},
	{CHECKBOX, "adi,obs-peak-agc-hb2-fast-attack", NULL, 0},
	{CHECKBOX, "adi,obs-peak-agc-hb2-overload-detect-enable", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-hb2-overload-duration-cnt", NULL, 0},
	{SPINBUTTON, "adi,obs-peak-agc-hb2-overload-thresh-cnt", NULL, 0},

	{SPINBUTTON, "adi,rx-pwr-agc-pmd-upper-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,rx-pwr-agc-pmd-upper-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,rx-pwr-agc-pmd-lower-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,rx-pwr-agc-pmd-lower-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,rx-pwr-agc-pmd-upper-high-gain-step-attack", NULL, 0},
	{SPINBUTTON, "adi,rx-pwr-agc-pmd-upper-low-gain-step-attack", NULL, 0},
	{SPINBUTTON, "adi,rx-pwr-agc-pmd-lower-high-gain-step-recovery", NULL, 0},
	{SPINBUTTON, "adi,rx-pwr-agc-pmd-lower-low-gain-step-recovery", NULL, 0},
	{SPINBUTTON, "adi,rx-pwr-agc-pmd-meas-duration", NULL, 0},
	{COMBOBOX, "adi,rx-pwr-agc-pmd-meas-config", (unsigned char[]){0, 1, 2, 3}, 4},

	{SPINBUTTON, "adi,obs-pwr-agc-pmd-upper-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,obs-pwr-agc-pmd-upper-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,obs-pwr-agc-pmd-lower-high-thresh", NULL, 0},
	{SPINBUTTON, "adi,obs-pwr-agc-pmd-lower-low-thresh", NULL, 0},
	{SPINBUTTON, "adi,obs-pwr-agc-pmd-upper-high-gain-step-attack", NULL, 0},
	{SPINBUTTON, "adi,obs-pwr-agc-pmd-upper-low-gain-step-attack", NULL, 0},
	{SPINBUTTON, "adi,obs-pwr-agc-pmd-lower-high-gain-step-recovery", NULL, 0},
	{SPINBUTTON, "adi,obs-pwr-agc-pmd-lower-low-gain-step-recovery", NULL, 0},
	{SPINBUTTON, "adi,obs-pwr-agc-pmd-meas-duration", NULL, 0},
	{COMBOBOX, "adi,obs-pwr-agc-pmd-meas-config", (unsigned char[]){0, 1, 2, 3}, 4},

	{SPINBUTTON, "adi,rx-agc-conf-agc-rx1-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx-agc-conf-agc-rx1-min-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx-agc-conf-agc-rx2-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,rx-agc-conf-agc-rx2-min-gain-index", NULL, 0},
	{CHECKBOX, "adi,rx-agc-conf-agc-peak-threshold-mode", NULL, 0},
	{CHECKBOX, "adi,rx-agc-conf-agc-low-ths-prevent-gain-increase", NULL, 0},
	{SPINBUTTON, "adi,rx-agc-conf-agc-gain-update-counter", NULL, 0},
	{SPINBUTTON, "adi,rx-agc-conf-agc-slow-loop-settling-delay", NULL, 0},
	{SPINBUTTON, "adi,rx-agc-conf-agc-peak-wait-time", NULL, 0},
	{CHECKBOX, "adi,rx-agc-conf-agc-reset-on-rx-enable", NULL, 0},
	{CHECKBOX, "adi,rx-agc-conf-agc-enable-sync-pulse-for-gain-counter", NULL, 0},

	{SPINBUTTON, "adi,obs-agc-conf-agc-obs-rx-max-gain-index", NULL, 0},
	{SPINBUTTON, "adi,obs-agc-conf-agc-obs-rx-min-gain-index", NULL, 0},
	{CHECKBOX, "adi,obs-agc-conf-agc-obs-rx-select", NULL, 0},
	{CHECKBOX, "adi,obs-agc-conf-agc-peak-threshold-mode", NULL, 0},
	{CHECKBOX, "adi,obs-agc-conf-agc-low-ths-prevent-gain-increase", NULL, 0},
	{SPINBUTTON, "adi,obs-agc-conf-agc-gain-update-counter", NULL, 0},
	{SPINBUTTON, "adi,obs-agc-conf-agc-slow-loop-settling-delay", NULL, 0},
	{SPINBUTTON, "adi,obs-agc-conf-agc-peak-wait-time", NULL, 0},
	{CHECKBOX, "adi,obs-agc-conf-agc-reset-on-rx-enable", NULL, 0},
	{CHECKBOX, "adi,obs-agc-conf-agc-enable-sync-pulse-for-gain-counter", NULL, 0},

	{SPINBUTTON, "adi,rx-profile-adc-div", NULL, 0},
	{COMBOBOX, "adi,rx-profile-rx-fir-decimation", (unsigned char[]){1,2,4}, 3},
	{SPINBUTTON, "adi,rx-profile-rx-dec5-decimation", NULL, 0},
	{CHECKBOX, "adi,rx-profile-en-high-rej-dec5", NULL, 0},
	{SPINBUTTON, "adi,rx-profile-rhb1-decimation", NULL, 0},
	{SPINBUTTON, "adi,rx-profile-iq-rate_khz", NULL, 0},
	{SPINBUTTON, "adi,rx-profile-rf-bandwidth_hz", NULL, 0},
	{SPINBUTTON, "adi,rx-profile-rx-bbf-3db-corner_khz", NULL, 0},

	{SPINBUTTON, "adi,obs-profile-adc-div", NULL, 0},
	{COMBOBOX, "adi,obs-profile-rx-fir-decimation", (unsigned char[]){1,2,4}, 3},
	{SPINBUTTON, "adi,obs-profile-rx-dec5-decimation", NULL, 0},
	{CHECKBOX, "adi,obs-profile-en-high-rej-dec5", NULL, 0},
	{SPINBUTTON, "adi,obs-profile-rhb1-decimation", NULL, 0},
	{SPINBUTTON, "adi,obs-profile-iq-rate_khz", NULL, 0},
	{SPINBUTTON, "adi,obs-profile-rf-bandwidth_hz", NULL, 0},
	{SPINBUTTON, "adi,obs-profile-rx-bbf-3db-corner_khz", NULL, 0},

	{SPINBUTTON, "adi,sniffer-profile-adc-div", NULL, 0},
	{COMBOBOX, "adi,sniffer-profile-rx-fir-decimation", (unsigned char[]){1,2,4}, 3},
	{SPINBUTTON, "adi,sniffer-profile-rx-dec5-decimation", NULL, 0},
	{CHECKBOX, "adi,sniffer-profile-en-high-rej-dec5", NULL, 0},
	{SPINBUTTON, "adi,sniffer-profile-rhb1-decimation", NULL, 0},
	{SPINBUTTON, "adi,sniffer-profile-iq-rate_khz", NULL, 0},
	{SPINBUTTON, "adi,sniffer-profile-rf-bandwidth_hz", NULL, 0},
	{SPINBUTTON, "adi,sniffer-profile-rx-bbf-3db-corner_khz", NULL, 0},

	{COMBOBOX, "adi,tx-profile-dac-div", (unsigned char[]){0,1,2}, 3},
	{COMBOBOX, "adi,tx-profile-tx-fir-interpolation", (unsigned char[]){1,2,4}, 3},
	{SPINBUTTON, "adi,tx-profile-thb1-interpolation", NULL, 0},
	{SPINBUTTON, "adi,tx-profile-thb2-interpolation", NULL, 0},
	{SPINBUTTON, "adi,tx-profile-tx-input-hb-interpolation", NULL, 0},
	{SPINBUTTON, "adi,tx-profile-iq-rate_khz", NULL, 0},
	{SPINBUTTON, "adi,tx-profile-primary-sig-bandwidth_hz", NULL, 0},
	{SPINBUTTON, "adi,tx-profile-rf-bandwidth_hz", NULL, 0},
	{SPINBUTTON, "adi,tx-profile-tx-dac-3db-corner_khz", NULL, 0},
	{SPINBUTTON, "adi,tx-profile-tx-bbf-3db-corner_khz", NULL, 0},

	{SPINBUTTON, "adi,clocks-device-clock_khz", NULL, 0},
	{SPINBUTTON, "adi,clocks-clk-pll-vco-freq_khz", NULL, 0},
	{COMBOBOX, "adi,clocks-clk-pll-vco-div", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,clocks-clk-pll-hs-div", NULL, 0},

	{COMBOBOX, "adi,tx-settings-tx-channels-enable", (unsigned char[]){0,1,2,3}, 4},
	{CHECKBOX, "adi,tx-settings-tx-pll-use-external-lo", NULL, 0},
	{SPINBUTTON, "adi,tx-settings-tx-pll-lo-frequency_hz", NULL, 0},
	{COMBOBOX, "adi,tx-settings-tx-atten-step-size", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,tx-settings-tx1-atten_mdb", NULL, 0},
	{SPINBUTTON, "adi,tx-settings-tx2-atten_mdb", NULL, 0},

	{COMBOBOX, "adi,rx-settings-rx-channels-enable", (unsigned char[]){0,1,2,3}, 4},
	{CHECKBOX, "adi,rx-settings-rx-pll-use-external-lo", NULL, 0},
	{SPINBUTTON, "adi,rx-settings-rx-pll-lo-frequency_hz", NULL, 0},
	{CHECKBOX, "adi,rx-settings-real-if-data", NULL, 0},

	{CHECKBOX_MASK, "adi,obs-settings-obs-rx-channels-enable#0", NULL, 0},
	{CHECKBOX_MASK, "adi,obs-settings-obs-rx-channels-enable#1", NULL, 0},
	{CHECKBOX_MASK, "adi,obs-settings-obs-rx-channels-enable#2", NULL, 0},
	{CHECKBOX_MASK, "adi,obs-settings-obs-rx-channels-enable#3", NULL, 0},
	{CHECKBOX_MASK, "adi,obs-settings-obs-rx-channels-enable#4", NULL, 0},

	{COMBOBOX, "adi,obs-settings-obs-rx-lo-source", (unsigned char[]){0, 1}, 2},
	{SPINBUTTON, "adi,obs-settings-sniffer-pll-lo-frequency_hz", NULL, 0},
	{CHECKBOX, "adi,obs-settings-real-if-data", NULL, 0},
	{COMBOBOX, "adi,obs-settings-default-obs-rx-channel", (unsigned char[]){0,1,2,3,4,5,6,0x14,0x24,0x34}, 10},

	{CHECKBOX, "adi,arm-gpio-use-rx2-enable-pin", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-use-tx2-enable-pin", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-tx-rx-pin-mode", NULL, 0},
	{CHECKBOX, "adi,arm-gpio-orx-pin-mode", NULL, 0},
	{SPINBUTTON, "adi,arm-gpio-orx-trigger-pin", NULL, 0},
	{SPINBUTTON, "adi,arm-gpio-orx-mode2-pin", NULL, 0},
	{SPINBUTTON, "adi,arm-gpio-orx-mode1-pin", NULL, 0},
	{SPINBUTTON, "adi,arm-gpio-orx-mode0-pin", NULL, 0},
	{SPINBUTTON, "adi,arm-gpio-rx1-enable-ack", NULL, 4}, /* Special handling */
	{SPINBUTTON, "adi,arm-gpio-rx2-enable-ack", NULL, 4}, /* Special handling */
	{SPINBUTTON, "adi,arm-gpio-tx1-enable-ack", NULL, 4}, /* Special handling */
	{SPINBUTTON, "adi,arm-gpio-tx2-enable-ack", NULL, 4}, /* Special handling */
	{SPINBUTTON, "adi,arm-gpio-orx1-enable-ack", NULL, 4}, /* Special handling */
	{SPINBUTTON, "adi,arm-gpio-orx2-enable-ack", NULL, 4}, /* Special handling */
	{SPINBUTTON, "adi,arm-gpio-srx-enable-ack", NULL, 4}, /* Special handling */
	{SPINBUTTON, "adi,arm-gpio-tx-obs-select", NULL, 4}, /* Special handling */
	{CHECKBOX_MASK, "adi,arm-gpio-rx1-enable-ack#4", NULL, 0}, /* Special handling */
	{CHECKBOX_MASK, "adi,arm-gpio-rx2-enable-ack#4", NULL, 0}, /* Special handling */
	{CHECKBOX_MASK, "adi,arm-gpio-tx1-enable-ack#4", NULL, 0}, /* Special handling */
	{CHECKBOX_MASK, "adi,arm-gpio-tx2-enable-ack#4", NULL, 0}, /* Special handling */
	{CHECKBOX_MASK, "adi,arm-gpio-orx1-enable-ack#4", NULL, 0}, /* Special handling */
	{CHECKBOX_MASK, "adi,arm-gpio-orx2-enable-ack#4", NULL, 0}, /* Special handling */
	{CHECKBOX_MASK, "adi,arm-gpio-srx-enable-ack#4", NULL, 0}, /* Special handling */
	{CHECKBOX_MASK, "adi,arm-gpio-tx-obs-select#4", NULL, 0}, /* Special handling */
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#0", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#1", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#2", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#3", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#4", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#5", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#6", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#7", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#8", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#9", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#10", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-3v3-oe-mask#11", NULL, 0},
	{COMBOBOX, "adi,gpio-3v3-src-ctrl3_0", (unsigned char[]){1,2,3,4}, 4},
	{COMBOBOX, "adi,gpio-3v3-src-ctrl7_4", (unsigned char[]){1,2,3,4}, 4},
	{COMBOBOX, "adi,gpio-3v3-src-ctrl11_8", (unsigned char[]){1,2,3,4}, 4},

	{CHECKBOX_MASK, "adi,gpio-oe-mask#0", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#1", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#2", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#3", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#4", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#5", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#6", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#7", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#8", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#9", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#10", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#11", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#12", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#13", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#14", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#15", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#16", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#17", NULL, 0},
	{CHECKBOX_MASK, "adi,gpio-oe-mask#18", NULL, 0},
	{COMBOBOX, "adi,gpio-src-ctrl3_0", (unsigned char[]){0,3,9,10}, 4},
	{COMBOBOX, "adi,gpio-src-ctrl7_4", (unsigned char[]){0,3,9,10}, 4},
	{COMBOBOX, "adi,gpio-src-ctrl11_8", (unsigned char[]){0,3,9,10}, 4},
	{COMBOBOX, "adi,gpio-src-ctrl15_12", (unsigned char[]){0,3,9,10}, 4},
	{COMBOBOX, "adi,gpio-src-ctrl18_16", (unsigned char[]){0,3,9,10}, 4},

	{CHECKBOX_MASK, "adi,aux-dac-enable-mask#0", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enable-mask#1", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enable-mask#2", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enable-mask#3", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enable-mask#4", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enable-mask#5", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enable-mask#6", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enable-mask#7", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enable-mask#8", NULL, 0},
	{CHECKBOX_MASK, "adi,aux-dac-enable-mask#9", NULL, 0},

	{SPINBUTTON, "adi,aux-dac-value0", NULL, 0},
	{COMBOBOX, "adi,aux-dac-slope0", (unsigned char[]){0,1}, 2},
	{COMBOBOX, "adi,aux-dac-vref0", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,aux-dac-value1", NULL, 0},
	{COMBOBOX, "adi,aux-dac-slope1", (unsigned char[]){0,1}, 2},
	{COMBOBOX, "adi,aux-dac-vref1", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,aux-dac-value2", NULL, 0},
	{COMBOBOX, "adi,aux-dac-slope2", (unsigned char[]){0,1}, 2},
	{COMBOBOX, "adi,aux-dac-vref2", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,aux-dac-value3", NULL, 0},
	{COMBOBOX, "adi,aux-dac-slope3", (unsigned char[]){0,1}, 2},
	{COMBOBOX, "adi,aux-dac-vref3", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,aux-dac-value4", NULL, 0},
	{COMBOBOX, "adi,aux-dac-slope4", (unsigned char[]){0,1}, 2},
	{COMBOBOX, "adi,aux-dac-vref4", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,aux-dac-value5", NULL, 0},
	{COMBOBOX, "adi,aux-dac-slope5", (unsigned char[]){0,1}, 2},
	{COMBOBOX, "adi,aux-dac-vref5", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,aux-dac-value6", NULL, 0},
	{COMBOBOX, "adi,aux-dac-slope6", (unsigned char[]){0,1}, 2},
	{COMBOBOX, "adi,aux-dac-vref6", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,aux-dac-value7", NULL, 0},
	{COMBOBOX, "adi,aux-dac-slope7", (unsigned char[]){0,1}, 2},
	{COMBOBOX, "adi,aux-dac-vref7", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,aux-dac-value8", NULL, 0},
	{COMBOBOX, "adi,aux-dac-slope8", (unsigned char[]){0,1}, 2},
	{COMBOBOX, "adi,aux-dac-vref8", (unsigned char[]){0,1,2,3}, 4},
	{SPINBUTTON, "adi,aux-dac-value9", NULL, 0},
	{COMBOBOX, "adi,aux-dac-slope9", (unsigned char[]){0,1}, 2},
	{COMBOBOX, "adi,aux-dac-vref9", (unsigned char[]){0,1,2,3}, 4},

	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#14", NULL, 0},
	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#10", NULL, 0},
	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#9", NULL, 0},
	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#8", NULL, 0},
	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#15", NULL, 0},
	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#16", NULL, 0},
	{CHECKBOX_MASK, "adi,default-initial-calibrations-mask#17", NULL, 0},


	{SPINBUTTON, "adi,dpd-damping", NULL, 0},
	{SPINBUTTON, "adi,dpd-num-weights", NULL, 0},
	{SPINBUTTON, "adi,dpd-model-version", NULL, 0},
	{CHECKBOX, "adi,dpd-high-power-model-update", NULL, 0},
	{SPINBUTTON, "adi,dpd-model-prior-weight", NULL, 0},
	{CHECKBOX, "adi,dpd-robust-modeling", NULL, 0},
	{SPINBUTTON, "adi,dpd-samples", NULL, 0},
	{SPINBUTTON, "adi,dpd-outlier-threshold", NULL, 0},
	{SPINBUTTON, "adi,dpd-additional-delay-offset", NULL, 0},
	{SPINBUTTON, "adi,dpd-path-delay-pn-seq-level", NULL, 0},
	{SPINBUTTON_S8, "adi,dpd-weights0-real", NULL, 0},
	{SPINBUTTON_S8, "adi,dpd-weights0-imag", NULL, 0},
	{SPINBUTTON_S8, "adi,dpd-weights1-real", NULL, 0},
	{SPINBUTTON_S8, "adi,dpd-weights1-imag", NULL, 0},
	{SPINBUTTON_S8, "adi,dpd-weights2-real", NULL, 0},
	{SPINBUTTON_S8, "adi,dpd-weights2-imag", NULL, 0},

	{SPINBUTTON_S16, "adi,clgc-tx1-desired-gain", NULL, 0},
	{SPINBUTTON_S16, "adi,clgc-tx2-desired-gain", NULL, 0},
	{SPINBUTTON, "adi,clgc-tx1-atten-limit", NULL, 0},
	{SPINBUTTON, "adi,clgc-tx2-atten-limit", NULL, 0},
	{SPINBUTTON, "adi,clgc-tx1-control-ratio", NULL, 0},
	{SPINBUTTON, "adi,clgc-tx2-control-ratio", NULL, 0},
	{CHECKBOX, "adi,clgc-allow-tx1-atten-updates", NULL, 0},
	{CHECKBOX, "adi,clgc-allow-tx2-atten-updates", NULL, 0},
	{SPINBUTTON_S16, "adi,clgc-additional-delay-offset", NULL, 0},
	{SPINBUTTON, "adi,clgc-path-delay-pn-seq-level", NULL, 0},
	{SPINBUTTON, "adi,clgc-tx1-rel-threshold", NULL, 0},
	{SPINBUTTON, "adi,clgc-tx2-rel-threshold", NULL, 0},
	{CHECKBOX, "adi,clgc-tx1-rel-threshold-en", NULL, 0},
	{CHECKBOX, "adi,clgc-tx2-rel-threshold-en", NULL, 0},

	{SPINBUTTON_S16, "adi,vswr-additional-delay-offset", NULL, 0},
	{SPINBUTTON, "adi,vswr-path-delay-pn-seq-level", NULL, 0},
	{SPINBUTTON, "adi,vswr-tx1-vswr-switch-gpio3p3-pin", NULL, 0},
	{SPINBUTTON, "adi,vswr-tx2-vswr-switch-gpio3p3-pin", NULL, 0},
	{CHECKBOX, "adi,vswr-tx1-vswr-switch-polarity", NULL, 0},
	{CHECKBOX, "adi,vswr-tx2-vswr-switch-polarity", NULL, 0},
	{SPINBUTTON, "adi,vswr-tx1-vswr-switch-delay_us", NULL, 0},
	{SPINBUTTON, "adi,vswr-tx2-vswr-switch-delay_us", NULL, 0},

	{COMBOBOX, "bist_prbs_rx", (unsigned char[]){0, 1, 2, 3}, 4},
	{COMBOBOX, "bist_prbs_obs", (unsigned char[]){0, 1, 2, 3}, 4},
	{CHECKBOX, "loopback_tx_rx", NULL, 0},
	{CHECKBOX, "loopback_tx_obs", NULL, 0},
	{BUTTON, "initialize", NULL, 0},
};

static const char *ad9371_adv_sr_attribs[] = {
	"debug.ad9371-phy.adi,jesd204-rx-framer-bank-id",
	"debug.ad9371-phy.adi,jesd204-rx-framer-device-id",
	"debug.ad9371-phy.adi,jesd204-rx-framer-lane0-id",
	"debug.ad9371-phy.adi,jesd204-rx-framer-m",
	"debug.ad9371-phy.adi,jesd204-rx-framer-k",
	"debug.ad9371-phy.adi,jesd204-rx-framer-scramble",
	"debug.ad9371-phy.adi,jesd204-rx-framer-external-sysref",
	"debug.ad9371-phy.adi,jesd204-rx-framer-serializer-lanes-enabled",
	"debug.ad9371-phy.adi,jesd204-rx-framer-serializer-lane-crossbar",
	"debug.ad9371-phy.adi,jesd204-rx-framer-serializer-amplitude",
	"debug.ad9371-phy.adi,jesd204-rx-framer-pre-emphasis",
	"debug.ad9371-phy.adi,jesd204-rx-framer-invert-lane-polarity",
	"debug.ad9371-phy.adi,jesd204-rx-framer-lmfc-offset",
	"debug.ad9371-phy.adi,jesd204-rx-framer-new-sysref-on-relink",
	"debug.ad9371-phy.adi,jesd204-rx-framer-enable-auto-chan-xbar",
	"debug.ad9371-phy.adi,jesd204-rx-framer-obs-rx-syncb-select",
	"debug.ad9371-phy.adi,jesd204-rx-framer-rx-syncb-mode",
	"debug.ad9371-phy.adi,jesd204-rx-framer-over-sample",

	"debug.ad9371-phy.adi,jesd204-obs-framer-bank-id",
	"debug.ad9371-phy.adi,jesd204-obs-framer-device-id",
	"debug.ad9371-phy.adi,jesd204-obs-framer-lane0-id",
	"debug.ad9371-phy.adi,jesd204-obs-framer-m",
	"debug.ad9371-phy.adi,jesd204-obs-framer-k",
	"debug.ad9371-phy.adi,jesd204-obs-framer-scramble",
	"debug.ad9371-phy.adi,jesd204-obs-framer-external-sysref",
	"debug.ad9371-phy.adi,jesd204-obs-framer-serializer-lanes-enabled",
	"debug.ad9371-phy.adi,jesd204-obs-framer-serializer-lane-crossbar",
	"debug.ad9371-phy.adi,jesd204-obs-framer-serializer-amplitude",
	"debug.ad9371-phy.adi,jesd204-obs-framer-pre-emphasis",
	"debug.ad9371-phy.adi,jesd204-obs-framer-invert-lane-polarity",
	"debug.ad9371-phy.adi,jesd204-obs-framer-lmfc-offset",
	"debug.ad9371-phy.adi,jesd204-obs-framer-new-sysref-on-relink",
	"debug.ad9371-phy.adi,jesd204-obs-framer-enable-auto-chan-xbar",
	"debug.ad9371-phy.adi,jesd204-obs-framer-obs-rx-syncb-select",
	"debug.ad9371-phy.adi,jesd204-obs-framer-rx-syncb-mode",
	"debug.ad9371-phy.adi,jesd204-obs-framer-over-sample",


	"debug.ad9371-phy.adi,jesd204-deframer-bank-id",
	"debug.ad9371-phy.adi,jesd204-deframer-device-id",
	"debug.ad9371-phy.adi,jesd204-deframer-lane0-id",
	"debug.ad9371-phy.adi,jesd204-deframer-m",
	"debug.ad9371-phy.adi,jesd204-deframer-k",
	"debug.ad9371-phy.adi,jesd204-deframer-scramble",
	"debug.ad9371-phy.adi,jesd204-deframer-external-sysref",
	"debug.ad9371-phy.adi,jesd204-deframer-deserializer-lanes-enabled",
	"debug.ad9371-phy.adi,jesd204-deframer-deserializer-lane-crossbar",
	"debug.ad9371-phy.adi,jesd204-deframer-eq-setting",
	"debug.ad9371-phy.adi,jesd204-deframer-invert-lane-polarity",
	"debug.ad9371-phy.adi,jesd204-deframer-lmfc-offset",
	"debug.ad9371-phy.adi,jesd204-deframer-new-sysref-on-relink",
	"debug.ad9371-phy.adi,jesd204-deframer-enable-auto-chan-xbar",
	"debug.ad9371-phy.adi,jesd204-deframer-tx-syncb-mode",

	"debug.ad9371-phy.adi,rx-gain-mode",
	"debug.ad9371-phy.adi,rx1-gain-index",
	"debug.ad9371-phy.adi,rx2-gain-index",
	"debug.ad9371-phy.adi,rx1-max-gain-index",
	"debug.ad9371-phy.adi,rx1-min-gain-index",
	"debug.ad9371-phy.adi,rx2-max-gain-index",
	"debug.ad9371-phy.adi,rx2-min-gain-index",

	"debug.ad9371-phy.adi,orx-gain-mode",
	"debug.ad9371-phy.adi,orx1-gain-index",
	"debug.ad9371-phy.adi,orx2-gain-index",
	"debug.ad9371-phy.adi,orx-max-gain-index",
	"debug.ad9371-phy.adi,orx-min-gain-index",

	"debug.ad9371-phy.adi,sniffer-gain-mode",
	"debug.ad9371-phy.adi,sniffer-gain-index",
	"debug.ad9371-phy.adi,sniffer-max-gain-index",
	"debug.ad9371-phy.adi,sniffer-min-gain-index",

	"debug.ad9371-phy.adi,rx-peak-agc-apd-high-thresh",
	"debug.ad9371-phy.adi,rx-peak-agc-apd-low-thresh",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-high-thresh",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-low-thresh",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-very-low-thresh",
	"debug.ad9371-phy.adi,rx-peak-agc-apd-high-thresh-exceeded-cnt",
	"debug.ad9371-phy.adi,rx-peak-agc-apd-low-thresh-exceeded-cnt",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-high-thresh-exceeded-cnt",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-low-thresh-exceeded-cnt",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-very-low-thresh-exceeded-cnt",
	"debug.ad9371-phy.adi,rx-peak-agc-apd-high-gain-step-attack",
	"debug.ad9371-phy.adi,rx-peak-agc-apd-low-gain-step-recovery",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-high-gain-step-attack",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-low-gain-step-recovery",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-very-low-gain-step-recovery",
	"debug.ad9371-phy.adi,rx-peak-agc-apd-fast-attack",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-fast-attack",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-overload-detect-enable",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-overload-duration-cnt",
	"debug.ad9371-phy.adi,rx-peak-agc-hb2-overload-thresh-cnt",

	"debug.ad9371-phy.adi,obs-peak-agc-apd-high-thresh",
	"debug.ad9371-phy.adi,obs-peak-agc-apd-low-thresh",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-high-thresh",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-low-thresh",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-very-low-thresh",
	"debug.ad9371-phy.adi,obs-peak-agc-apd-high-thresh-exceeded-cnt",
	"debug.ad9371-phy.adi,obs-peak-agc-apd-low-thresh-exceeded-cnt",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-high-thresh-exceeded-cnt",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-low-thresh-exceeded-cnt",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-very-low-thresh-exceeded-cnt",
	"debug.ad9371-phy.adi,obs-peak-agc-apd-high-gain-step-attack",
	"debug.ad9371-phy.adi,obs-peak-agc-apd-low-gain-step-recovery",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-high-gain-step-attack",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-low-gain-step-recovery",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-very-low-gain-step-recovery",
	"debug.ad9371-phy.adi,obs-peak-agc-apd-fast-attack",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-fast-attack",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-overload-detect-enable",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-overload-duration-cnt",
	"debug.ad9371-phy.adi,obs-peak-agc-hb2-overload-thresh-cnt",

	"debug.ad9371-phy.adi,rx-pwr-agc-pmd-upper-high-thresh",
	"debug.ad9371-phy.adi,rx-pwr-agc-pmd-upper-low-thresh",
	"debug.ad9371-phy.adi,rx-pwr-agc-pmd-lower-high-thresh",
	"debug.ad9371-phy.adi,rx-pwr-agc-pmd-lower-low-thresh",
	"debug.ad9371-phy.adi,rx-pwr-agc-pmd-upper-high-gain-step-attack",
	"debug.ad9371-phy.adi,rx-pwr-agc-pmd-upper-low-gain-step-attack",
	"debug.ad9371-phy.adi,rx-pwr-agc-pmd-lower-high-gain-step-recovery",
	"debug.ad9371-phy.adi,rx-pwr-agc-pmd-lower-low-gain-step-recovery",
	"debug.ad9371-phy.adi,rx-pwr-agc-pmd-meas-duration",
	"debug.ad9371-phy.adi,rx-pwr-agc-pmd-meas-config",

	"debug.ad9371-phy.adi,obs-pwr-agc-pmd-upper-high-thresh",
	"debug.ad9371-phy.adi,obs-pwr-agc-pmd-upper-low-thresh",
	"debug.ad9371-phy.adi,obs-pwr-agc-pmd-lower-high-thresh",
	"debug.ad9371-phy.adi,obs-pwr-agc-pmd-lower-low-thresh",
	"debug.ad9371-phy.adi,obs-pwr-agc-pmd-upper-high-gain-step-attack",
	"debug.ad9371-phy.adi,obs-pwr-agc-pmd-upper-low-gain-step-attack",
	"debug.ad9371-phy.adi,obs-pwr-agc-pmd-lower-high-gain-step-recovery",
	"debug.ad9371-phy.adi,obs-pwr-agc-pmd-lower-low-gain-step-recovery",
	"debug.ad9371-phy.adi,obs-pwr-agc-pmd-meas-duration",
	"debug.ad9371-phy.adi,obs-pwr-agc-pmd-meas-config",

	"debug.ad9371-phy.adi,rx-agc-conf-agc-rx1-max-gain-index",
	"debug.ad9371-phy.adi,rx-agc-conf-agc-rx1-min-gain-index",
	"debug.ad9371-phy.adi,rx-agc-conf-agc-rx2-max-gain-index",
	"debug.ad9371-phy.adi,rx-agc-conf-agc-rx2-min-gain-index",
	"debug.ad9371-phy.adi,rx-agc-conf-agc-peak-threshold-mode",
	"debug.ad9371-phy.adi,rx-agc-conf-agc-low-ths-prevent-gain-increase",
	"debug.ad9371-phy.adi,rx-agc-conf-agc-gain-update-counter",
	"debug.ad9371-phy.adi,rx-agc-conf-agc-slow-loop-settling-delay",
	"debug.ad9371-phy.adi,rx-agc-conf-agc-peak-wait-time",
	"debug.ad9371-phy.adi,rx-agc-conf-agc-reset-on-rx-enable",
	"debug.ad9371-phy.adi,rx-agc-conf-agc-enable-sync-pulse-for-gain-counter",

	"debug.ad9371-phy.adi,obs-agc-conf-agc-obs-rx-max-gain-index",
	"debug.ad9371-phy.adi,obs-agc-conf-agc-obs-rx-min-gain-index",
	"debug.ad9371-phy.adi,obs-agc-conf-agc-obs-rx-select",
	"debug.ad9371-phy.adi,obs-agc-conf-agc-peak-threshold-mode",
	"debug.ad9371-phy.adi,obs-agc-conf-agc-low-ths-prevent-gain-increase",
	"debug.ad9371-phy.adi,obs-agc-conf-agc-gain-update-counter",
	"debug.ad9371-phy.adi,obs-agc-conf-agc-slow-loop-settling-delay",
	"debug.ad9371-phy.adi,obs-agc-conf-agc-peak-wait-time",
	"debug.ad9371-phy.adi,obs-agc-conf-agc-reset-on-rx-enable",
	"debug.ad9371-phy.adi,obs-agc-conf-agc-enable-sync-pulse-for-gain-counter",

	"debug.ad9371-phy.adi,rx-profile-adc-div",
	"debug.ad9371-phy.adi,rx-profile-rx-fir-decimation",
	"debug.ad9371-phy.adi,rx-profile-rx-dec5-decimation",
	"debug.ad9371-phy.adi,rx-profile-en-high-rej-dec5",
	"debug.ad9371-phy.adi,rx-profile-rhb1-decimation",
	"debug.ad9371-phy.adi,rx-profile-iq-rate_khz",
	"debug.ad9371-phy.adi,rx-profile-rf-bandwidth_hz",
	"debug.ad9371-phy.adi,rx-profile-rx-bbf-3db-corner_khz",

	"debug.ad9371-phy.adi,obs-profile-adc-div",
	"debug.ad9371-phy.adi,obs-profile-rx-fir-decimation",
	"debug.ad9371-phy.adi,obs-profile-rx-dec5-decimation",
	"debug.ad9371-phy.adi,obs-profile-en-high-rej-dec5",
	"debug.ad9371-phy.adi,obs-profile-rhb1-decimation",
	"debug.ad9371-phy.adi,obs-profile-iq-rate_khz",
	"debug.ad9371-phy.adi,obs-profile-rf-bandwidth_hz",
	"debug.ad9371-phy.adi,obs-profile-rx-bbf-3db-corner_khz",

	"debug.ad9371-phy.adi,sniffer-profile-adc-div",
	"debug.ad9371-phy.adi,sniffer-profile-rx-fir-decimation",
	"debug.ad9371-phy.adi,sniffer-profile-rx-dec5-decimation",
	"debug.ad9371-phy.adi,sniffer-profile-en-high-rej-dec5",
	"debug.ad9371-phy.adi,sniffer-profile-rhb1-decimation",
	"debug.ad9371-phy.adi,sniffer-profile-iq-rate_khz",
	"debug.ad9371-phy.adi,sniffer-profile-rf-bandwidth_hz",
	"debug.ad9371-phy.adi,sniffer-profile-rx-bbf-3db-corner_khz",

	"debug.ad9371-phy.adi,tx-profile-dac-div",
	"debug.ad9371-phy.adi,tx-profile-tx-fir-interpolation",
	"debug.ad9371-phy.adi,tx-profile-thb1-interpolation",
	"debug.ad9371-phy.adi,tx-profile-thb2-interpolation",
	"debug.ad9371-phy.adi,tx-profile-tx-input-hb-interpolation",
	"debug.ad9371-phy.adi,tx-profile-iq-rate_khz",
	"debug.ad9371-phy.adi,tx-profile-primary-sig-bandwidth_hz",
	"debug.ad9371-phy.adi,tx-profile-rf-bandwidth_hz",
	"debug.ad9371-phy.adi,tx-profile-tx-dac-3db-corner_khz",
	"debug.ad9371-phy.adi,tx-profile-tx-bbf-3db-corner_khz",

	"debug.ad9371-phy.adi,clocks-device-clock_khz",
	"debug.ad9371-phy.adi,clocks-clk-pll-vco-freq_khz",
	"debug.ad9371-phy.adi,clocks-clk-pll-vco-div",
	"debug.ad9371-phy.adi,clocks-clk-pll-hs-div",

	"debug.ad9371-phy.adi,tx-settings-tx-channels-enable",
	"debug.ad9371-phy.adi,tx-settings-tx-pll-use-external-lo",
	"debug.ad9371-phy.adi,tx-settings-tx-pll-lo-frequency_hz",
	"debug.ad9371-phy.adi,tx-settings-tx-atten-step-size",
	"debug.ad9371-phy.adi,tx-settings-tx1-atten_mdb",
	"debug.ad9371-phy.adi,tx-settings-tx2-atten_mdb",

	"debug.ad9371-phy.adi,rx-settings-rx-channels-enable",
	"debug.ad9371-phy.adi,rx-settings-rx-pll-use-external-lo",
	"debug.ad9371-phy.adi,rx-settings-rx-pll-lo-frequency_hz",
	"debug.ad9371-phy.adi,rx-settings-real-if-data",

	"debug.ad9371-phy.adi,obs-settings-obs-rx-channels-enable",
	"debug.ad9371-phy.adi,obs-settings-obs-rx-lo-source",
	"debug.ad9371-phy.adi,obs-settings-sniffer-pll-lo-frequency_hz",
	"debug.ad9371-phy.adi,obs-settings-real-if-data",
	"debug.ad9371-phy.adi,obs-settings-default-obs-rx-channel",

	"debug.ad9371-phy.adi,arm-gpio-use-rx2-enable-pin",
	"debug.ad9371-phy.adi,arm-gpio-use-tx2-enable-pin",
	"debug.ad9371-phy.adi,arm-gpio-tx-rx-pin-mode",
	"debug.ad9371-phy.adi,arm-gpio-orx-pin-mode",
	"debug.ad9371-phy.adi,arm-gpio-orx-trigger-pin",
	"debug.ad9371-phy.adi,arm-gpio-orx-mode2-pin",
	"debug.ad9371-phy.adi,arm-gpio-orx-mode1-pin",
	"debug.ad9371-phy.adi,arm-gpio-orx-mode0-pin",
	"debug.ad9371-phy.adi,arm-gpio-rx1-enable-ack",
	"debug.ad9371-phy.adi,arm-gpio-rx2-enable-ack",
	"debug.ad9371-phy.adi,arm-gpio-tx1-enable-ack",
	"debug.ad9371-phy.adi,arm-gpio-tx2-enable-ack",
	"debug.ad9371-phy.adi,arm-gpio-orx1-enable-ack",
	"debug.ad9371-phy.adi,arm-gpio-orx2-enable-ack",
	"debug.ad9371-phy.adi,arm-gpio-srx-enable-ack",
	"debug.ad9371-phy.adi,arm-gpio-tx-obs-select",
	"debug.ad9371-phy.adi,arm-gpio-enable-mask",

	"debug.ad9371-phy.adi,gpio-3v3-oe-mask",
	"debug.ad9371-phy.adi,gpio-3v3-src-ctrl3_0",
	"debug.ad9371-phy.adi,gpio-3v3-src-ctrl7_4",
	"debug.ad9371-phy.adi,gpio-3v3-src-ctrl11_8",

	"debug.ad9371-phy.adi,gpio-oe-mask",
	"debug.ad9371-phy.adi,gpio-src-ctrl3_0",
	"debug.ad9371-phy.adi,gpio-src-ctrl7_4",
	"debug.ad9371-phy.adi,gpio-src-ctrl11_8",
	"debug.ad9371-phy.adi,gpio-src-ctrl15_12",
	"debug.ad9371-phy.adi,gpio-src-ctrl18_16",

	"debug.ad9371-phy.adi,aux-dac-enable-mask",
	"debug.ad9371-phy.adi,aux-dac-value0",
	"debug.ad9371-phy.adi,aux-dac-slope0",
	"debug.ad9371-phy.adi,aux-dac-vref0",
	"debug.ad9371-phy.adi,aux-dac-value1",
	"debug.ad9371-phy.adi,aux-dac-slope1",
	"debug.ad9371-phy.adi,aux-dac-vref1",
	"debug.ad9371-phy.adi,aux-dac-value2",
	"debug.ad9371-phy.adi,aux-dac-slope2",
	"debug.ad9371-phy.adi,aux-dac-vref2",
	"debug.ad9371-phy.adi,aux-dac-value3",
	"debug.ad9371-phy.adi,aux-dac-slope3",
	"debug.ad9371-phy.adi,aux-dac-vref3",
	"debug.ad9371-phy.adi,aux-dac-value4",
	"debug.ad9371-phy.adi,aux-dac-slope4",
	"debug.ad9371-phy.adi,aux-dac-vref4",
	"debug.ad9371-phy.adi,aux-dac-value5",
	"debug.ad9371-phy.adi,aux-dac-slope5",
	"debug.ad9371-phy.adi,aux-dac-vref5",
	"debug.ad9371-phy.adi,aux-dac-value6",
	"debug.ad9371-phy.adi,aux-dac-slope6",
	"debug.ad9371-phy.adi,aux-dac-vref6",
	"debug.ad9371-phy.adi,aux-dac-value7",
	"debug.ad9371-phy.adi,aux-dac-slope7",
	"debug.ad9371-phy.adi,aux-dac-vref7",
	"debug.ad9371-phy.adi,aux-dac-value8",
	"debug.ad9371-phy.adi,aux-dac-slope8",
	"debug.ad9371-phy.adi,aux-dac-vref8",
	"debug.ad9371-phy.adi,aux-dac-value9",
	"debug.ad9371-phy.adi,aux-dac-slope9",
	"debug.ad9371-phy.adi,aux-dac-vref9",

	"debug.adi,default-initial-calibrations-mask",

	"debug.ad9371-phy.adi,dpd-damping",
	"debug.ad9371-phy.adi,dpd-num-weights",
	"debug.ad9371-phy.adi,dpd-model-version",
	"debug.ad9371-phy.adi,dpd-high-power-model-update",
	"debug.ad9371-phy.adi,dpd-model-prior-weight",
	"debug.ad9371-phy.adi,dpd-robust-modeling",
	"debug.ad9371-phy.adi,dpd-samples",
	"debug.ad9371-phy.adi,dpd-outlier-threshold",
	"debug.ad9371-phy.adi,dpd-additional-delay-offset",
	"debug.ad9371-phy.adi,dpd-path-delay-pn-seq-level",
	"debug.ad9371-phy.adi,dpd-weights0-real",
	"debug.ad9371-phy.adi,dpd-weights0-imag",
	"debug.ad9371-phy.adi,dpd-weights1-real",
	"debug.ad9371-phy.adi,dpd-weights1-imag",
	"debug.ad9371-phy.adi,dpd-weights2-real",
	"debug.ad9371-phy.adi,dpd-weights2-imag",

	"debug.ad9371-phy.adi,clgc-tx1-desired-gain",
	"debug.ad9371-phy.adi,clgc-tx2-desired-gain",
	"debug.ad9371-phy.adi,clgc-tx1-atten-limit",
	"debug.ad9371-phy.adi,clgc-tx2-atten-limit",
	"debug.ad9371-phy.adi,clgc-tx1-control-ratio",
	"debug.ad9371-phy.adi,clgc-tx2-control-ratio",
	"debug.ad9371-phy.adi,clgc-allow-tx1-atten-updates",
	"debug.ad9371-phy.adi,clgc-allow-tx2-atten-updates",
	"debug.ad9371-phy.adi,clgc-additional-delay-offset",
	"debug.ad9371-phy.adi,clgc-path-delay-pn-seq-level",
	"debug.ad9371-phy.adi,clgc-tx1-rel-threshold",
	"debug.ad9371-phy.adi,clgc-tx2-rel-threshold",
	"debug.ad9371-phy.adi,clgc-tx1-rel-threshold-en",
	"debug.ad9371-phy.adi,clgc-tx2-rel-threshold-en",

	"debug.ad9371-phy.adi,vswr-additional-delay-offset",
	"debug.ad9371-phy.adi,vswr-path-delay-pn-seq-level",
	"debug.ad9371-phy.adi,vswr-tx1-vswr-switch-gpio3p3-pin",
	"debug.ad9371-phy.adi,vswr-tx2-vswr-switch-gpio3p3-pin",
	"debug.ad9371-phy.adi,vswr-tx1-vswr-switch-polarity",
	"debug.ad9371-phy.adi,vswr-tx2-vswr-switch-polarity",
	"debug.ad9371-phy.adi,vswr-tx1-vswr-switch-delay_us",
	"debug.ad9371-phy.adi,vswr-tx2-vswr-switch-delay_us",
};


static void reload_settings(void)
{
	struct osc_plugin *plugin;
	GSList *node;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin && !strncmp(plugin->name, "ad9371", 12)) {
			if (plugin->handle_external_request) {
				g_usleep(1 * G_USEC_PER_SEC);
				plugin->handle_external_request(NULL, "Reload Settings");
			}
		}
	}
}

static void signal_handler_cb (GtkWidget *widget, gpointer data)
{
	struct w_info *item = data;
	long long val;
	char str[80];
	int bit, ret;
	long long mask;

	switch (item->type) {
		case CHECKBOX:
			val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (widget));
			break;
		case BUTTON:
			val = 1;
			break;
		case SPINBUTTON:
			val = (long long) gtk_spin_button_get_value(GTK_SPIN_BUTTON (widget));

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
			val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (widget));
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

static void bist_tone_cb (GtkWidget *widget, gpointer data)
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

	sprintf(temp, "%u %u %u", enable, tx1_freq, tx2_freq);

	iio_device_debug_attr_write(dev, "bist_tone", "0 0 0");
	iio_device_debug_attr_write(dev, "bist_tone", temp);
}

static char * set_widget_value(GtkWidget *widget, struct w_info *item, long long val)
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
	char *signal;
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

static void change_page_cb (GtkNotebook *notebook, GtkNotebookTab *page,
		     guint page_num, gpointer user_data)
{
	GtkWidget *tohide = user_data;

	if (page_num == 12)
		gtk_widget_hide(tohide); /* Hide Init button in BIST Tab */
	else
		gtk_widget_show(tohide);
}

static int handle_external_request (struct osc_plugin *plugin, const char *request)
{
	int ret = 0;

	if (!strcmp(request, "Trigger MCS")) {
		GtkWidget *mcs_btn;

		mcs_btn = GTK_WIDGET(gtk_builder_get_object(builder, "mcs_sync"));
		g_signal_emit_by_name(mcs_btn, "clicked", NULL);
		ret = 1;
	}

	return ret;
}

static int ad9371adv_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
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

static int ad9371adv_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			ad9371adv_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{

	update_from_ini(ini_fn, THIS_DRIVER, dev,
			ad9371_adv_sr_attribs,
			ARRAY_SIZE(ad9371_adv_sr_attribs));
	if (can_update_widgets)
		update_widgets(builder);

}

static GtkWidget * ad9371adv_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkWidget *ad9371adv_panel;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	dev = iio_context_find_device(ctx, PHY_DEVICE);

	if (ini_fn)
		load_profile(NULL, ini_fn);

	builder = gtk_builder_new();

	if (osc_load_glade_file(builder, "ad9371_adv") < 0)
		return NULL;

	ad9371adv_panel = GTK_WIDGET(gtk_builder_get_object(builder, "ad9371adv_panel"));
	nbook = GTK_NOTEBOOK(gtk_builder_get_object(builder, "notebook"));

	/* DPD, CLGC and VSWR is AD9375 only */
	if (iio_device_find_debug_attr(dev, "adi,dpd-model-version") == NULL) {
		gtk_widget_hide(gtk_notebook_get_nth_page(nbook, 3));
		gtk_widget_hide(gtk_notebook_get_nth_page(nbook, 4));
		gtk_widget_hide(gtk_notebook_get_nth_page(nbook, 5));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "adi,default-initial-calibrations-mask#15")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "adi,default-initial-calibrations-mask#16")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "adi,default-initial-calibrations-mask#17")));
	}

	connect_widgets(builder);

	g_builder_connect_signal(builder, "tx1_nco_freq", "value-changed",
		G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "tx2_nco_freq", "value-changed",
		G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "tx_nco_enable", "toggled",
		G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "notebook1", "switch-page",
		G_CALLBACK(change_page_cb),
		GTK_WIDGET(gtk_builder_get_object(builder, "initialize")));

	can_update_widgets = true;

	return ad9371adv_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		save_to_ini(f, THIS_DRIVER, dev, ad9371_adv_sr_attribs,
				ARRAY_SIZE(ad9371_adv_sr_attribs));
		fclose(f);
	}
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	save_profile(NULL, ini_fn);
	osc_destroy_context(ctx);
}

static bool ad9371adv_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	dev = iio_context_find_device(osc_ctx, PHY_DEVICE);

	return !!dev && iio_device_get_debug_attrs_count(dev);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = ad9371adv_identify,
	.init = ad9371adv_init,
	.handle_item = ad9371adv_handle,
	.handle_external_request = handle_external_request,
	.update_active_page = update_active_page,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
