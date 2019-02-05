/**
 * Copyright (C) 2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>

#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static unsigned int num_tx, num_rx;
static struct iio_context *ctx;
static struct iio_device *dev;

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_device_sampling_freq("ad5628-1",
		USE_INTERN_SAMPLING_FREQ);
}

static void save_button_clicked(GtkButton *btn, gpointer data)
{
	iio_save_widgets(tx_widgets, num_tx);
	iio_save_widgets(rx_widgets, num_rx);
	rx_update_device_sampling_freq("ad5628-1",
		USE_INTERN_SAMPLING_FREQ);
}

static GtkWidget * AD5628_1_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *AD5628_1_panel;
	struct iio_channel *chn;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;
	dev = iio_context_find_device(ctx, "ad5628-1");

	builder = gtk_builder_new();

	if (osc_load_glade_file(builder, "AD5628_1") < 0)
		return NULL;

	AD5628_1_panel = GTK_WIDGET(gtk_builder_get_object(builder, "tablePanelAD5628_1"));

	/* Bind the IIO device files to the GUI widgets */

	chn = iio_device_find_channel(dev, "voltage0", true);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "raw", builder, "spinbuttonRawValue0", NULL);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown_mode", "powerdown_mode_available",
			builder, "comboboxPwrDwnModes0", NULL);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown",
			builder, "checkbuttonPowerdown0", 0);

	chn = iio_device_find_channel(dev, "voltage1", true);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "raw", builder, "spinbuttonRawValue1", NULL);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown_mode", "powerdown_mode_available",
			builder, "comboboxPwrDwnModes1", NULL);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown",
			builder, "checkbuttonPowerdown1", 0);

	chn = iio_device_find_channel(dev, "voltage2", true);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "raw", builder, "spinbuttonRawValue2", NULL);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown_mode", "powerdown_mode_available",
			builder, "comboboxPwrDwnModes2", NULL);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown",
			builder, "checkbuttonPowerdown2", 0);

	chn = iio_device_find_channel(dev, "voltage3", true);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "raw", builder, "spinbuttonRawValue3", NULL);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown_mode", "powerdown_mode_available",
			builder, "comboboxPwrDwnModes3", NULL);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown",
			builder, "checkbuttonPowerdown3", 0);

	chn = iio_device_find_channel(dev, "voltage4", true);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "raw", builder, "spinbuttonRawValue4", NULL);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown_mode", "powerdown_mode_available",
			builder, "comboboxPwrDwnModes4", NULL);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown",
			builder, "checkbuttonPowerdown4", 0);

	chn = iio_device_find_channel(dev, "voltage5", true);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "raw", builder, "spinbuttonRawValue5", NULL);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown_mode", "powerdown_mode_available",
			builder, "comboboxPwrDwnModes5", NULL);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown",
			builder, "checkbuttonPowerdown5", 0);

	chn = iio_device_find_channel(dev, "voltage6", true);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "raw", builder, "spinbuttonRawValue6", NULL);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown_mode", "powerdown_mode_available",
			builder, "comboboxPwrDwnModes6", NULL);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown",
			builder, "checkbuttonPowerdown6", 0);

	chn = iio_device_find_channel(dev, "voltage7", true);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "raw", builder, "spinbuttonRawValue7", NULL);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown_mode", "powerdown_mode_available",
			builder, "comboboxPwrDwnModes7", NULL);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, chn, "powerdown",
			builder, "checkbuttonPowerdown7", 0);

	g_builder_connect_signal(builder, "buttonSave", "clicked",
		G_CALLBACK(save_button_clicked), NULL);

	tx_update_values();
	rx_update_values();

	return AD5628_1_panel;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	osc_destroy_context(ctx);
}

static bool AD5628_1_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();
	return !!iio_context_find_device(osc_ctx, "ad5628-1");
}

struct osc_plugin plugin = {
	.name = "AD5628-1",
	.identify = AD5628_1_identify,
	.init = AD5628_1_init,
	.destroy = context_destroy,
};
