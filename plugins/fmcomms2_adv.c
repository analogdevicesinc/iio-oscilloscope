/**
 * Copyright (C) 2013 Analog Devices, Inc.
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

#include "../osc.h"
#include "../iio_utils.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../iio_widget.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

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

static char dir_name[512];

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
	{COMBOBOX, "bist_prbs"},
	{COMBOBOX, "loopback"},
	{BUTTON, "initialize"},
};

static void update_widget(GtkBuilder *builder, struct w_info *item)
{
	GtkWidget *widget;
	int val;

	widget = GTK_WIDGET(gtk_builder_get_object(builder, item->name));
	val = read_sysfs_posint(item->name, dir_name);

	/* check for errors, in case there is a kernel <-> userspace mismatch */
	if (val < 0) {
		printf("%s:%s: error accessing '%s' (%s)\n",
			__FILE__, __func__, item->name, strerror(-val));
		gtk_widget_hide(widget);
		return;
	}

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
	char temp[40];

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
	}

	sprintf(temp, "%d\n", val);
	write_sysfs_string(item->name, dir_name, temp);
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

	sprintf(temp, "%d %d %d %d\n", mode, freq, level * 6,
		(c2q << 3) | (c2i << 2) | (c1q << 1) | c1i);

	write_sysfs_string("bist_tone", dir_name, temp);
}

static void connect_widget(GtkBuilder *builder, struct w_info *item)
{
	GtkWidget *widget;
	char *signal = NULL;
	int val;

	widget = GTK_WIDGET(gtk_builder_get_object(builder, item->name));
	val = read_sysfs_posint(item->name, dir_name);

	/* check for errors, in case there is a kernel <-> userspace mismatch */
	if (val < 0) {
		printf("%s:%s: error accessing '%s' (%s)\n",
			__FILE__, __func__, item->name, strerror(-val));
		gtk_widget_hide(widget);
		return;
	}

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

	if (page_num == 5)
		gtk_widget_hide(tohide); /* Hide Init button in BIST Tab */
	else
		gtk_widget_show(tohide);
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

	return 0;
}

#define SYNC_RELOAD "SYNC_RELOAD"

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
	"debug.ad9361-phy.adi,ensm-enable-pin-pulse-mode-enable",
	"debug.ad9361-phy.adi,ensm-enable-txnrx-control-enable",
	"debug.ad9361-phy.adi,external-rx-lo-enable",
	"debug.ad9361-phy.adi,external-tx-lo-enable",
	"debug.ad9361-phy.adi,frequency-division-duplex-mode-enable",
	"debug.ad9361-phy.adi,gc-adc-large-overload-thresh",
	"debug.ad9361-phy.adi,gc-adc-ovr-sample-size",
	"debug.ad9361-phy.adi,gc-adc-small-overload-thresh",
	"debug.ad9361-phy.adi,gc-dec-pow-measurement-duration",
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
	SYNC_RELOAD,
	NULL
};

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static bool fmcomms2adv_identify(void)
{
	bool ret = !set_debugfs_paths("ad9361-phy");
	strcpy(dir_name, debug_name_dir());
	return ret;
}

struct osc_plugin plugin = {
	.name = "FMComms2 Advanced",
	.identify = fmcomms2adv_identify,
	.init = fmcomms2adv_init,
	.save_restore_attribs = fmcomms2_adv_sr_attribs,
	.handle_item = handle_item,
	.update_active_page = update_active_page,
};
