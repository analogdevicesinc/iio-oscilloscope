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

#include "../osc.h"
#include "../iio_utils.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"

extern char dev_dir_name[512];

static GtkWidget *dmm_results;
static GtkWidget *select_all_channels;
static GtkWidget *dmm_button;
static GtkWidget *device_list_widget;
static GtkListStore *device_list_store;
static GtkListStore *channel_list_store;
static gint this_page;
static GtkNotebook *nbook;

static void device_toggled(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer data)
{
	GtkTreePath* path = gtk_tree_path_new_from_string(pathStr);
	GtkTreeIter iter, iter2;
	unsigned int enabled;
	char *device, *elements, *start, *strt, *end, *next, *scale;
	char buf[128], buf2[128], pretty[128];
	gboolean loop, all = FALSE;

	gtk_tree_model_get_iter(GTK_TREE_MODEL (data), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL (data), &iter, 0, &device, 1, &enabled, -1);
	enabled = !enabled;
	gtk_list_store_set(GTK_LIST_STORE (data), &iter, 1, enabled, -1);

	loop = gtk_tree_model_get_iter_first(GTK_TREE_MODEL (data), &iter);
	gtk_list_store_clear(channel_list_store);

	while (loop) {
		gtk_tree_model_get(GTK_TREE_MODEL (data), &iter, 0, &device, 1, &enabled, -1);
		if (enabled && find_scan_elements(device, &elements, ACCESS_NORM)) {
			all = true;
			scan_elements_sort(&elements);
			scan_elements_insert(&elements, SCALE_TOKEN, "_raw");
			while(isspace(elements[strlen(elements) - 1]))
				elements[strlen(elements) - 1] = 0;
			start = elements;

			while (start[0] != 0) {
				/* find pointers, and correct for end of line */
				end = strchr(start, ' ');
				if (!end)
					end = start + strlen(start);

				scale = strstr(end + 1, SCALE_TOKEN);
				next = strchr(end + 1, ' ' );

				if (!next)
					next = end + 1 + strlen(end + 1);

				sprintf(buf, "%.*s", (int)(end - start), start);

				strt = start;
				if(scale >= next) {
					scale = NULL;
					buf2[0] = 0;
					start = end + 1;
				} else {
					sprintf(buf2, "%.*s", (int)(next - end) - 1, end + 1);
					start = next + 1;
				}


				/* if it doesn't start with "in_" or includes the scale,
				 * skip it */
				if (strncmp("in_", buf, 3) || (strstr(buf, SCALE_TOKEN)))
					continue;

				if (strstr(buf, "_raw"))
					sprintf(pretty, "%s: %.*s", device, (int)(end - strt) - 7, &strt[3]);
				else
					sprintf(pretty, "%s: %.*s", device, (int)(end - strt) - 4, &strt[3]);

				gtk_list_store_append(channel_list_store, &iter2);
				gtk_list_store_set(channel_list_store, &iter2,
						0, pretty,	/* pretty channel name */
						1, 0,		/* On/Off */
						2, device, 	/* the actual device */
						3, buf,		/* the actual channel name */
						4, buf2,	/* scale */
						 	-1);
			}
			free(elements);
		}
		loop = gtk_tree_model_iter_next(GTK_TREE_MODEL (data), &iter);
	}

	if(all)
		gtk_widget_show(select_all_channels);
	else
		gtk_widget_hide(select_all_channels);
}

void channel_toggle(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer data)
{
	GtkTreePath* path = gtk_tree_path_new_from_string(pathStr);
	GtkTreeIter iter;
	char *device, *name;
	unsigned int enabled;

	gtk_tree_model_get_iter(GTK_TREE_MODEL (data), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL (data), &iter, 0, &device, 1, &enabled, 2, &name, -1);
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
	char *devices = NULL, *device, *elements;
	unsigned int num;
	GtkTreeIter iter;

	gtk_list_store_clear(device_list_store);

	num = find_iio_names(&devices, "iio:device");
	if (devices != NULL) {
		device = devices;
		for (; num > 0; num--) {
			if (!is_input_device(device)) {
				if (find_scan_elements(device, &elements, ACCESS_NORM)) {
					if (strstr(elements, "in_")) {
						gtk_list_store_append(device_list_store, &iter);
						gtk_list_store_set(device_list_store, &iter, 0, device,  1, 0, -1);
					}
				}
			}
			device += strlen(device) + 1;
		}
		free(devices);
	}

}

static void dmm_update_thread(void)
{

        GtkTreeIter tree_iter;
        char *name, *device, *channel, *scale, tmp[128];
        gboolean loop, enabled;
        double value, sca;

        GtkTextBuffer *buf;
        GtkTextIter text_iter;

	gdk_threads_enter();
	while (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(dmm_button))) {
		/* start at the top every time */
		buf = gtk_text_buffer_new(NULL);
		gtk_text_buffer_get_iter_at_offset(buf, &text_iter, 0);

		if (this_page == gtk_notebook_get_current_page(nbook)) {
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
					set_dev_paths(device);
					read_devattr_double(channel, &value);
					if (scale)
						read_devattr_double(scale, &sca);
					else
						sca = 1.0;

					if(strstr(name, "voltage"))
						sprintf(tmp, "%s = %f Volts\n", name, value * sca / 1000.0);
					else if (strstr(name, "temp"))
						sprintf(tmp, "%s = %f Celsius\n", name, value * sca / 1000.0);
					else
						sprintf(tmp, "%s = %f\n", name, value * sca / 1000.0);

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

}

static void dmm_button_clicked(GtkToggleToolButton *btn, gpointer data)
{

	if (gtk_toggle_tool_button_get_active(btn)) {
		g_thread_new("Update_DMM", (void *)dmm_update_thread, NULL);
	} else {

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

	return 0;
}

static bool dmm_identify(void)
{
	return true;
}

const struct osc_plugin plugin = {
	.name = "DMM",
	.identify = dmm_identify,
	.init = dmm_init,
};
