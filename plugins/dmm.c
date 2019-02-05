/**
* Copyright (C) 2012-2013 Analog Devices, Inc.
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
#include <string.h>
#include <unistd.h>

#include <iio.h>

#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"

static GtkWidget *dmm_results;
static GtkWidget *select_all_channels;
static GtkWidget *dmm_button;
static GtkWidget *device_list_widget;
static GtkListStore *device_list_store;
static GtkListStore *channel_list_store;
static gint this_page;
static GtkNotebook *nbook;
static gboolean plugin_detached;

static struct iio_context *ctx;

static bool is_valid_dmm_channel(struct iio_channel *chn)
{
	const char *id;

	if (iio_channel_is_output(chn))
		return false;

	/* find the name */
	id = iio_channel_get_id(chn);

	/* some temps don't have 'raw' */
	if (!strstr(id, "temp") && !iio_channel_find_attr(chn, "raw"))
		return false;

	/* Must have 'scale', or be a temperature, which doesn't need scale */
	if (!strstr(id, "temp") && !iio_channel_find_attr(chn, "scale"))
		return false;

	return true;
}

static struct iio_device * get_device(const char *id)
{
	unsigned int i, nb = iio_context_get_devices_count(ctx);
	for (i = 0; i < nb; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		const char *name = iio_device_get_name(dev);

		if (!strcmp(id, iio_device_get_id(dev)) ||
				(name && !strcmp(name, id)))
			return dev;
	}
	return NULL;
}

static struct iio_channel * get_channel(const struct iio_device *dev,
		const char *id)
{
	unsigned int i, nb = iio_device_get_channels_count(dev);
	for (i = 0; i < nb; i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		const char *name = iio_channel_get_name(chn);

		if (iio_channel_is_output(chn))
			continue;

		if (!strcmp(id, iio_channel_get_id(chn)) ||
				(name && !strcmp(name, id)))
			return chn;
	}
	return NULL;
}

static double read_double_attr(const struct iio_channel *chn, const char *name)
{
	double val;
	int ret = iio_channel_attr_read_double(chn, name, &val);
	return ret < 0 ? -1.0 : val;
}

static void build_channel_list(void)
{
	GtkTreeIter iter, iter2, iter3;
	unsigned int enabled;
	char *device, *device2;
	gboolean first = FALSE, iter3_valid = FALSE, loop, loop2, all = FALSE;
	char dev_ch[256];

	loop = gtk_tree_model_get_iter_first(GTK_TREE_MODEL (device_list_store), &iter);
	gtk_list_store_clear(channel_list_store);

	while (loop) {
		gtk_tree_model_get(GTK_TREE_MODEL (device_list_store), &iter, 0, &device, 1, &enabled, -1);
		if (enabled) {
			struct iio_device *dev;
			unsigned int i, nb_channels;

			all = true;
			/* is it already in the list? */
			loop2 = gtk_tree_model_get_iter_first(GTK_TREE_MODEL (channel_list_store), &iter2);

			if (loop2) {
				first = TRUE;
				iter3 = iter2;
				iter3_valid = TRUE;
			}

			while (loop2) {
				gtk_tree_model_get(GTK_TREE_MODEL (channel_list_store), &iter2, 2, &device2, -1);
				if (!strcmp(device, device2))
					break;
				if (strcmp(device, device2) >= 0) {
					first = FALSE;
					iter3 = iter2;
				}
				g_free(device2);
				loop2 = gtk_tree_model_iter_next(GTK_TREE_MODEL (channel_list_store), &iter2);
			}

			/* it is, so skip the rest */
			if (loop2) {
				loop = gtk_tree_model_iter_next(GTK_TREE_MODEL (device_list_store), &iter);
				continue;
			}

			dev = get_device(device);
			if (!dev)
				continue;

			nb_channels = iio_device_get_channels_count(dev);
			for (i = 0; i < nb_channels; i++) {
				struct iio_channel *chn =
					iio_device_get_channel(dev, i);
				const char *name, *id, *devid;

				/* Must be input */
				if (!is_valid_dmm_channel(chn))
					continue;

				/* find the name */
				devid = iio_device_get_id(dev);
				name = iio_channel_get_name(chn);
				id = iio_channel_get_id(chn);
				if (!name)
					name = id;

				if (iter3_valid) {
					if (first) {
						gtk_list_store_insert_before(channel_list_store, &iter2, &iter3);
						first = FALSE;
					} else if(gtk_tree_model_iter_next(GTK_TREE_MODEL (channel_list_store), &iter3))
						gtk_list_store_insert_before(channel_list_store, &iter2, &iter3);
					else
						gtk_list_store_append(channel_list_store, &iter2);
				} else {
					gtk_list_store_append(channel_list_store, &iter2);
					iter3_valid = TRUE;
				}

				snprintf(dev_ch, sizeof(dev_ch), "%s:%s", 
					device, name);
				
				gtk_list_store_set(channel_list_store, &iter2,
						0, dev_ch,	/* device & channel name */
						1, 0,		/* On/Off */
						2, devid,	/* device ID */
						3, id,		/* channel ID */
							-1);
				iter3 = iter2;
			}
		} else {
			loop2 = gtk_tree_model_get_iter_first(GTK_TREE_MODEL (channel_list_store), &iter2);
			while (loop2) {
				gtk_tree_model_get(GTK_TREE_MODEL (channel_list_store), &iter2, 2, &device2, -1);
				if (!strcmp(device, device2)) {
					loop2 = gtk_list_store_remove(channel_list_store, &iter2);
					continue;
				}
				loop2 = gtk_tree_model_iter_next(GTK_TREE_MODEL (channel_list_store), &iter2);
			}
		}
		loop = gtk_tree_model_iter_next(GTK_TREE_MODEL (device_list_store), &iter);
	}

	gtk_tree_sortable_set_sort_column_id(
		GTK_TREE_SORTABLE(GTK_TREE_MODEL(channel_list_store)),
		0, GTK_SORT_ASCENDING);

	if (all)
		gtk_widget_show(select_all_channels);
	else
		gtk_widget_hide(select_all_channels);
}


static void device_toggled(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer data)
{
	GtkTreePath* path = gtk_tree_path_new_from_string(pathStr);
	GtkTreeIter iter;
	unsigned int enabled;

	gtk_tree_model_get_iter(GTK_TREE_MODEL (data), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL (data), &iter, 1, &enabled, -1);
	enabled = !enabled;
	gtk_list_store_set(GTK_LIST_STORE (data), &iter, 1, enabled, -1);

	build_channel_list();
}

static void channel_toggle(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer data)
{
	GtkTreePath* path = gtk_tree_path_new_from_string(pathStr);
	GtkTreeIter iter;
	unsigned int enabled;

	gtk_tree_model_get_iter(GTK_TREE_MODEL (data), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL (data), &iter, 1, &enabled, -1);
	enabled = !enabled;
	gtk_list_store_set(GTK_LIST_STORE (data), &iter, 1, enabled, -1);

}

static void pick_all_channels(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer data)
{
	gboolean loop;
	unsigned int enabled;
	GtkTreeIter iter;

	loop = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(channel_list_store), &iter);
	while (loop) {
		gtk_tree_model_get(GTK_TREE_MODEL (channel_list_store), &iter, 1, &enabled, -1);
		enabled = true;
		gtk_list_store_set(GTK_LIST_STORE (channel_list_store), &iter, 1, enabled, -1);
		loop = gtk_tree_model_iter_next(GTK_TREE_MODEL (channel_list_store), &iter);
	}

}

static void init_device_list(void)
{
	unsigned int i, num = iio_context_get_devices_count(ctx);
	GtkTreeIter iter;

	gtk_list_store_clear(device_list_store);

	for (i = 0; i < num; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		unsigned int j, nch = iio_device_get_channels_count(dev);
		const char *id;
		bool found_valid_chan = false;

		for (j = 0; !found_valid_chan && j < nch; j++) {
			struct iio_channel *chn =
				iio_device_get_channel(dev, j);
			found_valid_chan = is_valid_dmm_channel(chn);
		}

		if (!found_valid_chan)
			continue;

		id = iio_device_get_name(dev);
		if (!id)
			id = iio_device_get_id(dev);

		gtk_list_store_append(device_list_store, &iter);
		gtk_list_store_set(device_list_store, &iter, 0, id,  1, 0, -1);
	}
	gtk_tree_sortable_set_sort_column_id(
		GTK_TREE_SORTABLE(GTK_TREE_MODEL(device_list_store)),
		0, GTK_SORT_ASCENDING);
}

static gboolean dmm_update_loop_running;

static gboolean dmm_update(gpointer foo)
{

	GtkTreeIter tree_iter;
	char *name, *device, *channel, tmp[128];
	gboolean loop, enabled;
	double value;

	GtkTextBuffer *buf;
	GtkTextIter text_iter;

	/* start at the top every time */
	buf = gtk_text_buffer_new(NULL);
	gtk_text_buffer_get_iter_at_offset(buf, &text_iter, 0);

	if (this_page == gtk_notebook_get_current_page(nbook) || plugin_detached) {
		loop = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(channel_list_store), &tree_iter);
		while (loop) {
			gtk_tree_model_get(GTK_TREE_MODEL(channel_list_store), &tree_iter,
					0, &name,
					1, &enabled,
					2, &device,
					3, &channel,
					-1);
			if (enabled) {
				struct iio_device *dev =
					get_device(device);
				struct iio_channel *chn =
					get_channel(dev, channel);

				if (iio_channel_find_attr(chn, "raw"))
					value = read_double_attr(chn, "raw");
				else if (iio_channel_find_attr(chn, "processed"))
					value = read_double_attr(chn, "processed");
				else if (iio_channel_find_attr(chn, "input"))
					value = read_double_attr(chn, "input");
				else {
					sprintf(tmp, "skipping %s", name);
					goto dmm_update_next;
				}

				if (iio_channel_find_attr(chn, "offset"))
					value += read_double_attr(chn,
							"offset");
				if (iio_channel_find_attr(chn, "scale"))
					value *= read_double_attr(chn,
							"scale");

				if (!strncmp(channel, "voltage", 7))
					sprintf(tmp, "%s = %f Volts\n", name, value / 1000);
				else if (!strncmp(channel, "temp", 4))
					sprintf(tmp, "%s = %3.2f °C\n", name, value / 1000);
				else if (!strncmp(channel, "current", 4))
					sprintf(tmp, "%s = %f Milliampere\n", name, value);
				else if (!strncmp(channel, "accel", 5))
					sprintf(tmp, "%s = %f m/s²\n", name, value);
				else if (!strncmp(channel, "anglvel", 7))
					sprintf(tmp, "%s = %f rad/s\n", name, value);
				else if (!strncmp(channel, "pressure", 8))
					sprintf(tmp, "%s = %f kPa\n", name, value);
				else if (!strncmp(channel, "magn", 4))
					sprintf(tmp, "%s = %f Gauss\n", name, value);
				else
					sprintf(tmp, "%s = %f\n", name, value);

				gtk_text_buffer_insert(buf, &text_iter, tmp, -1);
			}
dmm_update_next:
			loop = gtk_tree_model_iter_next(GTK_TREE_MODEL(channel_list_store), &tree_iter);
		}

		gtk_text_view_set_buffer(GTK_TEXT_VIEW(dmm_results), buf);
		g_object_unref(buf);
	}

	return dmm_update_loop_running;
}

static void dmm_button_clicked(GtkToggleToolButton *btn, gpointer data)
{
	dmm_update_loop_running = gtk_toggle_tool_button_get_active(btn);
	if (dmm_update_loop_running)
		g_timeout_add(500, (GSourceFunc) dmm_update, ctx);
}

static gboolean dmm_button_icon_transform(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer user_data)
{
	if (g_value_get_boolean(source_value))
		g_value_set_static_string(target_value, "gtk-stop");
	else
		g_value_set_static_string(target_value, "gtk-media-play");

	return TRUE;
}

/*
 *  Main function
 */
static GtkWidget * dmm_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *dmm_panel;

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	if (osc_load_glade_file(builder, "dmm") < 0)
		return NULL;

	dmm_panel = GTK_WIDGET(gtk_builder_get_object(builder, "dmm_panel"));
	device_list_widget = GTK_WIDGET(gtk_builder_get_object(builder, "device_list_view"));
	device_list_store = GTK_LIST_STORE(gtk_builder_get_object(builder, "device_list"));

	dmm_button = GTK_WIDGET(gtk_builder_get_object(builder, "dmm_button"));
	channel_list_store = GTK_LIST_STORE(gtk_builder_get_object(builder, "channel_list"));

	dmm_results = GTK_WIDGET(gtk_builder_get_object(builder, "dmm_results"));
	select_all_channels = GTK_WIDGET(gtk_builder_get_object(builder, "all_channels"));

	g_builder_connect_signal(builder, "device_toggle", "toggled",
			G_CALLBACK(device_toggled), device_list_store);
	g_builder_connect_signal(builder, "channel_toggle", "toggled",
			G_CALLBACK(channel_toggle), channel_list_store);
	g_builder_connect_signal(builder, "all_channels", "clicked",
			G_CALLBACK(pick_all_channels), channel_list_store);

	g_builder_connect_signal(builder, "dmm_button", "toggled",
			G_CALLBACK(dmm_button_clicked), channel_list_store);

	g_builder_bind_property(builder, "dmm_button", "active",
			"channel_list_view", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "dmm_button", "active",
			"device_list_view", "sensitive", G_BINDING_INVERT_BOOLEAN);

	g_object_bind_property_full(dmm_button, "active", dmm_button,
			"stock-id", 0, dmm_button_icon_transform, NULL, NULL, NULL);

	gtk_widget_show_all(dmm_panel);
	gtk_widget_hide(select_all_channels);

	init_device_list();

	/* we are looking for almost random numbers, so this will work */
	srand((unsigned int)time(NULL));

	return dmm_panel;
}

static int dmm_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	GtkTreeIter iter;
	char *device, *channel, tmp[256];
	gboolean loop;
	unsigned int enabled;

	if (MATCH_ATTRIB("device_list")) {
		GtkTreeModel *model = GTK_TREE_MODEL(device_list_store);
		loop = gtk_tree_model_get_iter_first(model, &iter);
		while (loop) {
			gtk_tree_model_get(model, &iter, 0,
					&device, 1, &enabled, -1);
			/* load/restore */
			if (strstr(value, device)) {
				enabled = value[strlen(value) - 1] == '1';
				gtk_list_store_set(device_list_store,
						&iter, 1, enabled, -1);
			}
			g_free(device);
			loop = gtk_tree_model_iter_next(model, &iter);
		}
		build_channel_list();

	} else if (MATCH_ATTRIB("channel_list")) {
		GtkTreeModel *model = GTK_TREE_MODEL(channel_list_store);
		loop = gtk_tree_model_get_iter_first(model, &iter);
		while (loop) {
			gtk_tree_model_get(model, &iter, 1,
					&enabled, 2, &device, 3, &channel, -1);
			/* load/restore */
			sprintf(tmp, "%s:%s", device, channel);
			if (strstr(value, tmp)) {
				enabled = 1;
				gtk_list_store_set(channel_list_store,
					&iter, 1, enabled, -1);
			}
			g_free(channel);
			g_free(device);
			loop = gtk_tree_model_iter_next(model, &iter);
		}

	} else if (MATCH_ATTRIB("running")) {
		/* load/restore */
		gtk_toggle_tool_button_set_active(
				GTK_TOGGLE_TOOL_BUTTON(dmm_button),
				!strcmp(value, "Yes"));
	} else {
		return -EINVAL;
	}

	return 0;
}

static int dmm_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line,
			attrib, value, dmm_handle_driver, NULL);
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	g_source_remove_by_user_data(ctx);
	osc_destroy_context(ctx);
}

static bool dmm_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();
	unsigned int i, num;
	bool ret = false;

	num = iio_context_get_devices_count(osc_ctx);
	for (i = 0; !ret && i < num; i++) {
		struct iio_device *dev = iio_context_get_device(osc_ctx, i);
		unsigned int j, nch = iio_device_get_channels_count(dev);

		for (j = 0; !ret && j < nch; j++) {
			struct iio_channel *chn =
				iio_device_get_channel(dev, j);
			if (is_valid_dmm_channel(chn))
				ret = true;
		}
	}

	return ret;
}

struct osc_plugin plugin = {
	.name = "DMM",
	.identify = dmm_identify,
	.init = dmm_init,
	.handle_item = dmm_handle,
	.update_active_page = update_active_page,
	.destroy = context_destroy,
};
