/**
 * Copyright (C) 2013-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <glib/gthread.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <malloc.h>
#include <values.h>
#include <sys/stat.h>
#include <string.h>

#include <iio.h>

#include "../osc.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../iio_widget.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

static struct iio_context *ctx;
static struct iio_device *dev;

static gint this_page;
static GtkNotebook *nbook;
static gboolean plugin_detached;
static GtkBuilder *builder;

enum fmcomms2adv_wtype {
	CHECKBOX,
	SPINBUTTON,
	COMBOBOX,
	BUTTON,
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
	{CHECKBOX, "adi,ensm-enable-pin-pulse-mode-enable"},
	{CHECKBOX, "adi,ensm-enable-txnrx-control-enable"},
	{CHECKBOX, "adi,external-rx-lo-enable"},
	{CHECKBOX, "adi,external-tx-lo-enable"},
	{CHECKBOX, "adi,frequency-division-duplex-mode-enable"},
	{SPINBUTTON, "adi,gc-adc-large-overload-thresh"},
	{SPINBUTTON, "adi,gc-adc-ovr-sample-size"},
	{SPINBUTTON, "adi,gc-adc-small-overload-thresh"},
	{SPINBUTTON, "adi,gc-dec-pow-measurement-duration"},
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
	{SPINBUTTON, "adi,temp-sense-offset-signed"},
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
	{COMBOBOX, "bist_prbs"},
	{COMBOBOX, "loopback"},
	{BUTTON, "initialize"},
};

static void update_widget(GtkBuilder *builder, struct w_info *item)
{
	GtkWidget *widget;
	int val;
	long long value;

	widget = GTK_WIDGET(gtk_builder_get_object(builder, item->name));
	val = iio_device_debug_attr_read_longlong(dev, item->name, &value);

	/* check for errors, in case there is a kernel <-> userspace mismatch */
	if (val < 0) {
		printf("%s:%s: error accessing '%s' (%s)\n",
			__FILE__, __func__, item->name, strerror(-val));
		gtk_widget_hide(widget);
		return;
	}

	val = (int) value;
	switch (item->type) {
		case CHECKBOX:
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), !!val);
			break;
		case BUTTON:
			if (val)
				gtk_button_clicked(GTK_BUTTON(widget));
			break;
		case SPINBUTTON:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), val);
			break;
		case COMBOBOX:
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), val);
			break;
	}

}

void signal_handler_cb (GtkWidget *widget, gpointer data)
{
	struct w_info *item = data;
	unsigned val;

	switch (item->type) {
		case CHECKBOX:
			val = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (widget));
			break;
		case BUTTON:
			val = 1;
			break;
		case SPINBUTTON:
			val = (unsigned) gtk_spin_button_get_value(GTK_SPIN_BUTTON (widget));
			break;
		case COMBOBOX:
			val = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
			break;
		default:
			return;
	}

	iio_device_debug_attr_write_longlong(dev, item->name, val);
}

void bist_tone_cb (GtkWidget *widget, gpointer data)
{
	GtkBuilder *builder = data;
	unsigned mode, level, freq, c2i, c2q, c1i, c1q;
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

	sprintf(temp, "%d %d %d %d", mode, freq, level * 6,
		(c2q << 3) | (c2i << 2) | (c1q << 1) | c1i);

	iio_device_debug_attr_write(dev, "bist_tone", temp);
}

static void connect_widget(GtkBuilder *builder, struct w_info *item)
{
	GtkWidget *widget;
	char *signal = NULL;
	int val;
	long long value;

	widget = GTK_WIDGET(gtk_builder_get_object(builder, item->name));
	val = iio_device_debug_attr_read_longlong(dev, item->name, &value);

	/* check for errors, in case there is a kernel <-> userspace mismatch */
	if (val < 0) {
		printf("%s:%s: error accessing '%s' (%s)\n",
			__FILE__, __func__, item->name, strerror(-val));
		gtk_widget_hide(widget);
		return;
	}

	val = (int) value;
	switch (item->type) {
		case CHECKBOX:
			signal = "toggled";
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), !!val);
			break;
		case BUTTON:
			signal = "clicked";
			break;
		case SPINBUTTON:
			signal = "value-changed";
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), val);
			break;
		case COMBOBOX:
			signal = "changed";
			gtk_combo_box_set_active(GTK_COMBO_BOX(widget), val);
			break;
	}

	g_builder_connect_signal(builder, item->name, signal,
		G_CALLBACK(signal_handler_cb), item);
}

void change_page_cb (GtkNotebook *notebook, GtkNotebookPage *page,
		     guint page_num, gpointer user_data)
{
	GtkWidget *tohide = user_data;

	if (page_num == 7)
		gtk_widget_hide(tohide); /* Hide Init button in BIST Tab */
	else
		gtk_widget_show(tohide);
}

static struct iio_context * fmcomms2_adv_iio_context(void)
{
	return ctx;
}

#define SYNC_RELOAD "SYNC_RELOAD"

static GSList *fmcomms2_adv_sr_attribs;

static void build_plugin_profile_attribute_list(void)
{
	profile_elements_init(&fmcomms2_adv_sr_attribs);

	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "agc-adc-large-overload-exceed-counter");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-adc-large-overload-inc-steps");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-adc-lmt-small-overload-prevent-gain-inc-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-adc-small-overload-exceed-counter");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-attack-delay-extra-margin-us");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-dig-gain-step-size");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-dig-saturation-exceed-counter");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-gain-update-interval-us");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-immed-gain-change-if-large-adc-overload-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-immed-gain-change-if-large-lmt-overload-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-inner-thresh-high");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-inner-thresh-high-dec-steps");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-inner-thresh-low");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-inner-thresh-low-inc-steps");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-lmt-overload-large-exceed-counter");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-lmt-overload-large-inc-steps");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-lmt-overload-small-exceed-counter");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-outer-thresh-high");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-outer-thresh-high-dec-steps");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-outer-thresh-low");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-outer-thresh-low-inc-steps");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,agc-sync-for-gain-counter-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-adc-decimation");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-adc-rate");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,clk-output-mode-select");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,ctrl-outs-enable-mask");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,ctrl-outs-index");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,dc-offset-attenuation-high-range");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,dc-offset-attenuation-low-range");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,dc-offset-count-high-range");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,dc-offset-count-low-range");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,dc-offset-tracking-update-event-mask");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,elna-bypass-loss-mdB");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,elna-gain-mdB");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,elna-rx1-gpo0-control-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,elna-rx2-gpo1-control-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,elna-settling-delay-ns");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,ensm-enable-pin-pulse-mode-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,ensm-enable-txnrx-control-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,external-rx-lo-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,external-tx-lo-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,frequency-division-duplex-mode-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,gc-adc-large-overload-thresh");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,gc-adc-ovr-sample-size");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,gc-adc-small-overload-thresh");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,gc-dec-pow-measurement-duration");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,gc-dig-gain-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,gc-lmt-overload-high-thresh");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,gc-lmt-overload-low-thresh");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,gc-low-power-thresh");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,gc-max-dig-gain");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,gc-rx1-mode");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,gc-rx2-mode");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,mgc-dec-gain-step");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,mgc-inc-gain-step");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,mgc-rx1-ctrl-inp-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,mgc-rx2-ctrl-inp-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,mgc-split-table-ctrl-inp-gain-mode");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,rssi-delay");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,rssi-duration");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,rssi-restart-mode");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,rssi-wait");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,rx-rf-port-input-select");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,split-gain-table-mode-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,tdd-skip-vco-cal-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,tdd-use-dual-synth-mode-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,tdd-use-fdd-vco-tables-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,temp-sense-decimation");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,temp-sense-measurement-interval-ms");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,temp-sense-offset-signed");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,temp-sense-periodic-measurement-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,tx-rf-port-input-select");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,update-tx-gain-in-alert-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,xo-disable-use-ext-refclk-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-dec-pow-measurement-duration");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-allow-agc-gain-increase-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-energy-lost-stronger-sig-gain-lock-exit-cnt");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-final-overrange-count");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-gain-increase-after-gain-lock-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-gain-index-type-after-exit-rx-mode");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-lmt-final-settling-steps");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-lock-level-gain-increase-upper-limit");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-lock-level-lmt-gain-increase-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-lp-thresh-increment-steps");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-lp-thresh-increment-time");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-lpf-final-settling-steps");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-optimized-gain-offset");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-power-measurement-duration-in-state5");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-rst-gla-en-agc-pulled-high-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-rst-gla-engergy-lost-goto-optim-gain-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-rst-gla-engergy-lost-sig-thresh-below-ll");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-rst-gla-engergy-lost-sig-thresh-exceeded-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-rst-gla-if-en-agc-pulled-high-mode");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-rst-gla-large-adc-overload-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-rst-gla-large-lmt-overload-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-rst-gla-stronger-sig-thresh-above-ll");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-rst-gla-stronger-sig-thresh-exceeded-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-state-wait-time-ns");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,fagc-use-last-lock-level-for-set-gain-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac-manual-mode-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac1-active-in-alert-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac1-active-in-rx-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac1-active-in-tx-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac1-default-value-mV");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac1-rx-delay-us");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac1-tx-delay-us");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac2-active-in-alert-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac2-active-in-rx-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac2-active-in-tx-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac2-default-value-mV");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac2-rx-delay-us");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,aux-dac2-tx-delay-us");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,rx-fastlock-delay-ns");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,tx-fastlock-delay-ns");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,rx-fastlock-pincontrol-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,tx-fastlock-pincontrol-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,txmon-1-front-end-gain");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,txmon-1-lo-cm");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,txmon-2-front-end-gain");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,txmon-2-lo-cm");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,txmon-dc-tracking-enable");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,txmon-delay");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,txmon-duration");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,txmon-high-gain");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,txmon-low-gain");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,txmon-low-high-thresh");
	profile_elements_add_debug_attr(&fmcomms2_adv_sr_attribs, dev, "adi,txmon-one-shot-mode-enable");
	profile_elements_add_plugin_attr(&fmcomms2_adv_sr_attribs, SYNC_RELOAD);

	fmcomms2_adv_sr_attribs = g_slist_reverse(fmcomms2_adv_sr_attribs);
}

static int fmcomms2adv_init(GtkWidget *notebook)
{
	GtkWidget *fmcomms2adv_panel;
	int i;

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (!gtk_builder_add_from_file(builder, "fmcomms2_adv.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "fmcomms2_adv.glade", NULL);

	fmcomms2adv_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms2adv_panel"));

	for (i = 0; i < ARRAY_SIZE(attrs); i++)
		connect_widget(builder, &attrs[i]);

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


	this_page = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), fmcomms2adv_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), fmcomms2adv_panel, "FMComms2 Advanced");

	g_builder_connect_signal(builder, "notebook1", "switch-page",
		G_CALLBACK(change_page_cb),
		GTK_WIDGET(gtk_builder_get_object(builder, "initialize")));

	build_plugin_profile_attribute_list();

	return 0;
}

static char *handle_item(struct osc_plugin *plugin, const char *attrib,
			 const char *value)
{
	int i;

	if (MATCH_ATTRIB(SYNC_RELOAD)) {
		if (value) {
			for (i = 0; i < ARRAY_SIZE(attrs); i++)
				update_widget(builder, &attrs[i]);
			gtk_button_clicked(GTK_BUTTON(gtk_builder_get_object(builder,
					"initialize")));
		} else {
			return "1";
		}
	} else {
		if (value) {
			printf("Unhandled tokens in ini file,\n"
				"\tSection %s\n\tAtttribute : %s\n\tValue: %s\n",
				"FMComms2 Advanced", attrib, value);
			return "FAIL";
		}
	}

	return NULL;
}

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static bool fmcomms2adv_identify(void)
{
	ctx = osc_create_context();
	dev = iio_context_find_device(ctx, "ad9361-phy");
	if (dev && !iio_device_get_debug_attrs_count(dev))
		dev = NULL;
	if (!dev)
		iio_context_destroy(ctx);

	return !!dev;
}

struct osc_plugin plugin = {
	.name = "FMComms2 Advanced",
	.identify = fmcomms2adv_identify,
	.init = fmcomms2adv_init,
	.save_restore_attribs = &fmcomms2_adv_sr_attribs,
	.get_iio_context = fmcomms2_adv_iio_context,
	.handle_item = handle_item,
	.update_active_page = update_active_page,
};
