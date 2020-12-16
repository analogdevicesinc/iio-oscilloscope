/**
 * Copyright (C) 2019 Analog Devices, Inc.
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
#include "dac_data_manager.h"

#define THIS_DRIVER "DAC Data Manager"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

static struct dac_data_manager *dac_tx_manager;
static GSList *dac_tx_manager_list = NULL;

static struct iio_context *ctx;
static struct iio_device *dac;

static struct iio_widget tx_widgets[100];
static unsigned int num_tx;

static bool can_update_widgets;

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
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
			g_signal_connect(G_OBJECT(widgets[i].widget), signal_name,
					 G_CALLBACK(save_widget_value), &widgets[i]);
		}
	}
}

static bool generic_identify(const struct osc_plugin *plugin)
{
	return 1;
}

static GSList * get_dac_dev_names(void)
{
	GSList *node;
	GSList *dac_dev_names = NULL;
	struct osc_plugin *plugin;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin->get_dac_dev_names)
			dac_dev_names = g_slist_concat (dac_dev_names, plugin->get_dac_dev_names(plugin));
	}

	return dac_dev_names;
}

static gint compare_func(gconstpointer a, gconstpointer b)
{
	const char *pa = a, *pb = b;

	return strcmp(pa, pb);
}

static bool has_output_scan_elements(struct iio_device *dev)
{
	struct iio_channel *ch;
	unsigned int i;

	for (i = 0; i < iio_device_get_channels_count(dev); i++) {
		ch = iio_device_get_channel(dev, i);
		if (iio_channel_is_scan_element(ch) && iio_channel_is_output(ch))
			return true;
	}

	return false;
}

static GtkWidget * generic_init(struct osc_plugin *plugin, GtkWidget *notebook,
				const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *generic_panel;
	GtkWidget *dds_container;
	GtkWidget *dds_vbox;
	bool generic_en = false;
	int i, dev_count;
	GSList * dac_dev_names;
	const char *crt_dev_name;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	builder = gtk_builder_new();
	if (osc_load_glade_file(builder, "generic_dac") < 0) {
		osc_destroy_context(ctx);
		return NULL;
	}

	generic_panel = GTK_WIDGET(gtk_builder_get_object(builder, "generic_panel"));
	dds_vbox = GTK_WIDGET(gtk_builder_get_object(builder, "dds_vbox"));
	dac_dev_names = get_dac_dev_names();
	dev_count = iio_context_get_devices_count(ctx);

	for (i = 0; i < dev_count; i++) {
		dac = iio_context_get_device(ctx, i);
		if (!has_output_scan_elements(dac))
			continue;

		crt_dev_name = iio_device_get_name(dac);
		if (!crt_dev_name)
			continue;

		if (g_slist_find_custom(dac_dev_names, crt_dev_name, compare_func))
			continue;

		dac_tx_manager = dac_data_manager_new(dac, NULL, ctx);
		if (!dac_tx_manager)
			continue;

		generic_en = true;

		dds_container = gtk_frame_new("DDS");
		gtk_container_add(GTK_CONTAINER(dds_vbox), dds_container);
		gtk_container_add(GTK_CONTAINER(dds_container),
				  dac_data_manager_get_gui_container(dac_tx_manager));
		gtk_widget_show_all(dds_container);

		dac_data_manager_freq_widgets_range_update(dac_tx_manager, INT_MAX / 2.0);
		dac_data_manager_update_iio_widgets(dac_tx_manager);

		make_widget_update_signal_based(tx_widgets, num_tx);

		tx_update_values();
		dac_data_manager_update_iio_widgets(dac_tx_manager);

		dac_data_manager_set_buffer_size_alignment(dac_tx_manager, 16);
		dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager,
				OSC_WAVEFORM_FILE_PATH);

		dac_tx_manager_list = g_slist_append(dac_tx_manager_list, (gpointer) dac_tx_manager);

		can_update_widgets = true;
	}

	g_slist_free(dac_dev_names);

	if (!generic_en) {
		osc_destroy_context(ctx);
		return NULL;
	}

	return generic_panel;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	GSList *node;

	for (node = dac_tx_manager_list; node; node = g_slist_next(node)) {
		dac_tx_manager = node->data;
		dac_data_manager_free(dac_tx_manager);
		dac_tx_manager = NULL;
	}
	g_slist_free(dac_tx_manager_list);
	dac_tx_manager_list = NULL;

	osc_destroy_context(ctx);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = generic_identify,
	.init = generic_init,
	.destroy = context_destroy,
};
