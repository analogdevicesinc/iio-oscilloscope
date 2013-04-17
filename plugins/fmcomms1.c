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
#include "../eeprom.h"
#include "../osc.h"

static const gdouble mhz_scale = 1000000.0;

#define VERSION_SUPPORTED 0
static struct fmcomms1_calib_data *cal_data;

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static unsigned int num_tx, num_rx;

static const char *adc_freq_device;
static const char *adc_freq_file;

static int num_tx_pll, num_rx_pll;

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

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

double fract_to_float(unsigned short val)
{
	double ret = 0;

	if (val & 0x8000) {
		ret = 1.0000;
		val &= ~0x8000;
	}

	ret += (double)val / 0x8000;

	return ret;
}

struct fmcomms1_calib_data *find_entry(struct fmcomms1_calib_data *ptr, unsigned f)
{
	struct fmcomms1_calib_data *data;
	int ind = 0;
	int delta, gindex = 0;
	int min_delta = 2147483647;

	data = ptr;

	do {
		if (data->adi_magic0 != ADI_MAGIC_0 || data->adi_magic1 != ADI_MAGIC_1) {
			fprintf (stderr, "invalid magic detected\n");
			return NULL;
		}
		if (data->version != ADI_VERSION(VERSION_SUPPORTED)) {
			fprintf (stderr, "unsupported version detected %c\n", data->version);
			return NULL;
		}


		if (f) {
			delta = abs(f - data->cal_frequency_MHz);
			if (delta < min_delta) {
				gindex = ind;
				min_delta = delta;
			}

		}
		ind++;
	} while (data++->next);

	return &ptr[gindex];
}

void store_entry_hw(struct fmcomms1_calib_data *data, unsigned tx, unsigned rx)
{
	if (!data)
		return;

	if (tx) {
		set_dev_paths("cf-ad9122-core-lpc");
		write_devattr_slonglong("out_voltage0_calibbias", data->i_dac_offset);
		write_devattr_slonglong("out_voltage0_calibscale", data->i_dac_fs_adj);
		write_devattr_slonglong("out_voltage0_phase", data->i_phase_adj);
		write_devattr_slonglong("out_voltage1_calibbias", data->q_dac_offset);
		write_devattr_slonglong("out_voltage1_calibscale", data->q_dac_fs_adj);
		write_devattr_slonglong("out_voltage1_phase", data->q_phase_adj);
		tx_update_values();
	}

	if (rx) {
		set_dev_paths("cf-ad9643-core-lpc");
		write_devattr_slonglong("in_voltage0_calibbias", data->i_adc_offset_adj);
		write_devattr_double("in_voltage0_calibscale", fract_to_float(data->i_adc_gain_adj));
		write_devattr_slonglong("in_voltage1_calibbias", data->q_adc_offset_adj);
		write_devattr_double("in_voltage1_calibscale", fract_to_float(data->q_adc_gain_adj));
		rx_update_values();
	}
}

static gdouble pll_get_freq(struct iio_widget *widget)
{
	gdouble freq;

	gdouble scale = widget->priv ? *(gdouble *)widget->priv : 1.0;
	freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON (widget->widget));
	freq *= scale;

	return freq;
}

static void cal_button_clicked(GtkButton *btn, gpointer data)
{
	gdouble freq;
	freq = pll_get_freq(&tx_widgets[num_tx_pll]);
	store_entry_hw(find_entry(cal_data, (unsigned) (freq / mhz_scale)), 1, 0);

	freq = pll_get_freq(&rx_widgets[num_rx_pll]);
	store_entry_hw(find_entry(cal_data,  (unsigned) (freq / mhz_scale)), 0, 1);
}

static int fmcomms1_cal_eeprom(void)
{
	char eprom_names[512];
	FILE *efp, *fp;
	int num, tmp;

	/* flushes all open output streams */
	fflush(NULL);

	cal_data = malloc(FAB_SIZE_CAL_EEPROM);
	if (cal_data == NULL) {
		return -ENOMEM;
	}

	fp = popen("find /sys -name eeprom 2>/dev/null", "r");

	if(fp == NULL) {
		fprintf(stderr, "can't execute find\n");
		return -errno;
	}

	num = 0;

	while(fgets(eprom_names, sizeof(eprom_names), fp) != NULL){
		num++;
		/* strip trailing new lines */
		if (eprom_names[strlen(eprom_names) - 1] == '\n')
			eprom_names[strlen(eprom_names) - 1] = '\0';

		efp = fopen(eprom_names, "rb");
		if(efp == NULL)
			return -errno;

		memset(cal_data, 0, FAB_SIZE_CAL_EEPROM);
		tmp = fread(cal_data, FAB_SIZE_CAL_EEPROM, 1, efp);
		fclose(efp);

		if (!tmp || cal_data->adi_magic0 != ADI_MAGIC_0 || cal_data->adi_magic1 != ADI_MAGIC_1) {
			continue;
		}

		if (cal_data->version != ADI_VERSION(VERSION_SUPPORTED)) {
			continue;
		}

		fprintf (stdout, "Found Calibration EEPROM @ %s\n", eprom_names);
		pclose(fp);

		return 0;
	}

	pclose(fp);

	return -ENODEV;
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

	/* The next free frequency related widgets - keep in this order! */
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage_1A_sampling_frequency",
			builder, "dac_data_clock", &mhz_scale);

	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage_interpolation_frequency",
			"out_altvoltage_interpolation_frequency_available",
			builder, "dac_interpolation_clock", NULL);

	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc",
			"out_altvoltage_interpolation_center_shift_frequency",
			"out_altvoltage_interpolation_center_shift_frequency_available",
			builder, "dac_fcenter_shift", NULL);

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage0_1A_raw",
			builder, "tx_enable", 0);
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
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage0_calibbias",
			builder, "dac_calibbias0", NULL);
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage0_calibscale",
			builder, "dac_calibscale0", NULL);
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage0_phase",
			builder, "dac_calibphase0", NULL);
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage1_calibbias",
			builder, "dac_calibbias1", NULL);
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage1_calibscale",
			builder, "dac_calibscale1", NULL);
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage1_phase",
			builder, "dac_calibphase1", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_frequency_resolution",
			builder, "tx_lo_spacing", NULL);
	num_tx_pll = num_tx;
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_frequency",
			builder, "tx_lo_freq", &mhz_scale);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_powerdown",
			builder, "tx_lo_powerdown", 1);


	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_frequency_resolution",
			builder, "rx_lo_spacing", NULL);
	num_rx_pll = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_frequency",
			builder, "rx_lo_freq", &mhz_scale);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_powerdown",
			builder, "rx_lo_powerdown", 1);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			adc_freq_device, adc_freq_file,
			builder, "adc_freq", &mhz_scale);
	iio_spin_button_s64_init_from_builder(&rx_widgets[num_rx++],
			"cf-ad9643-core-lpc", "in_voltage0_calibbias",
			builder, "adc_calibbias0", NULL);
	iio_spin_button_s64_init_from_builder(&rx_widgets[num_rx++],
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

	if (fmcomms1_cal_eeprom() >= 0)
		g_builder_connect_signal(builder, "fmcomms1_cal", "clicked",
			G_CALLBACK(cal_button_clicked), NULL);
	else
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms1_cal")));

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
