/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
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
#include <malloc.h>

#include "../iio_widget.h"
#include "../iio_utils.h"
#include "../osc_plugin.h"
#include "../config.h"

static const gdouble mhz_scale = 1000000.0;

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static unsigned int num_tx, num_rx;

static const char *adc_freq_device;
static const char *adc_freq_file;

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}
void rx_update_labels(void);

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

static void save_button_clicked(GtkButton *btn, gpointer data)
{
	iio_save_widgets(tx_widgets, num_tx);
	iio_save_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

static int compare_gain(const char *a, const char *b)
{
	double val_a, val_b;
	sscanf(a, "%lf", &val_a);
	sscanf(b, "%lf", &val_b);

	if (val_a < val_b)
		return -1;
	else if(val_a > val_b)
		return 1;
	else
		return 0;
}

static int fmcomms1_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *fmcomms1_panel;

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "fmcomms1.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "fmcomms1.glade", NULL);

	fmcomms1_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms1_panel"));

	if (iio_devattr_exists("cf-ad9643-core-lpc", "in_voltage_sampling_frequency")) {
		adc_freq_device = "cf-ad9643-core-lpc";
		adc_freq_file = "in_voltage_sampling_frequency";
	} else {
		adc_freq_device = "ad9523-lpc";
		adc_freq_file = "out_altvoltage2_ADC_CLK_frequency";
	}

	/* Bind the IIO device files to the GUI widgets */
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage0_1A_raw",
			builder, "tx_enable");
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage0_1A_frequency",
			builder, "dds_tone1_freq", &mhz_scale);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage2_2A_frequency",
			builder, "dds_tone1_freq", &mhz_scale);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage1_1B_frequency",
			builder, "dds_tone2_freq", &mhz_scale);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage3_2B_frequency",
			builder, "dds_tone2_freq", &mhz_scale);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage0_1A_scale",
			"out_altvoltage_1A_scale_available",
			builder, "dds_tone1_scale", compare_gain);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage2_2A_scale",
			"out_altvoltage_2A_scale_available",
			builder, "dds_tone1_scale", compare_gain);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage1_1B_scale",
			"out_altvoltage_1B_scale_available",
			builder, "dds_tone2_scale", compare_gain);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage3_2B_scale",
			"out_altvoltage_2B_scale_available",
			builder, "dds_tone2_scale", compare_gain);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage0_calibbias",
			builder, "dac_calibbias0", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage0_calibscale",
			builder, "dac_calibscale0", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage0_phase",
			builder, "dac_calibphase0", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage0_calibbias",
			builder, "dac_calibbias1", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage1_calibscale",
			builder, "dac_calibscale1", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage1_phase",
			builder, "dac_calibphase1", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_frequency",
			builder, "tx_lo_freq", &mhz_scale);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_frequency_resolution",
			builder, "tx_lo_spacing", NULL);

	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_frequency",
			builder, "rx_lo_freq", &mhz_scale);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_frequency_resolution",
			builder, "rx_lo_spacing", NULL);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			adc_freq_device, adc_freq_file,
			builder, "adc_freq", &mhz_scale);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"cf-ad9643-core-lpc", "in_voltage0_calibbias",
			builder, "adc_calibbias0", NULL);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"cf-ad9643-core-lpc", "in_voltage1_calibbias",
			builder, "adc_calibbias1", NULL);
	iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
			"cf-ad9643-core-lpc", "in_voltage0_calibscale",
			builder, "adc_calibscale0", NULL);
	iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
			"cf-ad9643-core-lpc", "in_voltage1_calibscale",
			builder, "adc_calibscale1", NULL);
	iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
			"ad8366-lpc", "out_voltage0_hardwaregain",
			builder, "adc_gain0", NULL);
	iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
			"ad8366-lpc", "out_voltage1_hardwaregain",
			builder, "adc_gain1", NULL);

	g_builder_connect_signal(builder, "fmcomms1_settings_save", "clicked",
		G_CALLBACK(save_button_clicked), NULL);

	tx_update_values();
	rx_update_values();

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), fmcomms1_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), fmcomms1_panel, "FMComms1");

	return 0;
}

static bool fmcomms1_identify(void)
{
	return !set_dev_paths("cf-ad9122-core-lpc");
}

const struct osc_plugin plugin = {
	.name = "FMComms1",
	.identify = fmcomms1_identify,
	.init = fmcomms1_init,
};
