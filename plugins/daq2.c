/**
 * Copyright (C) 2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <glib.h>
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
#include <sys/stat.h>
#include <string.h>

#include <iio.h>

#include "../libini2.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "./block_diagram.h"
#include "dac_data_manager.h"

#define THIS_DRIVER "DAQ1/2/3"

#define SYNC_RELOAD "SYNC_RELOAD"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

static const char *ADC_DEVICE;
static const char *DAC_DEVICE;


#define DAQ1_ADC_DEVICE "axi-ad9684-lpc"
#define DAQ2_ADC_DEVICE "axi-ad9680-hpc"
#define DAQ3_ADC_DEVICE "axi-ad9680-hpc"


#define DAQ1_DAC_DEVICE "axi-ad9122-lpc"
#define DAQ2_DAC_DEVICE "axi-ad9144-hpc"
#define DAQ3_DAC_DEVICE "axi-ad9152-hpc"


static struct dac_data_manager *dac_tx_manager;

static struct iio_context *ctx;
static struct iio_device *dac, *adc;

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static unsigned int num_tx, num_rx;

static bool can_update_widgets;

static int daq_board;
static const char **daq_sr_attribs;
static int sr_attribs_array_size;


static const char *daq1_sr_attribs[] = {
	DAQ1_ADC_DEVICE".in_voltage_sampling_frequency",
	DAQ1_ADC_DEVICE".in_voltage0_test_mode",
	DAQ1_ADC_DEVICE".in_voltage1_test_mode",
	DAQ1_DAC_DEVICE".out_altvoltage_sampling_frequency",
	DAQ1_DAC_DEVICE".out_altvoltage0_1A_frequency",
	DAQ1_DAC_DEVICE".out_altvoltage2_2A_frequency",
	DAQ1_DAC_DEVICE".out_altvoltage1_1B_frequency",
	DAQ1_DAC_DEVICE".out_altvoltage3_2B_frequency",
	DAQ1_DAC_DEVICE".out_altvoltage0_1A_scale",
	DAQ1_DAC_DEVICE".out_altvoltage2_2A_scale",
	DAQ1_DAC_DEVICE".out_altvoltage1_1B_scale",
	DAQ1_DAC_DEVICE".out_altvoltage3_2B_scale",
	DAQ1_DAC_DEVICE".out_altvoltage0_1A_phase",
	DAQ1_DAC_DEVICE".out_altvoltage1_1B_phase",
	DAQ1_DAC_DEVICE".out_altvoltage2_2A_phase",
	DAQ1_DAC_DEVICE".out_altvoltage3_2B_phase",
};

static const char *daq2_sr_attribs[] = {
	DAQ2_ADC_DEVICE".in_voltage_sampling_frequency",
	DAQ2_ADC_DEVICE".in_voltage0_test_mode",
	DAQ2_ADC_DEVICE".in_voltage1_test_mode",
	DAQ2_DAC_DEVICE".out_altvoltage_sampling_frequency",
	DAQ2_DAC_DEVICE".out_altvoltage0_1A_frequency",
	DAQ2_DAC_DEVICE".out_altvoltage2_2A_frequency",
	DAQ2_DAC_DEVICE".out_altvoltage1_1B_frequency",
	DAQ2_DAC_DEVICE".out_altvoltage3_2B_frequency",
	DAQ2_DAC_DEVICE".out_altvoltage0_1A_scale",
	DAQ2_DAC_DEVICE".out_altvoltage2_2A_scale",
	DAQ2_DAC_DEVICE".out_altvoltage1_1B_scale",
	DAQ2_DAC_DEVICE".out_altvoltage3_2B_scale",
	DAQ2_DAC_DEVICE".out_altvoltage0_1A_phase",
	DAQ2_DAC_DEVICE".out_altvoltage1_1B_phase",
	DAQ2_DAC_DEVICE".out_altvoltage2_2A_phase",
	DAQ2_DAC_DEVICE".out_altvoltage3_2B_phase",
};

static const char *daq3_sr_attribs[] = {
	DAQ3_ADC_DEVICE".in_voltage_sampling_frequency",
	DAQ3_ADC_DEVICE".in_voltage0_test_mode",
	DAQ3_ADC_DEVICE".in_voltage1_test_mode",
	DAQ3_DAC_DEVICE".out_altvoltage_sampling_frequency",
	DAQ3_DAC_DEVICE".out_altvoltage0_1A_frequency",
	DAQ3_DAC_DEVICE".out_altvoltage2_2A_frequency",
	DAQ3_DAC_DEVICE".out_altvoltage1_1B_frequency",
	DAQ3_DAC_DEVICE".out_altvoltage3_2B_frequency",
	DAQ3_DAC_DEVICE".out_altvoltage0_1A_scale",
	DAQ3_DAC_DEVICE".out_altvoltage2_2A_scale",
	DAQ3_DAC_DEVICE".out_altvoltage1_1B_scale",
	DAQ3_DAC_DEVICE".out_altvoltage3_2B_scale",
	DAQ3_DAC_DEVICE".out_altvoltage0_1A_phase",
	DAQ3_DAC_DEVICE".out_altvoltage1_1B_phase",
	DAQ3_DAC_DEVICE".out_altvoltage2_2A_phase",
	DAQ3_DAC_DEVICE".out_altvoltage3_2B_phase",
};

static const char * daq2_driver_attribs[] = {
	"dds_mode",
	"tx_channel_0",
	"tx_channel_1",
	"dac_buf_filename",
};

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_device_sampling_freq(ADC_DEVICE,
		USE_INTERN_SAMPLING_FREQ);
}

static int compare_gain(const char *a, const char *b) __attribute__((unused));
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

static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	iio_w->save(iio_w);
}

static void make_widget_update_signal_based(struct iio_widget *widgets,
	unsigned int num_widgets)
{
	char signal_name[25];
	unsigned int i;

	for (i = 0; i < num_widgets; i++) {
		if (GTK_IS_CHECK_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_TOGGLE_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_SPIN_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "value-changed");
		else if (GTK_IS_COMBO_BOX_TEXT(widgets[i].widget))
			sprintf(signal_name, "%s", "changed");
		else
			printf("unhandled widget type, attribute: %s\n", widgets[i].attr_name);

		if (GTK_IS_SPIN_BUTTON(widgets[i].widget) &&
			widgets[i].priv_progress != NULL) {
				iio_spin_button_progress_activate(&widgets[i]);
		} else {
			g_signal_connect(G_OBJECT(widgets[i].widget), signal_name, G_CALLBACK(save_widget_value), &widgets[i]);
		}
	}
}

static int daq2_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	if (MATCH_ATTRIB("dds_mode")) {
		dac_data_manager_set_dds_mode(dac_tx_manager,
				DAC_DEVICE, 1, atoi(value));
	} else if (!strncmp(attrib, "tx_channel_", sizeof("tx_channel_") - 1)) {
		int tx = atoi(attrib + sizeof("tx_channel_") - 1);
		dac_data_manager_set_tx_channel_state(
				dac_tx_manager, tx, !!atoi(value));
	} else if (MATCH_ATTRIB("dac_buf_filename")) {
		if (dac_data_manager_get_dds_mode(dac_tx_manager,
					DAC_DEVICE, 1) == DDS_BUFFER)
			dac_data_manager_set_buffer_chooser_filename(
					dac_tx_manager, value);
	} else if (MATCH_ATTRIB("SYNC_RELOAD")) {
		if (can_update_widgets) {
			rx_update_values();
			tx_update_values();
			dac_data_manager_update_iio_widgets(dac_tx_manager);
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static int daq2_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			daq2_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(daq2_driver_attribs); i++) {
		char *value = read_token_from_ini(ini_fn, THIS_DRIVER,
				daq2_driver_attribs[i]);
		if (value) {
			daq2_handle_driver(NULL, daq2_driver_attribs[i], value);
			free(value);
		}
	}

	update_from_ini(ini_fn, THIS_DRIVER, dac, daq_sr_attribs,
			sr_attribs_array_size);
	update_from_ini(ini_fn, THIS_DRIVER, adc, daq_sr_attribs,
			sr_attribs_array_size);

	if (can_update_widgets) {
		rx_update_values();
		tx_update_values();
		dac_data_manager_update_iio_widgets(dac_tx_manager);
	}
}

static GtkWidget * daq2_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *daq2_panel;
	GtkWidget *dds_container;
	GtkLabel *adc_freq_label, *dac_freq_label;
	struct iio_channel *ch0, *ch1;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	dac = iio_context_find_device(ctx, DAC_DEVICE);
	adc = iio_context_find_device(ctx, ADC_DEVICE);

	dac_tx_manager = dac_data_manager_new(dac, NULL, ctx);
	if (!dac_tx_manager) {
		osc_destroy_context(ctx);
		return NULL;
	}

	builder = gtk_builder_new();

	if (osc_load_glade_file(builder, "daq2") < 0)
		return NULL;

	daq2_panel = GTK_WIDGET(gtk_builder_get_object(builder, "daq2_panel"));
	dds_container = GTK_WIDGET(gtk_builder_get_object(builder, "dds_transmit_block"));
	gtk_container_add(GTK_CONTAINER(dds_container), dac_data_manager_get_gui_container(dac_tx_manager));
	gtk_widget_show_all(dds_container);

	if (ini_fn)
		load_profile(NULL, ini_fn);

	/* Bind the IIO device files to the GUI widgets */

	char attr_val[256];
	long long val;
	double tx_sampling_freq;

	/* Rx Widgets */

	ch0 = iio_device_find_channel(adc, "voltage0", false);
	ch1 = iio_device_find_channel(adc, "voltage1", false);

	if (iio_channel_attr_read_longlong(ch0, "sampling_frequency", &val) == 0)
		snprintf(attr_val, sizeof(attr_val), "%.3f", (double)val / 1000000.0);
	else
		snprintf(attr_val, sizeof(attr_val), "%s", "error");

	adc_freq_label = GTK_LABEL(gtk_builder_get_object(builder, "adc_frequency_label"));
	gtk_label_set_text(adc_freq_label, attr_val);

	iio_combo_box_init_from_builder(&rx_widgets[num_rx++],
		adc, ch0, "test_mode", "test_mode_available", builder,
		"ch0_test_mode", NULL);
	iio_combo_box_init_from_builder(&rx_widgets[num_rx++],
		adc, ch1, "test_mode", "test_mode_available", builder,
		"ch1_test_mode", NULL);

	/* Tx Widgets */
	ch0 = iio_device_find_channel(dac, "altvoltage0", true);

	if (iio_channel_attr_read_longlong(ch0, "sampling_frequency", &val) == 0) {
		tx_sampling_freq = (double)val / 1000000.0;
		snprintf(attr_val, sizeof(attr_val), "%.3f", tx_sampling_freq);
	} else {
		snprintf(attr_val, sizeof(attr_val), "%s", "error");
		tx_sampling_freq = 0;
	}

	dac_freq_label = GTK_LABEL(gtk_builder_get_object(builder, "dac_frequency_label"));
	gtk_label_set_text(dac_freq_label, attr_val);

	make_widget_update_signal_based(rx_widgets, num_rx);
	make_widget_update_signal_based(tx_widgets, num_tx);

	dac_data_manager_freq_widgets_range_update(dac_tx_manager, tx_sampling_freq / 2);

	tx_update_values();
	rx_update_values();
	dac_data_manager_update_iio_widgets(dac_tx_manager);

	dac_data_manager_set_buffer_size_alignment(dac_tx_manager, 16);
	dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager, OSC_WAVEFORM_FILE_PATH);

	block_diagram_init(builder, 4,
			"AD9680_11752-001.svg", "AD9144_11675-002.svg",
			"AD9523_09278-020.svg", "AD-FMCDAQ2-EBZ.jpg");

	can_update_widgets = true;

	return daq2_panel;
}

static void save_widgets_to_ini(FILE *f)
{
	fprintf(f, "dds_mode = %i\n"
			"dac_buf_filename = %s\n"
			"tx_channel_0 = %i\n"
			"tx_channel_1 = %i\n",
			dac_data_manager_get_dds_mode(dac_tx_manager, DAC_DEVICE, 1),
			dac_data_manager_get_buffer_chooser_filename(dac_tx_manager),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 0),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 1));
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		/* Write the section header */
		save_to_ini(f, THIS_DRIVER, dac, daq_sr_attribs,
				sr_attribs_array_size);
		save_to_ini(f, NULL, adc, daq_sr_attribs,
				sr_attribs_array_size);
		save_widgets_to_ini(f);
		fclose(f);
	}
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	save_profile(NULL, ini_fn);

	if (dac_tx_manager) {
		dac_data_manager_free(dac_tx_manager);
		dac_tx_manager = NULL;
	}

	osc_destroy_context(ctx);
}

static bool daq2_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	if (iio_context_find_device(osc_ctx, DAQ1_DAC_DEVICE)) {
		daq_board = 1;
		ADC_DEVICE = DAQ1_ADC_DEVICE;
		DAC_DEVICE = DAQ1_DAC_DEVICE;
		daq_sr_attribs = daq1_sr_attribs;
		sr_attribs_array_size = ARRAY_SIZE(daq1_sr_attribs);
	} else if (iio_context_find_device(osc_ctx, DAQ2_DAC_DEVICE)) {
		daq_board = 2;
		ADC_DEVICE = DAQ2_ADC_DEVICE;
		DAC_DEVICE = DAQ2_DAC_DEVICE;
		daq_sr_attribs = daq2_sr_attribs;
		sr_attribs_array_size = ARRAY_SIZE(daq2_sr_attribs);
	} else if (iio_context_find_device(osc_ctx, DAQ3_DAC_DEVICE)) {
		daq_board = 3;
		ADC_DEVICE = DAQ3_ADC_DEVICE;
		DAC_DEVICE = DAQ3_DAC_DEVICE;
		daq_sr_attribs = daq3_sr_attribs;
		sr_attribs_array_size = ARRAY_SIZE(daq3_sr_attribs);
	} else {
		ADC_DEVICE = "";
		DAC_DEVICE = "";
	}

	return !!iio_context_find_device(osc_ctx, DAC_DEVICE) &&
		!!iio_context_find_device(osc_ctx, ADC_DEVICE);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = daq2_identify,
	.init = daq2_init,
	.handle_item = daq2_handle,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
