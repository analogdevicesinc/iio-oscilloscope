
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
#include "dac_data_manager.h"

#define THIS_DRIVER "ad9739a"

#define SYNC_RELOAD "SYNC_RELOAD"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

static const char *dac_name;

#define LPC_DAC_DEVICE "axi-ad9739a-lpc"
#define HPC_DAC_DEVICE "axi-ad9739a-hpc"

static struct dac_data_manager *dac_tx_manager;

static struct iio_context *ctx;
static struct iio_device *dac;

static struct iio_widget tx_widgets[20];
static unsigned int num_tx;

static bool can_update_widgets;

static const char **ad9739a_sr_attribs;
static int sr_attribs_array_size;

static const char *lpc_ad9739a_sr_attribs[] = {
	LPC_DAC_DEVICE".full_scale_current",
	LPC_DAC_DEVICE".operation_mode",
	LPC_DAC_DEVICE".out_altvoltage0_1A_frequency",
	LPC_DAC_DEVICE".out_altvoltage1_1B_frequency",
	LPC_DAC_DEVICE".out_altvoltage0_1A_scale",
	LPC_DAC_DEVICE".out_altvoltage1_1B_scale",
	LPC_DAC_DEVICE".out_altvoltage0_1A_phase",
	LPC_DAC_DEVICE".out_altvoltage1_1B_phase",
};

static const char *hpc_ad9739a_sr_attribs[] = {
	HPC_DAC_DEVICE".full_scale_current",
	HPC_DAC_DEVICE".operation_mode",
	HPC_DAC_DEVICE".out_altvoltage0_1A_frequency",
	HPC_DAC_DEVICE".out_altvoltage1_1B_frequency",
	HPC_DAC_DEVICE".out_altvoltage0_1A_scale",
	HPC_DAC_DEVICE".out_altvoltage1_1B_scale",
	HPC_DAC_DEVICE".out_altvoltage0_1A_phase",
	HPC_DAC_DEVICE".out_altvoltage1_1B_phase",
};

static const char * ad9739a_driver_attribs[] = {
	"dds_mode",
	"tx_channel_0",
	"dac_buf_filename",
};

static void reload_button_clicked(GtkButton *btn, gpointer data)
{
	iio_update_widgets_block_signals_by_data(tx_widgets, num_tx);
	dac_data_manager_update_iio_widgets(dac_tx_manager);
}

static int ad9739a_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	if (MATCH_ATTRIB("dds_mode")) {
		dac_data_manager_set_dds_mode(dac_tx_manager,
				dac_name, 1, atoi(value));
	} else if (!strncmp(attrib, "tx_channel_", sizeof("tx_channel_") - 1)) {
		int tx = atoi(attrib + sizeof("tx_channel_") - 1);
		dac_data_manager_set_tx_channel_state(
				dac_tx_manager, tx, !!atoi(value));
	} else if (MATCH_ATTRIB("dac_buf_filename")) {
		if (dac_data_manager_get_dds_mode(dac_tx_manager,
					dac_name, 1) == DDS_BUFFER)
			dac_data_manager_set_buffer_chooser_filename(
					dac_tx_manager, value);
	} else if (MATCH_ATTRIB("SYNC_RELOAD")) {
		if (can_update_widgets) {
			reload_button_clicked(NULL, NULL);
		}
	} else {
		return -EINVAL;
	}

	return 0;
}

static int ad9739a_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			ad9739a_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(ad9739a_driver_attribs); i++) {
		char *value = read_token_from_ini(ini_fn, THIS_DRIVER,
				ad9739a_driver_attribs[i]);
		if (value) {
			ad9739a_handle_driver(NULL, ad9739a_driver_attribs[i], value);
			free(value);
		}
	}

	update_from_ini(ini_fn, THIS_DRIVER, dac, ad9739a_sr_attribs,
			sr_attribs_array_size);

	if (can_update_widgets)
		reload_button_clicked(NULL, NULL);
}

static GtkWidget * ad9739a_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *ad9739a_panel;
	GtkWidget *dds_container;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	dac = iio_context_find_device(ctx, dac_name);
	dac_tx_manager = dac_data_manager_new(dac, NULL, ctx);
	if (!dac_tx_manager) {
		osc_destroy_context(ctx);
		return NULL;
	}

	builder = gtk_builder_new();
	if (osc_load_glade_file(builder, "ad9739a") < 0) {
		osc_destroy_context(ctx);
		return NULL;
	}

	ad9739a_panel = GTK_WIDGET(gtk_builder_get_object(builder, "ad9739a_panel"));
	dds_container = GTK_WIDGET(gtk_builder_get_object(builder, "dds_transmit_block"));
	gtk_container_add(GTK_CONTAINER(dds_container), dac_data_manager_get_gui_container(dac_tx_manager));
	gtk_widget_show_all(dds_container);

	/* Bind the IIO device files to the GUI widgets */

	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
		dac, NULL, "operation_mode", "operation_modes_available",
		 builder, "operation_modes_combo", NULL);

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		dac, NULL, "full_scale_current", builder,
		"full_scale_spin", NULL);

	if (ini_fn)
		load_profile(NULL, ini_fn);

	/* Update all widgets with current values */
	iio_update_widgets(tx_widgets, num_tx);

	iio_make_widgets_update_signal_based(tx_widgets, num_tx,
					     G_CALLBACK(iio_widget_save_block_signals_by_data_cb));
	g_builder_connect_signal(builder, "ad9739a_settings_reload", "clicked",
		G_CALLBACK(reload_button_clicked), NULL);

	dac_data_manager_freq_widgets_range_update(dac_tx_manager, 2E15 / 2);

	dac_data_manager_update_iio_widgets(dac_tx_manager);

	dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager, OSC_WAVEFORM_FILE_PATH);

	can_update_widgets = true;

	return ad9739a_panel;
}

static void save_widgets_to_ini(FILE *f)
{
	fprintf(f, "dds_mode = %i\n"
			"dac_buf_filename = %s\n"
			"tx_channel_0 = %i\n",
			dac_data_manager_get_dds_mode(dac_tx_manager, dac_name, 1),
			dac_data_manager_get_buffer_chooser_filename(dac_tx_manager),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 0));
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		/* Write the section header */
		save_to_ini(f, THIS_DRIVER, dac, ad9739a_sr_attribs,
				sr_attribs_array_size);
		save_widgets_to_ini(f);
		fclose(f);
	}
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	save_profile(NULL, ini_fn);
	dac_data_manager_free(dac_tx_manager);
	dac_tx_manager = NULL;
	osc_destroy_context(ctx);
}

static bool ad9739a_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	if (iio_context_find_device(osc_ctx, LPC_DAC_DEVICE)){
		dac_name = LPC_DAC_DEVICE;
		ad9739a_sr_attribs = lpc_ad9739a_sr_attribs;
		sr_attribs_array_size = ARRAY_SIZE(lpc_ad9739a_sr_attribs);
	} else if (iio_context_find_device(osc_ctx, HPC_DAC_DEVICE)) {
		dac_name = HPC_DAC_DEVICE;
		ad9739a_sr_attribs = hpc_ad9739a_sr_attribs;
		sr_attribs_array_size = ARRAY_SIZE(hpc_ad9739a_sr_attribs);
	} else {
		dac_name="";
	}

	return !!iio_context_find_device(osc_ctx, dac_name);
}

GSList* get_dac_dev_names(const struct osc_plugin *plugin) {
	GSList *list = NULL;

	list = g_slist_append (list, (gpointer) dac_name);

	return list;
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = ad9739a_identify,
	.init = ad9739a_init,
	.handle_item = ad9739a_handle,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
	.get_dac_dev_names = get_dac_dev_names,
};
