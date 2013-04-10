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

GtkWidget *register_address, *register_value;
GtkWidget *register_address_hex, *register_value_hex;
GtkWidget *register_write, *register_read;
GtkWidget *device_list;

static void reg_read_clicked(GtkButton *btn, gpointer data)
{
	int i;
	char buf[10];

	i = read_reg((int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(register_address)));
	if (i >= 0) {
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(register_value), i);
		snprintf(buf, sizeof(buf), "0x%03X", i);
		gtk_label_set_text(GTK_LABEL(register_value_hex), buf);
	} else {
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(register_value), 0);
		snprintf(buf, sizeof(buf), "<error>");
		gtk_label_set_text(GTK_LABEL(register_value_hex), buf);
	}

	gtk_widget_show(register_write);
}

static void reg_write_clicked(GtkButton *btn, gpointer data)
{
	write_reg((unsigned)gtk_spin_button_get_value(GTK_SPIN_BUTTON(register_address)),
			 (unsigned)gtk_spin_button_get_value(GTK_SPIN_BUTTON(register_value)));

}

static void debug_reg_add_change_value_cb(GtkButton *btn, gpointer data)
{
	char buf[10];
	int add = gtk_spin_button_get_value(GTK_SPIN_BUTTON(register_address));

	snprintf(buf, sizeof(buf), "0x%03X", add);
	gtk_label_set_text(GTK_LABEL(register_address_hex), buf);

	snprintf(buf, sizeof(buf), "<unknown>");
	gtk_label_set_text(GTK_LABEL(register_value_hex), buf);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(register_value), (gdouble)0);

	gtk_widget_hide(register_write);
}

static void debug_reg_val_change_value_cb(GtkButton *btn, gpointer data)
{
	char buf[10];
	int val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(register_value));

	snprintf(buf, sizeof(buf), "0x%03X", val);
	gtk_label_set_text(GTK_LABEL(register_value_hex), buf);
}

static void debug_device_list_cb(GtkButton *btn, gpointer data)
{
	char buf[10];

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(register_value), (gdouble)0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(register_address), (gdouble)0);
	gtk_widget_hide(register_write);

	if (g_strcmp0("None\0",  
			gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(device_list)))) {
		gtk_widget_show(register_read);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(register_value), (gdouble)0);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(register_address), (gdouble)0);
		snprintf(buf, sizeof(buf), "<unknown>");
		gtk_label_set_text(GTK_LABEL(register_value_hex), buf);
		set_debugfs_paths(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(device_list)));
	} else {
		gtk_widget_hide(register_read);
	}

}

static int debug_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *debug_panel;
	char *devices=NULL, *device;
	int num;

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "debug.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "debug.glade", NULL);

	debug_panel = GTK_WIDGET(gtk_builder_get_object(builder, "reg_debug_panel"));
	register_address = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_address"));
	register_address_hex = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_address_hex"));
	register_value   = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_value"));
	register_value_hex = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_value_hex"));
	register_write = GTK_WIDGET(gtk_builder_get_object(builder, "debug_write_reg"));
	register_read = GTK_WIDGET(gtk_builder_get_object(builder, "debug_read_reg"));
	device_list =  GTK_WIDGET(gtk_builder_get_object(builder, "debug_device_list"));

	/* Gets rid of the dummy values */
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(device_list), 0);
	/* Put in the correct values */
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(device_list),
			(const gchar *)"None");
	num = find_iio_names(&devices, NULL, NULL);
	device=devices;
	for (; num > 0; num--) {
		/* Make sure we can access things */
		if (!set_debugfs_paths(devices)) {
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(device_list),
					(const gchar *)devices);
		}
		devices += strlen(devices) + 1;
	}
	free(device);
	/* pick the default, which is "none" */
	gtk_combo_box_set_active(GTK_COMBO_BOX(device_list), 0);

	g_builder_connect_signal(builder, "debug_write_reg", "clicked",
			 G_CALLBACK(reg_write_clicked), NULL);
	g_builder_connect_signal(builder, "debug_read_reg", "clicked",
			G_CALLBACK(reg_read_clicked), NULL);
	g_builder_connect_signal(builder, "debug_reg_value", "value_changed",
			G_CALLBACK(debug_reg_val_change_value_cb), NULL);
	g_builder_connect_signal(builder, "debug_reg_address", "value_changed",
			G_CALLBACK(debug_reg_add_change_value_cb), NULL);
	g_builder_connect_signal(builder, "debug_device_list", "changed",
			G_CALLBACK(debug_device_list_cb), NULL);

	gtk_widget_hide(register_read);
	gtk_widget_hide(register_write);

	/* Show the panel */
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), debug_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), debug_panel, "Debug");

	return 0;
}

static bool debug_identify(void)
{
	int num, i = 0;
	char *devices=NULL, *device;

	num = find_iio_names(&devices, NULL, NULL);
	device=devices;
	for (; num > 0; num--) {
		/* Make sure we can access things */
		if (!set_debugfs_paths(devices))
			i++;
		devices += strlen(devices) + 1;
	}
	free(device);
	if (i)
		return true;

	return false;
}

const struct osc_plugin plugin = {
	.name = "Debug",
	.identify = debug_identify,
	.init = debug_init,
};
