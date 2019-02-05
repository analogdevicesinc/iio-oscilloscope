/**
 * Copyright (C) 2017 Analog Devices, Inc.
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
//#include "./block_diagram.h"
#include "dac_data_manager.h"

#define THIS_DRIVER "FMCOMMS11"

#define SYNC_RELOAD "SYNC_RELOAD"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

#define ADC_DEVICE "axi-ad9625-hpc"
#define DAC_DEVICE "axi-ad9162-hpc"
#define ATTN_DEVICE "hmc1119"
#define VGA_DEVICE "adl5240"

/*
iio:device0/name:ad7291
iio:device1/name:xadc
iio:device2/name:adl5240
iio:device3/name:hmc1119
iio:device4/name:ad9508
iio:device5/name:adf4355
iio:device6/name:axi-ad9162-hpc
iio:device7/name:axi-ad9625-hpc
*/

static const gdouble mhz_scale = 1000000.0;

static struct dac_data_manager *dac_tx_manager;

static struct iio_context *ctx;
static struct iio_device *dac, *adc, *vga, *attn;

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static unsigned int num_tx, num_rx;

static bool can_update_widgets;

static const char **daq_sr_attribs;
static int sr_attribs_array_size;


static const char *fmcomms11_sr_attribs[] = {
	ADC_DEVICE".in_voltage_sampling_frequency",
	ADC_DEVICE".in_voltage0_test_mode",
	ADC_DEVICE".in_voltage_scale",
	DAC_DEVICE".out_altvoltage_sampling_frequency",
	DAC_DEVICE".out_altvoltage0_1A_frequency",
	DAC_DEVICE".out_altvoltage1_1B_frequency",
	DAC_DEVICE".out_altvoltage0_1A_scale",
	DAC_DEVICE".out_altvoltage1_1B_scale",
	DAC_DEVICE".out_altvoltage0_1A_phase",
	DAC_DEVICE".out_altvoltage1_1B_phase",
	DAC_DEVICE".out_altvoltage2_frequency_nco",
	DAC_DEVICE".out_voltage_fir85_enable",
	ATTN_DEVICE".out_voltage0_hardwaregain",
	VGA_DEVICE".out_voltage0_hardwaregain",
};

static const char * fmcomms11_driver_attribs[] = {
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

static int fmcomms11_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
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

static int fmcomms11_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			fmcomms11_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(fmcomms11_driver_attribs); i++) {
		char *value = read_token_from_ini(ini_fn, THIS_DRIVER,
				fmcomms11_driver_attribs[i]);
		if (value) {
			fmcomms11_handle_driver(NULL,
				fmcomms11_driver_attribs[i], value);
			free(value);
		}
	}

	update_from_ini(ini_fn, THIS_DRIVER, dac, daq_sr_attribs,
			sr_attribs_array_size);
	update_from_ini(ini_fn, THIS_DRIVER, adc, daq_sr_attribs,
			sr_attribs_array_size);
	update_from_ini(ini_fn, THIS_DRIVER, attn, daq_sr_attribs,
			sr_attribs_array_size);
	update_from_ini(ini_fn, THIS_DRIVER, vga, daq_sr_attribs,
			sr_attribs_array_size);

	if (can_update_widgets) {
		rx_update_values();
		tx_update_values();
		dac_data_manager_update_iio_widgets(dac_tx_manager);
	}
}

static GtkWidget * fmcomms11_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *fmcomms11_panel;
	GtkWidget *dds_container;
	GtkTextBuffer *adc_buff, *dac_buff;
	struct iio_channel *ch0;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	dac = iio_context_find_device(ctx, DAC_DEVICE);
	adc = iio_context_find_device(ctx, ADC_DEVICE);
	vga = iio_context_find_device(ctx, VGA_DEVICE);
	attn = iio_context_find_device(ctx, ATTN_DEVICE);

	dac_tx_manager = dac_data_manager_new(dac, NULL, ctx);
	if (!dac_tx_manager) {
		osc_destroy_context(ctx);
		return NULL;
	}

	builder = gtk_builder_new();

	if (osc_load_glade_file(builder, "fmcomms11") < 0)
		return NULL;

	fmcomms11_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms11_panel"));
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

	if (iio_channel_attr_read_longlong(ch0, "sampling_frequency", &val) == 0)
		snprintf(attr_val, sizeof(attr_val), "%.2f", (double)(val / 1000000ul));
	else
		snprintf(attr_val, sizeof(attr_val), "%s", "error");

	adc_buff = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(adc_buff, attr_val, -1);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(builder, "text_view_adc_freq")), adc_buff);

	iio_combo_box_init_from_builder(&rx_widgets[num_rx++],
		adc, ch0, "test_mode", "test_mode_available", builder,
		"ch0_test_mode", NULL);

	iio_combo_box_init_from_builder(&rx_widgets[num_rx++],
		adc, ch0, "scale", "scale_available", builder,
		"ch0_scales", NULL);

	/* Tx Widgets */
	ch0 = iio_device_find_channel(dac, "altvoltage0", true);

	if (iio_channel_attr_read_longlong(ch0, "sampling_frequency", &val) == 0) {
		tx_sampling_freq = (double)(val / 1000000ul);
		snprintf(attr_val, sizeof(attr_val), "%.2f", tx_sampling_freq);
	} else {
		snprintf(attr_val, sizeof(attr_val), "%s", "error");
		tx_sampling_freq = 0;
	}

	ch0 = iio_device_find_channel(dac, "altvoltage2", true);

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		dac, ch0, "frequency_nco", builder,
		"out_altvoltage2_frequency_nco", &mhz_scale);


	ch0 = iio_device_find_channel(dac, "voltage0", true);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		dac, ch0, "fir85_enable", builder,
		"out_voltage_fir85_enable", 0);

	ch0 = iio_device_find_channel(vga, "voltage0", true);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		vga, ch0, "hardwaregain", builder,
		"out_voltage0_hardwaregain_adl5240", NULL);

	ch0 = iio_device_find_channel(attn, "voltage0", true);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		attn, ch0, "hardwaregain", builder,
		"out_voltage0_hardwaregain_hmc1119", NULL);

	dac_buff = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(dac_buff, attr_val, -1);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(builder, "text_view_dac_freq")), dac_buff);

	make_widget_update_signal_based(rx_widgets, num_rx);
	make_widget_update_signal_based(tx_widgets, num_tx);

	dac_data_manager_freq_widgets_range_update(dac_tx_manager, tx_sampling_freq / 2);

	tx_update_values();
	rx_update_values();
	dac_data_manager_update_iio_widgets(dac_tx_manager);

	dac_data_manager_set_buffer_size_alignment(dac_tx_manager, 64);
	dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager, OSC_WAVEFORM_FILE_PATH);

// 	block_diagram_init(builder, 2, );

	can_update_widgets = true;

	return fmcomms11_panel;
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
		save_to_ini(f, NULL, attn, daq_sr_attribs,
			    sr_attribs_array_size);
		save_to_ini(f, NULL, vga, daq_sr_attribs,
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

static bool fmcomms11_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	daq_sr_attribs = fmcomms11_sr_attribs;
	sr_attribs_array_size = ARRAY_SIZE(fmcomms11_sr_attribs);


	return !!iio_context_find_device(osc_ctx, DAC_DEVICE) &&
		!!iio_context_find_device(osc_ctx, ADC_DEVICE) &&
		!!iio_context_find_device(osc_ctx, VGA_DEVICE) &&
		!!iio_context_find_device(osc_ctx, ATTN_DEVICE);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = fmcomms11_identify,
	.init = fmcomms11_init,
	.handle_item = fmcomms11_handle,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
