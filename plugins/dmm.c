/**
* Copyright (C) 2012-2013 Analog Devices, Inc.
*
* Licensed under the GPL-2.
*
**/

#include <stdio.h>

#include <gtk/gtk.h>
#include <glib/gthread.h>
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
#include <string.h>

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

		if (!strcmp(id, iio_channel_get_id(chn)) ||
				(name && !strcmp(name, id)))
			return chn;
	}
	return NULL;
}

static double read_double_attr(const struct iio_channel *chn, const char *name)
{
	unsigned int i, nb = iio_channel_get_attrs_count(chn);

	for (i = 0; i < nb; i++) {
		char buf[1024];
		size_t ret;
		const char *attr = iio_channel_get_attr(chn, i);
		if (strcmp(attr, name))
			continue;

		ret = iio_channel_attr_read(chn, attr, buf, sizeof(buf));
		if (ret < 0)
			return -1.0;
		return strtod(buf, NULL);
	}

	return -1.0;
}

static bool has_attribute(const struct iio_channel *chn, const char *name)
{
	unsigned int i, nb = iio_channel_get_attrs_count(chn);

	for (i = 0; i < nb; i++) {
		const char *attr = iio_channel_get_attr(chn, i);
		if (!strcmp(attr, name))
			return true;
	}
	return false;
}

static void build_channel_list(void)
{
	GtkTreeIter iter, iter2, iter3;
	unsigned int enabled;
	char *device, *device2;
	gboolean first = FALSE, iter3_valid = FALSE, loop, loop2, all = FALSE;

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
				char buf[1024], *scale;

				if (iio_channel_is_output(chn))
					continue;

				if (iio_channel_attr_read(chn, "scale",
							buf, sizeof(buf)) < 0)
					continue;

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

				scale = strdup(buf);

				devid = iio_device_get_id(dev);
				name = iio_channel_get_name(chn);
				id = iio_channel_get_id(chn);
				if (!name)
					name = id;

				gtk_list_store_set(channel_list_store, &iter2,
						0, name,	/* channel name */
						1, 0,		/* On/Off */
						2, devid,	/* device ID */
						3, id,		/* channel ID */
						4, scale,	/* scale */
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

void channel_toggle(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer data)
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
		bool input = false;

		for (j = 0; !input && j < nch; j++) {
			struct iio_channel *chn =
				iio_device_get_channel(dev, j);
			input = !iio_channel_is_output(chn) &&
				!iio_channel_is_scan_element(chn);
		}

		if (!input)
			continue;

		id = iio_device_get_name(dev);
		if (!id)
			id = iio_device_get_id(dev);

		gtk_list_store_append(device_list_store, &iter);
		gtk_list_store_set(device_list_store, &iter, 0, id,  1, 0, -1);
	}
}

static void dmm_update_thread(void)
{

	GtkTreeIter tree_iter;
	char *name, *device, *channel, *scale, tmp[128];
	gboolean loop, enabled;
	double value;

	GtkTextBuffer *buf;
	GtkTextIter text_iter;

	gdk_threads_enter();
	while (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(dmm_button))) {
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
						4, &scale,
						-1);
				if (enabled) {
					struct iio_device *dev =
						get_device(device);
					struct iio_channel *chn =
						get_channel(dev, channel);

					if (has_attribute(chn, "raw"))
						value = read_double_attr(chn, "raw");
					else if (has_attribute(chn, "processed"))
						value = read_double_attr(chn, "processed");
					else
						continue;

					if (has_attribute(chn, "offset"))
						value += read_double_attr(chn,
								"offset");
					if (has_attribute(chn, "scale"))
						value *= read_double_attr(chn,
								"scale");
					value /= 1000.0;

					if (!strncmp(channel, "voltage", 7))
						sprintf(tmp, "%s = %f Volts\n", name, value);
					else if (!strncmp(channel, "temp", 4))
						sprintf(tmp, "%s = %f Celsius\n", name, value);
					else
						sprintf(tmp, "%s = %f\n", name, value);

					gtk_text_buffer_insert(buf, &text_iter, tmp, -1);
				}
				loop = gtk_tree_model_iter_next(GTK_TREE_MODEL(channel_list_store), &tree_iter);
			}

			gtk_text_view_set_buffer(GTK_TEXT_VIEW(dmm_results), buf);
			g_object_unref(buf);
		}
		gdk_threads_leave();
		usleep(500000);
		gdk_threads_enter();
	}

	gdk_threads_leave();
	g_thread_exit(NULL);
}

static void dmm_button_clicked(GtkToggleToolButton *btn, gpointer data)
{
	static GThread *thr = NULL;

	if (gtk_toggle_tool_button_get_active(btn)) {
		thr = g_thread_new("Update_DMM", (void *)dmm_update_thread, NULL);
	} else {
		if (thr) {
			g_thread_unref(thr);
			thr = NULL;
		}
	}
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
static int dmm_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *dmm_panel;

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (!gtk_builder_add_from_file(builder, "dmm.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "dmm.glade", NULL);

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

	/* Show the panel */
	this_page = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), dmm_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), dmm_panel, "DMM");

	init_device_list();

	/* we are looking for almost random numbers, so this will work */
	srand((unsigned int)time(NULL));

	return 0;
}

#define DEVICE_LIST "device_list"
#define CHANNEL_LIST "channel_list"
#define RUNNING "running"

static char *dmm_handle(struct osc_plugin *plugin, const char *attrib,
		const char *value)
{
	GtkTreeIter iter;
	char *device, *channel, tmp[256];
	char *buf = NULL;
	gboolean loop;
	unsigned int enabled;

	if (MATCH_ATTRIB(DEVICE_LIST)) {
		loop = gtk_tree_model_get_iter_first(GTK_TREE_MODEL (device_list_store), &iter);
		while (loop) {
			gtk_tree_model_get(GTK_TREE_MODEL(device_list_store),
					&iter, 0, &device, 1, &enabled, -1);
			if (value) {
				/* load/restore */
				if (strstr(value, device)) {
					enabled = value[strlen(value) - 1] == '1';
					gtk_list_store_set(GTK_LIST_STORE(device_list_store),
						&iter, 1, enabled, -1);
				}
			} else {
				/* save */
				if (buf) {
					buf = realloc(buf, strlen(DEVICE_LIST) + strlen(buf) + strlen(device) + 8);
					sprintf(&buf[strlen(buf)], "\n%s = %s %i", DEVICE_LIST, device, enabled);
				} else {
					buf = malloc(strlen(device) + 5);
					sprintf(buf, "%s %i", device, enabled);
				}
			}
			g_free(device);
			loop = gtk_tree_model_iter_next(GTK_TREE_MODEL(device_list_store), &iter);
		}
		if (value)
			build_channel_list();


		return buf;

	} else if (MATCH_ATTRIB(CHANNEL_LIST)) {
		loop = gtk_tree_model_get_iter_first(GTK_TREE_MODEL (channel_list_store), &iter);
		while (loop) {
			gtk_tree_model_get(GTK_TREE_MODEL(channel_list_store),
					&iter, 1, &enabled, 2, &device, 3, &channel, -1);
			if (value) {
				/* load/restore */
				sprintf(tmp, "%s:%s", device, channel);
				if (strstr(value, tmp)) {
					enabled = 1;
					gtk_list_store_set(GTK_LIST_STORE(channel_list_store),
						&iter, 1, enabled, -1);
				}
			} else {
				/* save */
				if (enabled) {
					if (buf) {
						buf = realloc(buf, strlen(buf) +
								strlen(CHANNEL_LIST) +
								strlen(device) +
								strlen(channel) + 8);
						sprintf(&buf[strlen(buf)], "\n%s = %s:%s",
								CHANNEL_LIST, device, channel);
					} else {
						buf = malloc(strlen(device) + strlen(channel) + 2);
						sprintf(buf, "%s:%s", device, channel);
					}
				}
			}
			g_free(channel);
			g_free(device);
			loop = gtk_tree_model_iter_next(GTK_TREE_MODEL(channel_list_store), &iter);
		}
		return buf;
	} else if (MATCH_ATTRIB(RUNNING)) {
		if (value) {
			/* load/restore */
			if (!strcmp(value, "Yes"))
				gtk_toggle_tool_button_set_active(
						GTK_TOGGLE_TOOL_BUTTON(dmm_button), TRUE);
			else
				gtk_toggle_tool_button_set_active(
						GTK_TOGGLE_TOOL_BUTTON(dmm_button), FALSE);
		} else {
			/* save */
			if(gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(dmm_button)))
				sprintf(tmp, "Yes");
			else
				sprintf(tmp, "No");

			buf = strdup(tmp);
			return buf;
		}
	} else {
		printf("Unhandled tokens in ini file,\n"
				"\tSection %s\n\tAtttribute : %s\n\tValue: %s\n",
				"DMM", attrib, value);
		if (value)
			return "FAIL";
	}

	return NULL;
}

static struct iio_context *dmm_iio_context(void)
{
	return ctx;
}

static const char *dmm_sr_attribs[] = {
	DEVICE_LIST,
	CHANNEL_LIST,
	RUNNING,
	NULL,
};

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void context_destroy(void)
{
	iio_context_destroy(ctx);
}

static bool dmm_identify(void)
{
	unsigned int i, num;
	bool ret = false;

	ctx = osc_create_context();
	if (!ctx)
		return false;

	num = iio_context_get_devices_count(ctx);
	for (i = 0; !ret && i < num; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		unsigned int j, nch = iio_device_get_channels_count(dev);

		for (j = 0; !ret && j < nch; j++) {
			struct iio_channel *chn =
				iio_device_get_channel(dev, j);
			if (!iio_channel_is_output(chn))
				ret = true;
		}
	}

	if (!ret) {
		iio_context_destroy(ctx);
		ctx = NULL;
	}

	return ret;
}

struct osc_plugin plugin = {
	.name = "DMM",
	.identify = dmm_identify,
	.init = dmm_init,
	.save_restore_attribs = dmm_sr_attribs,
	.get_iio_context = dmm_iio_context,
	.handle_item = dmm_handle,
	.update_active_page = update_active_page,
	.destroy = context_destroy,
};
