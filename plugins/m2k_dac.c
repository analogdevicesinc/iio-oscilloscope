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
#include <malloc.h>
#include <sys/stat.h>
#include <string.h>

#include <iio.h>

#include "../osc.h"
#include "../iio_widget.h"
#include "../libini2.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "dac_data_manager.h"

#define THIS_DRIVER "M2K DAC"
#define DAC_DEVICE "m2k-dac"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

static struct dac_data_manager *dac_tx_manager;
static struct iio_context *ctx;
static struct iio_device *dac;

static gint this_page;
static GtkWidget *m2k_panel;
static gboolean plugin_detached;

static const char *m2k_sr_attribs[] = {
	DAC_DEVICE".out_altvoltage0_1A_frequency",
	DAC_DEVICE".out_altvoltage0_1A_phase",
	DAC_DEVICE".out_altvoltage0_1A_raw",
	DAC_DEVICE".out_altvoltage0_1A_scale",
	DAC_DEVICE".out_altvoltage1_1B_frequency",
	DAC_DEVICE".out_altvoltage1_1B_phase",
	DAC_DEVICE".out_altvoltage1_1B_raw",
	DAC_DEVICE".out_altvoltage1_1B_scale",
	DAC_DEVICE".out_altvoltage2_2A_frequency",
	DAC_DEVICE".out_altvoltage2_2A_phase",
	DAC_DEVICE".out_altvoltage2_2A_raw",
	DAC_DEVICE".out_altvoltage2_2A_scale",
	DAC_DEVICE".out_altvoltage3_2B_frequency",
	DAC_DEVICE".out_altvoltage3_2B_phase",
	DAC_DEVICE".out_altvoltage3_2B_raw",
	DAC_DEVICE".out_altvoltage3_2B_scale",
};

static const char * m2k_dac_driver_attribs[] = {
	"load_fir_filter_file",
	"dds_mode_tx1",
	"tx_channel_0",
	"tx_channel_1",
	"dac_buf_filename",
};

static GtkWidget * m2k_init(GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *dds_container;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	dac = iio_context_find_device(ctx, DAC_DEVICE);

	dac_tx_manager = dac_data_manager_new(dac, NULL, ctx);
	if (!dac_tx_manager) {
		iio_context_destroy(ctx);
		return NULL;
	}

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "m2k.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "m2k.glade", NULL);

	m2k_panel = GTK_WIDGET(gtk_builder_get_object(builder, "m2k_panel"));
	dds_container = GTK_WIDGET(gtk_builder_get_object(builder, "dds_transmit_block"));
	gtk_container_add(GTK_CONTAINER(dds_container), dac_data_manager_get_gui_container(dac_tx_manager));
	gtk_widget_show_all(dds_container);

	/* Bind the IIO device files to the GUI widgets */
	double tx_sampling_freq;

	tx_sampling_freq = 1E6;

	dac_data_manager_freq_widgets_range_update(dac_tx_manager, tx_sampling_freq / 2);
	dac_data_manager_update_iio_widgets(dac_tx_manager);
	dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager, OSC_WAVEFORM_FILE_PATH);

	return m2k_panel;
}

static int m2k_handle_driver(const char *attrib, const char *value)
{
	int ret = 0;

	if (MATCH_ATTRIB("dds_mode_tx1")) {
		dac_data_manager_set_dds_mode(dac_tx_manager,
				DAC_DEVICE, 1, atoi(value));
	} else if (!strncmp(attrib, "tx_channel_", sizeof("tx_channel_") - 1)) {
		int tx = atoi(attrib + sizeof("tx_channel_") - 1);
		dac_data_manager_set_tx_channel_state(
				dac_tx_manager, tx, !!atoi(value));
	} else if (MATCH_ATTRIB("dac_buf_filename")) {
		dac_data_manager_set_buffer_chooser_filename(
				dac_tx_manager, value);
	} else {
		return -EINVAL;
	}

	return ret;
}

static int m2k_handle(int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			m2k_handle_driver);
}

static void load_profile(const char *ini_fn)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(m2k_dac_driver_attribs); i++) {
		char *value = read_token_from_ini(ini_fn, THIS_DRIVER,
				m2k_dac_driver_attribs[i]);
		if (value) {
			m2k_handle_driver(
					m2k_dac_driver_attribs[i], value);
			free(value);
		}
	}

	update_from_ini(ini_fn, THIS_DRIVER, dac, m2k_sr_attribs,
			ARRAY_SIZE(m2k_sr_attribs));
}

static void save_widgets_to_ini(FILE *f)
{
	char buf[0x1000];

	snprintf(buf, sizeof(buf),
			"dds_mode_tx1 = %i\n"
			"tx_channel_0 = %i\n"
			"tx_channel_1 = %i\n"
			"dac_buf_filename = %s\n",
			dac_data_manager_get_dds_mode(dac_tx_manager, DAC_DEVICE, 1),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 0),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 1),
			dac_data_manager_get_buffer_chooser_filename(dac_tx_manager));
	fwrite(buf, 1, strlen(buf), f);
}

static void save_profile(const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		save_to_ini(f, THIS_DRIVER, dac, m2k_sr_attribs,
				ARRAY_SIZE(m2k_sr_attribs));

		save_widgets_to_ini(f);
		fclose(f);
	}
}

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void context_destroy(const char *ini_fn)
{
	g_source_remove_by_user_data(ctx);

	if (ini_fn)
		save_profile(ini_fn);

	if (dac_tx_manager) {
		dac_data_manager_free(dac_tx_manager);
		dac_tx_manager = NULL;
	}

	iio_context_destroy(ctx);
}

static bool m2k_identify(void)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	return !!iio_context_find_device(osc_ctx, DAC_DEVICE);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = m2k_identify,
	.init = m2k_init,
	.handle_item = m2k_handle,
	.update_active_page = update_active_page,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
