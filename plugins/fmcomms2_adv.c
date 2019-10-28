/**
 * Copyright (C) 2013-2014 Analog Devices, Inc.
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

#include <ad9361.h>
#include <iio.h>

#include "../libini2.h"
#include "../osc.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../iio_widget.h"
#include "../datatypes.h"

#define PHY_DEVICE	"ad9361-phy"
#define PHY_SLAVE_DEVICE	"ad9361-phy-B"

#define CAP_DEVICE	"cf-ad9361-lpc"
#define CAP_DEVICE_ALT	"cf-ad9361-A"
#define CAP_SLAVE_DEVICE	"cf-ad9361-B"

#define DDS_DEVICE	"cf-ad9361-dds-core-lpc"
#define DDS_SLAVE_DEVICE	"cf-ad9361-dds-core-B"

#define THIS_DRIVER "AD936X Advanced"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

static struct iio_context *ctx;
static struct iio_device *dev;
static struct iio_device *dev_slave;
static struct iio_device *dev_dds_master;
static struct iio_device *dev_dds_slave;
struct iio_device *cf_ad9361_lpc, *cf_ad9361_hpc;

static struct iio_channel *dds_out[2][8];
OscPlot *plot_xcorr_4ch;
static volatile int auto_calibrate = 0;
static bool cap_device_channels_enabled;

static bool can_update_widgets;

static gint this_page;
static GtkNotebook *nbook;
static gboolean plugin_detached;
static GtkBuilder *builder;

/* 1MHZ tone */
#define CAL_TONE	1000000
#define CAL_SCALE	0.12500
#define MARKER_AVG	3

enum fmcomms2adv_wtype {
	CHECKBOX,
	SPINBUTTON,
	COMBOBOX,
	BUTTON,
	CHECKBOX_MASK,
	SPINBUTTON_S8,
};

struct w_info {
	enum fmcomms2adv_wtype type;
	const char * const name;
};

static struct w_info attrs[] = {
	{SPINBUTTON, "adi,agc-adc-large-overload-exceed-counter"},
	{SPINBUTTON, "adi,agc-adc-large-overload-inc-steps"},
	{CHECKBOX, "adi,agc-adc-lmt-small-overload-prevent-gain-inc-enable"},
	{SPINBUTTON, "adi,agc-adc-small-overload-exceed-counter"},
	{SPINBUTTON, "adi,agc-attack-delay-extra-margin-us"},
	{SPINBUTTON, "adi,agc-dig-gain-step-size"},
	{SPINBUTTON, "adi,agc-dig-saturation-exceed-counter"},
	{SPINBUTTON, "adi,agc-gain-update-interval-us"},
	{CHECKBOX, "adi,agc-immed-gain-change-if-large-adc-overload-enable"},
	{CHECKBOX, "adi,agc-immed-gain-change-if-large-lmt-overload-enable"},
	{SPINBUTTON, "adi,agc-inner-thresh-high"},
	{SPINBUTTON, "adi,agc-inner-thresh-high-dec-steps"},
	{SPINBUTTON, "adi,agc-inner-thresh-low"},
	{SPINBUTTON, "adi,agc-inner-thresh-low-inc-steps"},
	{SPINBUTTON, "adi,agc-lmt-overload-large-exceed-counter"},
	{SPINBUTTON, "adi,agc-lmt-overload-large-inc-steps"},
	{SPINBUTTON, "adi,agc-lmt-overload-small-exceed-counter"},
	{SPINBUTTON, "adi,agc-outer-thresh-high"},
	{SPINBUTTON, "adi,agc-outer-thresh-high-dec-steps"},
	{SPINBUTTON, "adi,agc-outer-thresh-low"},
	{SPINBUTTON, "adi,agc-outer-thresh-low-inc-steps"},
	{CHECKBOX, "adi,agc-sync-for-gain-counter-enable"},
	{SPINBUTTON, "adi,aux-adc-decimation"},
	{SPINBUTTON, "adi,aux-adc-rate"},
	{COMBOBOX, "adi,clk-output-mode-select"},
	{SPINBUTTON, "adi,ctrl-outs-enable-mask"},
	{SPINBUTTON, "adi,ctrl-outs-index"},
	{SPINBUTTON, "adi,dc-offset-attenuation-high-range"},
	{SPINBUTTON, "adi,dc-offset-attenuation-low-range"},
	{SPINBUTTON, "adi,dc-offset-count-high-range"},
	{SPINBUTTON, "adi,dc-offset-count-low-range"},
	{SPINBUTTON, "adi,dc-offset-tracking-update-event-mask"},
	{SPINBUTTON, "adi,elna-bypass-loss-mdB"},
	{SPINBUTTON, "adi,elna-gain-mdB"},
	{CHECKBOX, "adi,elna-rx1-gpo0-control-enable"},
	{CHECKBOX, "adi,elna-rx2-gpo1-control-enable"},
	{SPINBUTTON, "adi,elna-settling-delay-ns"},
	{CHECKBOX, "adi,elna-gaintable-all-index-enable"},
	{CHECKBOX, "adi,ensm-enable-pin-pulse-mode-enable"},
	{CHECKBOX, "adi,ensm-enable-txnrx-control-enable"},
	{CHECKBOX, "adi,external-rx-lo-enable"},
	{CHECKBOX, "adi,external-tx-lo-enable"},
	{CHECKBOX, "adi,frequency-division-duplex-mode-enable"},
	{SPINBUTTON, "adi,gc-adc-large-overload-thresh"},
	{SPINBUTTON, "adi,gc-adc-ovr-sample-size"},
	{SPINBUTTON, "adi,gc-adc-small-overload-thresh"},
	{SPINBUTTON, "adi,gc-dec-pow-measurement-duration"},
	{CHECKBOX, "adi,gc-use-rx-fir-out-for-dec-pwr-meas-enable"},
	{CHECKBOX, "adi,gc-dig-gain-enable"},
	{SPINBUTTON, "adi,gc-lmt-overload-high-thresh"},
	{SPINBUTTON, "adi,gc-lmt-overload-low-thresh"},
	{SPINBUTTON, "adi,gc-low-power-thresh"},
	{SPINBUTTON, "adi,gc-max-dig-gain"},
	{COMBOBOX, "adi,gc-rx1-mode"},
	{COMBOBOX, "adi,gc-rx2-mode"},
	{SPINBUTTON, "adi,mgc-dec-gain-step"},
	{SPINBUTTON, "adi,mgc-inc-gain-step"},
	{CHECKBOX, "adi,mgc-rx1-ctrl-inp-enable"},
	{CHECKBOX, "adi,mgc-rx2-ctrl-inp-enable"},
	{COMBOBOX, "adi,mgc-split-table-ctrl-inp-gain-mode"},
	{SPINBUTTON, "adi,rssi-delay"},
	{SPINBUTTON, "adi,rssi-duration"},
	{COMBOBOX, "adi,rssi-restart-mode"},
	{SPINBUTTON, "adi,rssi-wait"},
	{COMBOBOX, "adi,rx-rf-port-input-select"},
	{COMBOBOX, "adi,split-gain-table-mode-enable"},
	{CHECKBOX, "adi,tdd-skip-vco-cal-enable"},
	{CHECKBOX, "adi,tdd-use-dual-synth-mode-enable"},
	{CHECKBOX, "adi,tdd-use-fdd-vco-tables-enable"},
	{SPINBUTTON, "adi,temp-sense-decimation"},
	{SPINBUTTON, "adi,temp-sense-measurement-interval-ms"},
	{SPINBUTTON_S8, "adi,temp-sense-offset-signed"},
	{CHECKBOX, "adi,temp-sense-periodic-measurement-enable"},
	{COMBOBOX, "adi,tx-rf-port-input-select"},
	{CHECKBOX, "adi,update-tx-gain-in-alert-enable"},
	{CHECKBOX, "adi,xo-disable-use-ext-refclk-enable"},
	{SPINBUTTON, "adi,fagc-dec-pow-measurement-duration"},
	{CHECKBOX, "adi,fagc-allow-agc-gain-increase-enable"},
	{SPINBUTTON, "adi,fagc-energy-lost-stronger-sig-gain-lock-exit-cnt"},
	{SPINBUTTON, "adi,fagc-final-overrange-count"},
	{CHECKBOX, "adi,fagc-gain-increase-after-gain-lock-enable"},
	{COMBOBOX, "adi,fagc-gain-index-type-after-exit-rx-mode"},
	{SPINBUTTON, "adi,fagc-lmt-final-settling-steps"},
	{SPINBUTTON, "adi,fagc-lock-level-gain-increase-upper-limit"},
	{CHECKBOX, "adi,fagc-lock-level-lmt-gain-increase-enable"},
	{SPINBUTTON, "adi,fagc-lp-thresh-increment-steps"},
	{SPINBUTTON, "adi,fagc-lp-thresh-increment-time"},
	{SPINBUTTON, "adi,fagc-lpf-final-settling-steps"},
	{SPINBUTTON, "adi,fagc-optimized-gain-offset"},
	{SPINBUTTON, "adi,fagc-power-measurement-duration-in-state5"},
	{CHECKBOX, "adi,fagc-rst-gla-en-agc-pulled-high-enable"},
	{COMBOBOX, "adi,fagc-rst-gla-engergy-lost-goto-optim-gain-enable"},
	{SPINBUTTON, "adi,fagc-rst-gla-engergy-lost-sig-thresh-below-ll"},
	{CHECKBOX, "adi,fagc-rst-gla-engergy-lost-sig-thresh-exceeded-enable"},
	{COMBOBOX, "adi,fagc-rst-gla-if-en-agc-pulled-high-mode"},
	{CHECKBOX, "adi,fagc-rst-gla-large-adc-overload-enable"},
	{CHECKBOX, "adi,fagc-rst-gla-large-lmt-overload-enable"},
	{SPINBUTTON, "adi,fagc-rst-gla-stronger-sig-thresh-above-ll"},
	{CHECKBOX, "adi,fagc-rst-gla-stronger-sig-thresh-exceeded-enable"},
	{SPINBUTTON, "adi,fagc-state-wait-time-ns"},
	{COMBOBOX, "adi,fagc-use-last-lock-level-for-set-gain-enable"},
	{CHECKBOX, "adi,aux-dac-manual-mode-enable"},
	{CHECKBOX, "adi,aux-dac1-active-in-alert-enable"},
	{CHECKBOX, "adi,aux-dac1-active-in-rx-enable"},
	{CHECKBOX, "adi,aux-dac1-active-in-tx-enable"},
	{SPINBUTTON, "adi,aux-dac1-default-value-mV"},
	{SPINBUTTON, "adi,aux-dac1-rx-delay-us"},
	{SPINBUTTON, "adi,aux-dac1-tx-delay-us"},
	{CHECKBOX, "adi,aux-dac2-active-in-alert-enable"},
	{CHECKBOX, "adi,aux-dac2-active-in-rx-enable"},
	{CHECKBOX, "adi,aux-dac2-active-in-tx-enable"},
	{SPINBUTTON, "adi,aux-dac2-default-value-mV"},
	{SPINBUTTON, "adi,aux-dac2-rx-delay-us"},
	{SPINBUTTON, "adi,aux-dac2-tx-delay-us"},
	{SPINBUTTON, "adi,rx-fastlock-delay-ns"},
	{SPINBUTTON, "adi,tx-fastlock-delay-ns"},
	{CHECKBOX, "adi,rx-fastlock-pincontrol-enable"},
	{CHECKBOX, "adi,tx-fastlock-pincontrol-enable"},
	{COMBOBOX, "adi,txmon-1-front-end-gain"},
	{SPINBUTTON, "adi,txmon-1-lo-cm"},
	{COMBOBOX, "adi,txmon-2-front-end-gain"},
	{SPINBUTTON, "adi,txmon-2-lo-cm"},
	{CHECKBOX, "adi,txmon-dc-tracking-enable"},
	{SPINBUTTON, "adi,txmon-delay"},
	{SPINBUTTON, "adi,txmon-duration"},
	{SPINBUTTON, "adi,txmon-high-gain"},
	{SPINBUTTON, "adi,txmon-low-gain"},
	{SPINBUTTON, "adi,txmon-low-high-thresh"},
	{CHECKBOX, "adi,txmon-one-shot-mode-enable"},
	{CHECKBOX, "adi,rx1-rx2-phase-inversion-enable"},
	{CHECKBOX, "adi,qec-tracking-slow-mode-enable"},
	{CHECKBOX, "adi,gpo0-inactive-state-high-enable"},
	{CHECKBOX, "adi,gpo0-slave-rx-enable"},
	{CHECKBOX, "adi,gpo0-slave-tx-enable"},
	{SPINBUTTON, "adi,gpo0-rx-delay-us"},
	{SPINBUTTON, "adi,gpo0-tx-delay-us"},
	{CHECKBOX, "adi,gpo1-inactive-state-high-enable"},
	{CHECKBOX, "adi,gpo1-slave-rx-enable"},
	{CHECKBOX, "adi,gpo1-slave-tx-enable"},
	{SPINBUTTON, "adi,gpo1-rx-delay-us"},
	{SPINBUTTON, "adi,gpo1-tx-delay-us"},
	{CHECKBOX, "adi,gpo2-inactive-state-high-enable"},
	{CHECKBOX, "adi,gpo2-slave-rx-enable"},
	{CHECKBOX, "adi,gpo2-slave-tx-enable"},
	{SPINBUTTON, "adi,gpo2-rx-delay-us"},
	{SPINBUTTON, "adi,gpo2-tx-delay-us"},
	{CHECKBOX, "adi,gpo3-inactive-state-high-enable"},
	{CHECKBOX, "adi,gpo3-slave-rx-enable"},
	{CHECKBOX, "adi,gpo3-slave-tx-enable"},
	{SPINBUTTON, "adi,gpo3-rx-delay-us"},
	{SPINBUTTON, "adi,gpo3-tx-delay-us"},
	{CHECKBOX, "adi,gpo-manual-mode-enable"},
	{CHECKBOX_MASK, "adi,gpo-manual-mode-enable-mask#0"},
	{CHECKBOX_MASK, "adi,gpo-manual-mode-enable-mask#1"},
	{CHECKBOX_MASK, "adi,gpo-manual-mode-enable-mask#2"},
	{CHECKBOX_MASK, "adi,gpo-manual-mode-enable-mask#3"},
	{COMBOBOX, "bist_prbs"},
	{COMBOBOX, "loopback"},
	{BUTTON, "initialize"},
};

static const char *fmcomms2_adv_sr_attribs[] = {
	"debug.ad9361-phy.adi,agc-adc-large-overload-exceed-counter",
	"debug.ad9361-phy.adi,agc-adc-large-overload-inc-steps",
	"debug.ad9361-phy.adi,agc-adc-lmt-small-overload-prevent-gain-inc-enable",
	"debug.ad9361-phy.adi,agc-adc-small-overload-exceed-counter",
	"debug.ad9361-phy.adi,agc-attack-delay-extra-margin-us",
	"debug.ad9361-phy.adi,agc-dig-gain-step-size",
	"debug.ad9361-phy.adi,agc-dig-saturation-exceed-counter",
	"debug.ad9361-phy.adi,agc-gain-update-interval-us",
	"debug.ad9361-phy.adi,agc-immed-gain-change-if-large-adc-overload-enable",
	"debug.ad9361-phy.adi,agc-immed-gain-change-if-large-lmt-overload-enable",
	"debug.ad9361-phy.adi,agc-inner-thresh-high",
	"debug.ad9361-phy.adi,agc-inner-thresh-high-dec-steps",
	"debug.ad9361-phy.adi,agc-inner-thresh-low",
	"debug.ad9361-phy.adi,agc-inner-thresh-low-inc-steps",
	"debug.ad9361-phy.adi,agc-lmt-overload-large-exceed-counter",
	"debug.ad9361-phy.adi,agc-lmt-overload-large-inc-steps",
	"debug.ad9361-phy.adi,agc-lmt-overload-small-exceed-counter",
	"debug.ad9361-phy.adi,agc-outer-thresh-high",
	"debug.ad9361-phy.adi,agc-outer-thresh-high-dec-steps",
	"debug.ad9361-phy.adi,agc-outer-thresh-low",
	"debug.ad9361-phy.adi,agc-outer-thresh-low-inc-steps",
	"debug.ad9361-phy.adi,agc-sync-for-gain-counter-enable",
	"debug.ad9361-phy.adi,aux-adc-decimation",
	"debug.ad9361-phy.adi,aux-adc-rate",
	"debug.ad9361-phy.adi,clk-output-mode-select",
	"debug.ad9361-phy.adi,ctrl-outs-enable-mask",
	"debug.ad9361-phy.adi,ctrl-outs-index",
	"debug.ad9361-phy.adi,dc-offset-attenuation-high-range",
	"debug.ad9361-phy.adi,dc-offset-attenuation-low-range",
	"debug.ad9361-phy.adi,dc-offset-count-high-range",
	"debug.ad9361-phy.adi,dc-offset-count-low-range",
	"debug.ad9361-phy.adi,dc-offset-tracking-update-event-mask",
	"debug.ad9361-phy.adi,elna-bypass-loss-mdB",
	"debug.ad9361-phy.adi,elna-gain-mdB",
	"debug.ad9361-phy.adi,elna-rx1-gpo0-control-enable",
	"debug.ad9361-phy.adi,elna-rx2-gpo1-control-enable",
	"debug.ad9361-phy.adi,elna-settling-delay-ns",
	"debug.ad9361-phy.adi,elna-gaintable-all-index-enable",
	"debug.ad9361-phy.adi,ensm-enable-pin-pulse-mode-enable",
	"debug.ad9361-phy.adi,ensm-enable-txnrx-control-enable",
	"debug.ad9361-phy.adi,external-rx-lo-enable",
	"debug.ad9361-phy.adi,external-tx-lo-enable",
	"debug.ad9361-phy.adi,frequency-division-duplex-mode-enable",
	"debug.ad9361-phy.adi,gc-adc-large-overload-thresh",
	"debug.ad9361-phy.adi,gc-adc-ovr-sample-size",
	"debug.ad9361-phy.adi,gc-adc-small-overload-thresh",
	"debug.ad9361-phy.adi,gc-dec-pow-measurement-duration",
	"debug.ad9361-phy.adi,gc-use-rx-fir-out-for-dec-pwr-meas-enable",
	"debug.ad9361-phy.adi,gc-dig-gain-enable",
	"debug.ad9361-phy.adi,gc-lmt-overload-high-thresh",
	"debug.ad9361-phy.adi,gc-lmt-overload-low-thresh",
	"debug.ad9361-phy.adi,gc-low-power-thresh",
	"debug.ad9361-phy.adi,gc-max-dig-gain",
	"debug.ad9361-phy.adi,gc-rx1-mode",
	"debug.ad9361-phy.adi,gc-rx2-mode",
	"debug.ad9361-phy.adi,mgc-dec-gain-step",
	"debug.ad9361-phy.adi,mgc-inc-gain-step",
	"debug.ad9361-phy.adi,mgc-rx1-ctrl-inp-enable",
	"debug.ad9361-phy.adi,mgc-rx2-ctrl-inp-enable",
	"debug.ad9361-phy.adi,mgc-split-table-ctrl-inp-gain-mode",
	"debug.ad9361-phy.adi,rssi-delay",
	"debug.ad9361-phy.adi,rssi-duration",
	"debug.ad9361-phy.adi,rssi-restart-mode",
	"debug.ad9361-phy.adi,rssi-wait",
	"debug.ad9361-phy.adi,rx-rf-port-input-select",
	"debug.ad9361-phy.adi,split-gain-table-mode-enable",
	"debug.ad9361-phy.adi,tdd-skip-vco-cal-enable",
	"debug.ad9361-phy.adi,tdd-use-dual-synth-mode-enable",
	"debug.ad9361-phy.adi,tdd-use-fdd-vco-tables-enable",
	"debug.ad9361-phy.adi,temp-sense-decimation",
	"debug.ad9361-phy.adi,temp-sense-measurement-interval-ms",
	"debug.ad9361-phy.adi,temp-sense-offset-signed",
	"debug.ad9361-phy.adi,temp-sense-periodic-measurement-enable",
	"debug.ad9361-phy.adi,tx-rf-port-input-select",
	"debug.ad9361-phy.adi,update-tx-gain-in-alert-enable",
	"debug.ad9361-phy.adi,xo-disable-use-ext-refclk-enable",
	"debug.ad9361-phy.adi,fagc-dec-pow-measurement-duration",
	"debug.ad9361-phy.adi,fagc-allow-agc-gain-increase-enable",
	"debug.ad9361-phy.adi,fagc-energy-lost-stronger-sig-gain-lock-exit-cnt",
	"debug.ad9361-phy.adi,fagc-final-overrange-count",
	"debug.ad9361-phy.adi,fagc-gain-increase-after-gain-lock-enable",
	"debug.ad9361-phy.adi,fagc-gain-index-type-after-exit-rx-mode",
	"debug.ad9361-phy.adi,fagc-lmt-final-settling-steps",
	"debug.ad9361-phy.adi,fagc-lock-level-gain-increase-upper-limit",
	"debug.ad9361-phy.adi,fagc-lock-level-lmt-gain-increase-enable",
	"debug.ad9361-phy.adi,fagc-lp-thresh-increment-steps",
	"debug.ad9361-phy.adi,fagc-lp-thresh-increment-time",
	"debug.ad9361-phy.adi,fagc-lpf-final-settling-steps",
	"debug.ad9361-phy.adi,fagc-optimized-gain-offset",
	"debug.ad9361-phy.adi,fagc-power-measurement-duration-in-state5",
	"debug.ad9361-phy.adi,fagc-rst-gla-en-agc-pulled-high-enable",
	"debug.ad9361-phy.adi,fagc-rst-gla-engergy-lost-goto-optim-gain-enable",
	"debug.ad9361-phy.adi,fagc-rst-gla-engergy-lost-sig-thresh-below-ll",
	"debug.ad9361-phy.adi,fagc-rst-gla-engergy-lost-sig-thresh-exceeded-enable",
	"debug.ad9361-phy.adi,fagc-rst-gla-if-en-agc-pulled-high-mode",
	"debug.ad9361-phy.adi,fagc-rst-gla-large-adc-overload-enable",
	"debug.ad9361-phy.adi,fagc-rst-gla-large-lmt-overload-enable",
	"debug.ad9361-phy.adi,fagc-rst-gla-stronger-sig-thresh-above-ll",
	"debug.ad9361-phy.adi,fagc-rst-gla-stronger-sig-thresh-exceeded-enable",
	"debug.ad9361-phy.adi,fagc-state-wait-time-ns",
	"debug.ad9361-phy.adi,fagc-use-last-lock-level-for-set-gain-enable",
	"debug.ad9361-phy.adi,aux-dac-manual-mode-enable",
	"debug.ad9361-phy.adi,aux-dac1-active-in-alert-enable",
	"debug.ad9361-phy.adi,aux-dac1-active-in-rx-enable",
	"debug.ad9361-phy.adi,aux-dac1-active-in-tx-enable",
	"debug.ad9361-phy.adi,aux-dac1-default-value-mV",
	"debug.ad9361-phy.adi,aux-dac1-rx-delay-us",
	"debug.ad9361-phy.adi,aux-dac1-tx-delay-us",
	"debug.ad9361-phy.adi,aux-dac2-active-in-alert-enable",
	"debug.ad9361-phy.adi,aux-dac2-active-in-rx-enable",
	"debug.ad9361-phy.adi,aux-dac2-active-in-tx-enable",
	"debug.ad9361-phy.adi,aux-dac2-default-value-mV",
	"debug.ad9361-phy.adi,aux-dac2-rx-delay-us",
	"debug.ad9361-phy.adi,aux-dac2-tx-delay-us",
	"debug.ad9361-phy.adi,rx-fastlock-delay-ns",
	"debug.ad9361-phy.adi,tx-fastlock-delay-ns",
	"debug.ad9361-phy.adi,rx-fastlock-pincontrol-enable",
	"debug.ad9361-phy.adi,tx-fastlock-pincontrol-enable",
	"debug.ad9361-phy.adi,txmon-1-front-end-gain",
	"debug.ad9361-phy.adi,txmon-1-lo-cm",
	"debug.ad9361-phy.adi,txmon-2-front-end-gain",
	"debug.ad9361-phy.adi,txmon-2-lo-cm",
	"debug.ad9361-phy.adi,txmon-dc-tracking-enable",
	"debug.ad9361-phy.adi,txmon-delay",
	"debug.ad9361-phy.adi,txmon-duration",
	"debug.ad9361-phy.adi,txmon-high-gain",
	"debug.ad9361-phy.adi,txmon-low-gain",
	"debug.ad9361-phy.adi,txmon-low-high-thresh",
	"debug.ad9361-phy.adi,txmon-one-shot-mode-enable",
	"debug.ad9361-phy.adi,qec-tracking-slow-mode-enable",
	"debug.ad9361-phy.adi,gpo0-inactive-state-high-enable",
	"debug.ad9361-phy.adi,gpo0-slave-rx-enable",
	"debug.ad9361-phy.adi,gpo0-slave-tx-enable",
	"debug.ad9361-phy.adi,gpo0-rx-delay-us",
	"debug.ad9361-phy.adi,gpo0-tx-delay-us",
	"debug.ad9361-phy.adi,gpo1-inactive-state-high-enable",
	"debug.ad9361-phy.adi,gpo1-slave-rx-enable",
	"debug.ad9361-phy.adi,gpo1-slave-tx-enable",
	"debug.ad9361-phy.adi,gpo1-rx-delay-us",
	"debug.ad9361-phy.adi,gpo1-tx-delay-us",
	"debug.ad9361-phy.adi,gpo2-inactive-state-high-enable",
	"debug.ad9361-phy.adi,gpo2-slave-rx-enable",
	"debug.ad9361-phy.adi,gpo2-slave-tx-enable",
	"debug.ad9361-phy.adi,gpo2-rx-delay-us",
	"debug.ad9361-phy.adi,gpo2-tx-delay-us",
	"debug.ad9361-phy.adi,gpo3-inactive-state-high-enable",
	"debug.ad9361-phy.adi,gpo3-slave-rx-enable",
	"debug.ad9361-phy.adi,gpo3-slave-tx-enable",
	"debug.ad9361-phy.adi,gpo3-rx-delay-us",
	"debug.ad9361-phy.adi,gpo3-tx-delay-us",
	"debug.ad9361-phy.adi,gpo-manual-mode-enable",
	"debug.ad9361-phy.adi,gpo-manual-mode-enable-mask",
};

static void reload_settings(void)
{
	struct osc_plugin *plugin;
	GSList *node;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin && (!strncmp(plugin->name, "FMComms2/3/4", 12) ||
			!strncmp(plugin->name, "FMComms5-", 8))) {
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
	unsigned val;
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
		case SPINBUTTON_S8:
			val = (unsigned) gtk_spin_button_get_value(GTK_SPIN_BUTTON (widget));
			break;
		case COMBOBOX:
			val = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
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

			if (dev_slave)
				iio_device_debug_attr_write_longlong(dev_slave, str, mask);
			return;
		default:
			return;
	}

	iio_device_debug_attr_write_longlong(dev, item->name, val);

	if (dev_slave)
		iio_device_debug_attr_write_longlong(dev_slave, item->name, val);

	if (!strcmp(item->name, "initialize")) {
		reload_settings();
	}
}

static void iio_channels_change_shadow_of_enabled(struct iio_device *dev, bool increment)
{
	struct iio_channel *chn;
	struct extra_info *info;
	unsigned i;
	for (i = 0; i < iio_device_get_channels_count(dev); i++) {
		chn = iio_device_get_channel(dev, i);
		info = iio_channel_get_data(chn);
		if (increment)
			info->shadow_of_enabled++;
		else
			info->shadow_of_enabled--;
	}
}

static double scale_phase_0_360(double val)
{
	if (val >= 360.0)
		val -= 360.0;

	if (val < 0)
		val += 360.0;

	return val;
}

static double calc_phase_offset(double fsample, double dds_freq, double offset, double mag)
{
	double val = 360.0 / ((fsample / dds_freq) / offset);

	if (mag < 0)
		val += 180.0;

	return scale_phase_0_360(val);
}

static void trx_phase_rotation(struct iio_device *dev, gdouble val)
{
	struct iio_channel *out0, *out1;
	gdouble phase, vcos, vsin;
	unsigned offset;

	bool output = (dev == dev_dds_slave) || (dev == dev_dds_master);

	DBG("%s %f", iio_device_get_name(dev), val);

	phase = val * 2 * M_PI / 360.0;

	vcos = cos(phase);
	vsin = sin(phase);

	if (output)  {
		gdouble corr;
		corr = 1.0 / fmax(fabs(sin(phase) + cos(phase)),
				  fabs(cos(phase) - sin(phase)));
		vcos *= corr;
		vsin *= corr;
	}

	/* Set both RX1 and RX2 */
	for (offset = 0; offset <= 2; offset += 2) {
		if (offset == 2) {
			out0 = iio_device_find_channel(dev, "voltage2", output);
			out1 = iio_device_find_channel(dev, "voltage3", output);
		} else {
			out0 = iio_device_find_channel(dev, "voltage0", output);
			out1 = iio_device_find_channel(dev, "voltage1", output);
		}

		if (out1 && out0) {
			iio_channel_attr_write_double(out0, "calibscale", (double) vcos);
			iio_channel_attr_write_double(out0, "calibphase", (double) (-1.0 * vsin));
			iio_channel_attr_write_double(out1, "calibscale", (double) vcos);
			iio_channel_attr_write_double(out1, "calibphase", (double) vsin);
		}
	}
}

static void dds_tx_phase_rotation(struct iio_device *dev, gdouble val)
{
	long long i, q;
	int d, j;

	if (dev == dev_dds_slave)
		d = 1;
	else
		d = 0;

	i = scale_phase_0_360(val + 90.0) * 1000;
	q = scale_phase_0_360(val) * 1000;


	DBG("Val %f, I = %d, Q = %d", val, (int)i,(int) q);

	for (j = 0; j < 8; j++) {
		switch (j) {
			case 0:
			case 1:
			case 4:
			case 5:
				iio_channel_attr_write_longlong(dds_out[d][j], "phase", i);
				break;
			default:
				iio_channel_attr_write_longlong(dds_out[d][j], "phase", q);
		}
	}

}

static int default_dds(long long freq, double scale)
{
	int i, j, ret = 0;

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 8; j++) {
			ret |= iio_channel_attr_write_longlong(dds_out[i][j], "frequency", freq);
			ret |= iio_channel_attr_write_double(dds_out[i][j], "scale", scale);
		}

		dds_tx_phase_rotation(i ? dev_dds_slave : dev_dds_master, 0.0);
		trx_phase_rotation(i ? dev_dds_slave : dev_dds_master, 0.0);

	}

	return ret;
}

static void near_end_loopback_ctrl(unsigned channel, bool enable)
{

	unsigned tmp;
	struct iio_device *dev = (channel > 3) ?
		cf_ad9361_lpc : cf_ad9361_hpc;
	if (!dev)
		return;

	if (channel > 3)
		channel -= 4;

	if (iio_device_reg_read(dev, 0x80000418 + channel * 0x40, &tmp))
		return;

	if (enable)
		tmp |= 0x1;
	else
		tmp &= ~0xF;

	iio_device_reg_write(dev, 0x80000418 + channel * 0x40, tmp);

}

static void get_markers(double *offset, double *mag)
{
	int ret, sum = MARKER_AVG;
	struct marker_type *markers = NULL;
	const char *device_ref;

	device_ref = plugin_get_device_by_reference(CAP_DEVICE_ALT);

	*offset = 0;
	*mag = 0;

	for (sum = 0; sum < MARKER_AVG; sum++) {
		if (device_ref) {
			do {
				ret = plugin_data_capture_of_plot(plot_xcorr_4ch,
						device_ref, NULL, &markers);
			} while (ret == -EBUSY);
		}

		if (markers) {
			*offset += markers[0].x;
			*mag += markers[0].y;
		}
	}

	*offset /= MARKER_AVG;
	*mag /= MARKER_AVG;


	DBG("offset: %f, MAG0 %f", *offset, *mag);

	plugin_data_capture_of_plot(plot_xcorr_4ch, NULL, NULL, &markers);
}


static void __cal_switch_ports_enable_cb (unsigned val)
{
	unsigned lp_slave, lp_master, sw;
	char *rx_port, *tx_port;

	/*
	*  0 DISABLE
	*  1 TX1B_B (HPC) -> RX1C_B (HPC) : BIST_LOOPBACK on A
	*  2 TX1B_A (LPC) -> RX1C_B (HPC) : BIST_LOOPBACK on A
	*  3 TX1B_B (HPC) -> RX1C_A (LPC) : BIST_LOOPBACK on B
	*  4 TX1B_A (LPC) -> RX1C_A (LPC) : BIST_LOOPBACK on B
	*
	*/
	switch (val) {
	default:
	case 0:
		lp_slave = 0;
		lp_master = 0;
		sw = 0;
		tx_port = "A";
		rx_port = "A_BALANCED";
		break;
	case 1:
	case 2:
		lp_slave = 0;
		lp_master = 1;
		sw = val - 1;
		tx_port = "B";
		rx_port = "C_BALANCED";
		break;
	case 3:
	case 4:
		lp_slave = 1;
		lp_master = 0;
		sw = val - 1;
		tx_port = "B";
		rx_port = "C_BALANCED";
		break;
	}


#if 0
	iio_device_debug_attr_write_bool(dev, "loopback", lp_master);
	iio_device_debug_attr_write_bool(dev_slave, "loopback", lp_slave);
#else
	near_end_loopback_ctrl(0, lp_slave); /* HPC */
	near_end_loopback_ctrl(1, lp_slave); /* HPC */

	near_end_loopback_ctrl(4, lp_master); /* LPC */
	near_end_loopback_ctrl(5, lp_master); /* LPC */
#endif
	iio_device_debug_attr_write_longlong(dev, "calibration_switch_control", sw);
	iio_channel_attr_write(iio_device_find_channel(dev, "voltage0", false),
			       "rf_port_select", rx_port);
	iio_channel_attr_write(iio_device_find_channel(dev, "voltage0", true),
			       "rf_port_select", tx_port);

	if (dev_slave) {
		iio_channel_attr_write(iio_device_find_channel(dev_slave, "voltage0", false),
				"rf_port_select", rx_port);
		iio_channel_attr_write(iio_device_find_channel(dev_slave, "voltage0", true),
				"rf_port_select", tx_port);
	}

	return;

}

static void cal_switch_ports_enable_cb (GtkWidget *widget, gpointer data)
{
	__cal_switch_ports_enable_cb(gtk_combo_box_get_active(GTK_COMBO_BOX(widget)));
}

static void mcs_cb (GtkWidget *widget, gpointer data)
{
	ad9361_multichip_sync(dev, &dev_slave, 1,
			FIXUP_INTERFACE_TIMING | CHECK_SAMPLE_RATES);
}

static double tune_trx_phase_offset(struct iio_device *ldev, int *ret,
			long long cal_freq, long long cal_tone,
			double sign, double abort,
			void (*tune)(struct iio_device *, gdouble))
{
	int i;
	double offset, mag;
	double phase = 0.0, increment;

	for (i = 0; i < 10; i++) {

		get_markers(&offset, &mag);
		get_markers(&offset, &mag);

		increment = calc_phase_offset(cal_freq, cal_tone, offset, mag);
		increment *= sign;

		phase += increment;

		phase = scale_phase_0_360(phase);
		tune(ldev, phase);

		DBG("Step: %i increment %f Phase: %f\n", i, increment, phase);

		if (fabs(offset) < 0.001)
			break;
	}

	if (fabs(offset) > 0.1)
		*ret = -EFAULT;
	else
		*ret = 0;

	return phase * sign;
}

static unsigned get_cal_tone(void)
{
	unsigned freq;
	const char *cal_tone = getenv("CAL_TONE");

	if (!cal_tone)
		return CAL_TONE;

	freq = atoi(cal_tone);

	if (freq > 0 && freq < 31000000)
		return freq;

	return CAL_TONE;
}

static int get_cal_samples(long long cal_tone, long long cal_freq)
{
	int samples, env_samples;
	const char *cal_samples = getenv("CAL_SAMPLES");

	samples = exp2(ceil(log2(cal_freq/cal_tone)) + 2);

	if (!cal_samples)
		return samples;

	env_samples = atoi(cal_samples);

	if (env_samples < samples)
		return samples;

	return env_samples;
}

static void set_calibration_progress(GtkProgressBar *pbar, float fraction)
{
	if (gtk_widget_get_visible(GTK_WIDGET(pbar))) {
		char ptext[64];

		gdk_threads_enter();
		snprintf(ptext, sizeof(ptext), "Calibration Progress (%.2f %%)", fraction * 100);
		gtk_progress_bar_set_text(pbar, ptext);
		gtk_progress_bar_set_fraction(pbar, fraction);
		gdk_threads_leave();
	}
}

static void calibrate (gpointer button)
{
	GtkProgressBar *calib_progress = NULL;
	double rx_phase_lpc, rx_phase_hpc, tx_phase_hpc;
	struct iio_channel *in0, *in0_slave;
	long long cal_tone, cal_freq;
	int ret, samples;

	in0 = iio_device_find_channel(dev, "voltage0", false);
	in0_slave = iio_device_find_channel(dev_slave, "voltage0", false);
	if (!in0 || !in0_slave) {
		printf("could not find channels\n");
		ret = -ENODEV;
		goto calibrate_fail;
	}

	if (!cf_ad9361_lpc || !cf_ad9361_hpc) {
		printf("could not find capture cores\n");
		ret = -ENODEV;
		goto calibrate_fail;
	}

	if (!dev_dds_master || !dev_dds_slave) {
		printf("could not find dds cores\n");
		ret = -ENODEV;
		goto calibrate_fail;
	}

	calib_progress = GTK_PROGRESS_BAR(gtk_builder_get_object(builder, "progress_calibration"));
	set_calibration_progress(calib_progress, 0.00);

	mcs_cb(NULL, NULL);

	/*
	 * set some logical defaults / assumptions
	 */

	ret = default_dds(get_cal_tone(), CAL_SCALE);
	if (ret < 0) {
		printf("could not set dds cores\n");
		goto calibrate_fail;
	}

	iio_channel_attr_read_longlong(dds_out[0][0], "frequency", &cal_tone);
	iio_channel_attr_read_longlong(dds_out[0][0], "sampling_frequency", &cal_freq);

	samples = get_cal_samples(cal_tone, cal_freq);

	DBG("cal_tone %lld cal_freq %lld samples %d", cal_tone, cal_freq, samples);

	gdk_threads_enter();
	osc_plot_set_sample_count(plot_xcorr_4ch, samples);
	osc_plot_draw_start(plot_xcorr_4ch);
	gdk_threads_leave();

	/* Turn off quadrature tracking while the sync is going on */
	iio_channel_attr_write(in0, "quadrature_tracking_en", "0");
	iio_channel_attr_write(in0_slave, "quadrature_tracking_en", "0");

	/* reset any Tx rotation to zero */
	trx_phase_rotation(cf_ad9361_lpc, 0.0);
	trx_phase_rotation(cf_ad9361_hpc, 0.0);
	set_calibration_progress(calib_progress, 0.16);

	/*
	 * Calibrate RX:
	 * 1 TX1B_B (HPC) -> RX1C_B (HPC) : BIST_LOOPBACK on A
	 */
	osc_plot_xcorr_revert(plot_xcorr_4ch, true);
	__cal_switch_ports_enable_cb(1);
	rx_phase_hpc = tune_trx_phase_offset(cf_ad9361_hpc, &ret, cal_freq, cal_tone, 1.0, 0.01, trx_phase_rotation);
	if (ret < 0) {
		printf("Failed to tune phase : %s:%i\n", __func__, __LINE__);
		goto calibrate_fail;
	}
	set_calibration_progress(calib_progress, 0.40);
	DBG("rx_phase_hpc %f", rx_phase_hpc);

	/*
	 * Calibrate RX:
	 * 3 TX1B_B (HPC) -> RX1C_A (LPC) : BIST_LOOPBACK on B
	 */

	osc_plot_xcorr_revert(plot_xcorr_4ch, false);
	trx_phase_rotation(cf_ad9361_hpc, 0.0);
	__cal_switch_ports_enable_cb(3);
	rx_phase_lpc = tune_trx_phase_offset(cf_ad9361_lpc, &ret, cal_freq, cal_tone, 1.0, 0.01, trx_phase_rotation);
	if (ret < 0) {
		printf("Failed to tune phase : %s:%i\n", __func__, __LINE__);
		goto calibrate_fail;
	}
	set_calibration_progress(calib_progress, 0.64);

	(void) rx_phase_lpc; /* Avoid compiler warnings */
	DBG("rx_phase_lpc %f", rx_phase_lpc);

	/*
	 * Calibrate TX:
	 * 4 TX1B_A (LPC) -> RX1C_A (LPC) : BIST_LOOPBACK on B
	 */

	osc_plot_xcorr_revert(plot_xcorr_4ch, false);
	trx_phase_rotation(cf_ad9361_hpc, 0.0);
	__cal_switch_ports_enable_cb(4);
	tx_phase_hpc = tune_trx_phase_offset(dev_dds_slave, &ret, cal_freq, cal_tone, -1.0 , 0.001, trx_phase_rotation);
	if (ret < 0) {
		printf("Failed to tune phase : %s:%i\n", __func__, __LINE__);
		goto calibrate_fail;
	}
	set_calibration_progress(calib_progress, 0.88);
	DBG("tx_phase_hpc %f", tx_phase_hpc);

	trx_phase_rotation(cf_ad9361_hpc, rx_phase_hpc);

	gtk_range_set_value(GTK_RANGE(GTK_WIDGET(gtk_builder_get_object(builder,
			"tx_phase"))), scale_phase_0_360(tx_phase_hpc));

	ret = 0;
	set_calibration_progress(calib_progress, 1.0);

calibrate_fail:

	osc_plot_xcorr_revert(plot_xcorr_4ch, false);
	__cal_switch_ports_enable_cb(0);

	if (in0 && in0_slave) {
		iio_channel_attr_write(in0, "quadrature_tracking_en", "1");
		iio_channel_attr_write(in0_slave, "quadrature_tracking_en", "1");
	}

	gdk_threads_enter();
	reload_settings();

	if (ret) {
		create_blocking_popup(GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
			"FMCOMMS5", "Calibration failed");
		auto_calibrate = -1;
	} else {
		/* set completed flag for testing */
		auto_calibrate = 1;
	}

	osc_plot_destroy(plot_xcorr_4ch);
	if (button)
		gtk_widget_show(GTK_WIDGET(button));
	gdk_threads_leave();

	/* reset progress bar */
	gtk_progress_bar_set_fraction(calib_progress, 0.0);
	gtk_progress_bar_set_text(calib_progress, "Calibration Progress");

	/* Disable the channels that were enabled at the beginning of the calibration */
	struct iio_device *iio_dev;
	iio_dev = iio_context_find_device(get_context_from_osc(), CAP_DEVICE_ALT);
	if (iio_dev && cap_device_channels_enabled) {
		iio_channels_change_shadow_of_enabled(iio_dev, false);
		cap_device_channels_enabled = false;
	}

	g_thread_exit(NULL);
}

static void do_calibration (GtkWidget *widget, gpointer data)
{
	struct iio_device *iio_dev;
	unsigned num_chs, enabled_chs_mask;
	GtkToggleButton *silent_calib;

	plot_xcorr_4ch = plugin_get_new_plot();

	silent_calib = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "silent_calibration"));
	if (gtk_toggle_button_get_active(silent_calib)) {
		osc_plot_set_visible(plot_xcorr_4ch, false);
	}

	/* If channel selection of the plot used in the calibration combined
	 * with the channel selections of other existing plots is invalid then
	 * enable all channels. NOTE: remove this implementation once the dma
	 * starts working with any combination of channels.
	 */
	iio_dev = iio_context_find_device(get_context_from_osc(), CAP_DEVICE_ALT);
	if (iio_dev) {
		num_chs = iio_device_get_channels_count(iio_dev);
		enabled_chs_mask = global_enabled_channels_mask(iio_dev);
		if (!dma_valid_selection(CAP_DEVICE_ALT, enabled_chs_mask | 0x33, num_chs)) {
			cap_device_channels_enabled = true;
			iio_channels_change_shadow_of_enabled(iio_dev, true);
		}
	}

	if (plot_xcorr_4ch) {
		osc_plot_set_channel_state(plot_xcorr_4ch, CAP_DEVICE_ALT, 0, true);
		osc_plot_set_channel_state(plot_xcorr_4ch, CAP_DEVICE_ALT, 1, true);
		osc_plot_set_channel_state(plot_xcorr_4ch, CAP_DEVICE_ALT, 4, true);
		osc_plot_set_channel_state(plot_xcorr_4ch, CAP_DEVICE_ALT, 5, true);

		osc_plot_set_domain(plot_xcorr_4ch, XCORR_PLOT);
		osc_plot_set_marker_type(plot_xcorr_4ch, MARKER_PEAK);
	} else
		return;

	if (data)
		gtk_widget_hide(GTK_WIDGET(data));
	g_thread_new("Calibrate_thread", (void *) &calibrate, data);
}

static void undo_calibration (GtkWidget *widget, gpointer data)
{
	struct osc_plugin *plugin;
	GSList *node;

	trx_phase_rotation(dev_dds_master, 0.0);
	trx_phase_rotation(dev_dds_slave, 0.0);
	trx_phase_rotation(cf_ad9361_lpc, 0.0);
	trx_phase_rotation(cf_ad9361_hpc, 0.0);

	gtk_range_set_value(GTK_RANGE(data), 0);

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin && (!strncmp(plugin->name, "FMComms5", 8))) {
			if (plugin->handle_external_request)
				plugin->handle_external_request(NULL, "Reload Settings");
		}
	}
}

static void tx_phase_hscale_value_changed (GtkRange *hscale1, gpointer data)
{
	double value = gtk_range_get_value(hscale1);

	if ((uintptr_t) data)
		trx_phase_rotation(dev_dds_master, value);
	else
		trx_phase_rotation(dev_dds_slave, value);

}

static void bist_tone_cb (GtkWidget *widget, gpointer data)
{
	GtkBuilder *builder = data;
	unsigned int mode, level, freq, c2i, c2q, c1i, c1q;
	char temp[40];

	mode = gtk_combo_box_get_active(GTK_COMBO_BOX(
		GTK_WIDGET(gtk_builder_get_object(builder, "bist_tone"))));
	level = gtk_combo_box_get_active(GTK_COMBO_BOX(
		GTK_WIDGET(gtk_builder_get_object(builder, "tone_level"))));
	freq = gtk_combo_box_get_active(GTK_COMBO_BOX(
		GTK_WIDGET(gtk_builder_get_object(builder, "bist_tone_frequency"))));
	c2i = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
		GTK_WIDGET(gtk_builder_get_object(builder, "c2i"))));
	c2q = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
		GTK_WIDGET(gtk_builder_get_object(builder, "c2q"))));
	c1i = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
		GTK_WIDGET(gtk_builder_get_object(builder, "c1i"))));
	c1q = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(
		GTK_WIDGET(gtk_builder_get_object(builder, "c1q"))));

	sprintf(temp, "%u %u %u %u", mode, freq, level * 6,
		(c2q << 3) | (c2i << 2) | (c1q << 1) | c1i);

	iio_device_debug_attr_write(dev, "bist_tone", temp);

	if (dev_slave)
		iio_device_debug_attr_write(dev_slave, "bist_tone", temp);

}

static char * set_widget_value(GtkWidget *widget, struct w_info *item, int val)
{
	char str[80];
	int bit, ret;

	switch (item->type) {
		case CHECKBOX:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), !!val);
			return "toggled";
		case BUTTON:
			return "clicked";
		case SPINBUTTON:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), val);
			return "value-changed";
		case SPINBUTTON_S8:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), (char) val);
			return "value-changed";
		case COMBOBOX:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), val);
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
static void connect_widget(GtkBuilder *builder, struct w_info *item, int val)
{
	char *signal;
	GtkWidget *widget;
	widget = GTK_WIDGET(gtk_builder_get_object(builder, item->name));
	signal = set_widget_value(widget, item, val);
	g_builder_connect_signal(builder, item->name, signal,
		G_CALLBACK(signal_handler_cb), item);
}

static void update_widget(GtkBuilder *builder, struct w_info *item, int val)
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
			connect_widget(builder, &attrs[i], atoi(value));
			if (ret == 1)
				return 0;
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
			update_widget(builder, &attrs[i], atoi(value));
			if (ret == 1)
				return 0;
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

	if (page_num == 7)
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

static int fmcomms2adv_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	int ret = 0;

	if (MATCH_ATTRIB("calibrate")) {
		/* Set a timer for 20 seconds that calibration should succeed within. */
		struct timespec ts_current, ts_end;
		unsigned long long nsecs;
		clock_gettime(CLOCK_MONOTONIC, &ts_current);
		nsecs = ts_current.tv_nsec + (20000 * pow(10.0, 6));
		ts_end.tv_sec = ts_current.tv_sec + (nsecs / pow(10.0, 9));
		ts_end.tv_nsec = nsecs % (unsigned long long) pow(10.0, 9);

		do_calibration(NULL, NULL);
		while (!auto_calibrate && (timespeccmp(&ts_current, &ts_end, >) == 0)) {
			gtk_main_iteration();
			clock_gettime(CLOCK_MONOTONIC, &ts_current);
		}

		/* Calibration timed out or failed, probably running an old board
		 * without an ADF5355 on it.
		 */
		if (auto_calibrate < 0) {
			fprintf(stderr, "FMCOMMS5 calibration failed.\n");
			ret = -1;
		}

		/* reset calibration completion flag */
		auto_calibrate = 0;
	} else if (MATCH_ATTRIB("SYNC_RELOAD") && atoi(value)) {
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

static int fmcomms2adv_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			fmcomms2adv_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	char *value;

	update_from_ini(ini_fn, THIS_DRIVER, dev,
			fmcomms2_adv_sr_attribs,
			ARRAY_SIZE(fmcomms2_adv_sr_attribs));
	if (can_update_widgets)
		update_widgets(builder);

	value = read_token_from_ini(ini_fn, THIS_DRIVER, "calibrate");
	if (value) {
		fmcomms2adv_handle_driver(NULL, "calibrate", value);
		free(value);
	}
}

static int get_dds_channels(void)
{
	struct iio_device *dev;
	int i, j;
	char name[16];

	for (i = 0; i < 2; i++) {
		dev = i ? dev_dds_master : dev_dds_slave;

		for (j = 0; j < 8; j++)
		{
			snprintf(name, sizeof(name), "altvoltage%d", j);

			dds_out[i][j] = iio_device_find_channel(dev, name, true);
			if (!dds_out[i][j])
				return -errno;
		}
	}

	return 0;
}

static GtkWidget * fmcomms2adv_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkWidget *fmcomms2adv_panel;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	dev = iio_context_find_device(ctx, PHY_DEVICE);
	dev_slave = iio_context_find_device(ctx, PHY_SLAVE_DEVICE);

	if (dev_slave) {
		cf_ad9361_lpc = iio_context_find_device(ctx, CAP_DEVICE_ALT);
		cf_ad9361_hpc = iio_context_find_device(ctx, CAP_SLAVE_DEVICE);

		dev_dds_master = iio_context_find_device(ctx, DDS_DEVICE);
		dev_dds_slave = iio_context_find_device(ctx, DDS_SLAVE_DEVICE);
		if (get_dds_channels())
			return NULL;
	}

	if (ini_fn)
		load_profile(NULL, ini_fn);

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (osc_load_glade_file(builder, "fmcomms2_adv") < 0)
		return NULL;

	fmcomms2adv_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms2adv_panel"));

	connect_widgets(builder);

	gtk_combo_box_set_active(GTK_COMBO_BOX(
		GTK_WIDGET(gtk_builder_get_object(builder, "bist_tone"))), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(
		GTK_WIDGET(gtk_builder_get_object(builder, "bist_tone_frequency"))), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(
		GTK_WIDGET(gtk_builder_get_object(builder, "tone_level"))), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(
		GTK_WIDGET(gtk_builder_get_object(builder, "bist_prbs"))), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(
		GTK_WIDGET(gtk_builder_get_object(builder, "loopback"))), 0);

	g_builder_connect_signal(builder, "bist_tone", "changed",
		G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "bist_tone_frequency", "changed",
		G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "tone_level", "changed",
		G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "c2q", "toggled",
		G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "c1q", "toggled",
		G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "c2i", "toggled",
		G_CALLBACK(bist_tone_cb), builder);

	g_builder_connect_signal(builder, "c1i", "toggled",
		G_CALLBACK(bist_tone_cb), builder);

	if (dev_slave) {
		g_builder_connect_signal(builder, "mcs_sync", "clicked",
			G_CALLBACK(mcs_cb), builder);

		gtk_combo_box_set_active(
				GTK_COMBO_BOX(gtk_builder_get_object(builder, "calibration_switch_control")), 0);
		__cal_switch_ports_enable_cb(0);

		g_builder_connect_signal(builder, "calibration_switch_control", "changed",
			G_CALLBACK(cal_switch_ports_enable_cb), builder);

		g_builder_connect_signal(builder, "tx_phase", "value-changed",
			G_CALLBACK(tx_phase_hscale_value_changed), 0);


		g_builder_connect_signal(builder, "do_fmcomms5_cal", "clicked",
				G_CALLBACK(do_calibration), gtk_builder_get_object(builder, "do_fmcomms5_cal"));

		g_builder_connect_signal(builder, "undo_fmcomms5_cal", "clicked",
				G_CALLBACK(undo_calibration), gtk_builder_get_object(builder, "tx_phase"));

		g_object_bind_property(gtk_builder_get_object(builder, "silent_calibration"), "active",
		gtk_builder_get_object(builder, "progress_calibration"), "visible", G_BINDING_DEFAULT);
	} else {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "mcs_sync")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_fmcomms5")));
	}

	g_builder_connect_signal(builder, "notebook1", "switch-page",
		G_CALLBACK(change_page_cb),
		GTK_WIDGET(gtk_builder_get_object(builder, "initialize")));

	can_update_widgets = true;

	return fmcomms2adv_panel;
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
		save_to_ini(f, THIS_DRIVER, dev, fmcomms2_adv_sr_attribs,
				ARRAY_SIZE(fmcomms2_adv_sr_attribs));
		fclose(f);
	}
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	save_profile(NULL, ini_fn);
	osc_destroy_context(ctx);
}

static bool fmcomms2adv_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	dev = iio_context_find_device(osc_ctx, PHY_DEVICE);
	dev_slave = iio_context_find_device(osc_ctx, PHY_SLAVE_DEVICE);
	if (dev_slave) {
		cf_ad9361_lpc = iio_context_find_device(osc_ctx, CAP_DEVICE_ALT);
		cf_ad9361_hpc = iio_context_find_device(osc_ctx, CAP_SLAVE_DEVICE);
		dev_dds_master = iio_context_find_device(osc_ctx, DDS_DEVICE);
		dev_dds_slave = iio_context_find_device(osc_ctx, DDS_SLAVE_DEVICE);
	}

	return !!dev && iio_device_get_debug_attrs_count(dev)
		&& (!dev_slave || (!!cf_ad9361_lpc && !!cf_ad9361_hpc &&
					!!dev_dds_master && !!dev_dds_slave));
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = fmcomms2adv_identify,
	.init = fmcomms2adv_init,
	.handle_item = fmcomms2adv_handle,
	.handle_external_request = handle_external_request,
	.update_active_page = update_active_page,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
