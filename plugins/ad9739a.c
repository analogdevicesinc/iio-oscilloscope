
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

#define DAC_DEVICE "axi-ad9739a-hpc"

static struct dac_data_manager *dac_tx_manager;

static struct iio_context *ctx;
static struct iio_device *dac;

static struct iio_widget tx_widgets[20];
static unsigned int num_tx;

static bool can_update_widgets;

static const char *ad9739a_sr_attribs[] = {
	DAC_DEVICE".full_scale_current",
	DAC_DEVICE".operation_mode",
	DAC_DEVICE".out_altvoltage0_1A_frequency",
	DAC_DEVICE".out_altvoltage1_1B_frequency",
	DAC_DEVICE".out_altvoltage0_1A_scale",
	DAC_DEVICE".out_altvoltage1_1B_scale",
	DAC_DEVICE".out_altvoltage0_1A_phase",
	DAC_DEVICE".out_altvoltage1_1B_phase",
};

static const char * ad9739a_driver_attribs[] = {
	"dds_mode",
	"tx_channel_0",
	"dac_buf_filename",
};

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

static void update_widgets(void)
{
	iio_update_widgets_of_device(tx_widgets, num_tx, dac);
}

static void reload_button_clicked(GtkButton *btn, gpointer data)
{
	update_widgets();
	dac_data_manager_update_iio_widgets(dac_tx_manager);
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

static int ad9739a_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
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
			ARRAY_SIZE(ad9739a_sr_attribs));

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

	dac = iio_context_find_device(ctx, DAC_DEVICE);
	dac_tx_manager = dac_data_manager_new(dac, NULL, ctx);
	if (!dac_tx_manager) {
		osc_destroy_context(ctx);
		return NULL;
	}

	builder = gtk_builder_new();
	if (osc_load_glade_file(builder, "ad9739a") < 0)
		return NULL;

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
	update_widgets();

	make_widget_update_signal_based(tx_widgets, num_tx);
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
			dac_data_manager_get_dds_mode(dac_tx_manager, DAC_DEVICE, 1),
			dac_data_manager_get_buffer_chooser_filename(dac_tx_manager),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 0));
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		/* Write the section header */
		save_to_ini(f, THIS_DRIVER, dac, ad9739a_sr_attribs,
				ARRAY_SIZE(ad9739a_sr_attribs));
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

static bool ad9739a_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();
	return !!iio_context_find_device(osc_ctx, DAC_DEVICE);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = ad9739a_identify,
	.init = ad9739a_init,
	.handle_item = ad9739a_handle,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
