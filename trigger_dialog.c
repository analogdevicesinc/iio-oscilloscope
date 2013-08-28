/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>

#include "fru.h"
#include "osc.h"
#include "iio_utils.h"
#include "iio_widget.h"

static GtkWidget *trigger_dialog;
static GtkWidget *trigger_list_widget;
static GtkWidget *device_list_widget;
static GtkListStore *trigger_list_store;
static GtkListStore *device_list_store;
static GtkWidget *frequency_spin_button;
static GtkWidget *frequency_spin_button_label;
static GtkWidget *trigger_menu;

static char *crt_device;

static void trigger_change_trigger(void)
{
	const char *current_trigger;
	bool has_frequency;
	GtkTreeIter iter;

	if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(trigger_list_widget), &iter)) {
		gtk_tree_model_get(GTK_TREE_MODEL(trigger_list_store), &iter, 0,
			&current_trigger, -1);

		if (strcmp(current_trigger, "None") == 0)
			has_frequency = false;
		else
			has_frequency = iio_devattr_exists(current_trigger, "frequency");
	} else {
		has_frequency = false;
	}

	gtk_widget_set_sensitive(frequency_spin_button, has_frequency);
	gtk_widget_set_sensitive(frequency_spin_button_label, has_frequency);

	if (has_frequency) {
		int freq;
		set_dev_paths(current_trigger);
		read_devattr_int("frequency", &freq);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(frequency_spin_button), freq);
	} else {
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(frequency_spin_button), 0);
	}
}

static void load_device_list(void)
{
	char *devices = NULL, *device;
	unsigned int num;
	GtkTreeIter iter;
	
	gtk_list_store_clear(device_list_store);
	
	gtk_list_store_append(device_list_store, &iter);
	gtk_list_store_set(device_list_store, &iter, 0, "None", -1);
	gtk_combo_box_set_active_iter(GTK_COMBO_BOX(device_list_widget), &iter);
	
	num = find_iio_names(&devices, "iio:device");
	if (devices != NULL) {
		device = devices;
		for (; num > 0; num--) {
			gtk_list_store_append(device_list_store, &iter);
			gtk_list_store_set(device_list_store, &iter, 0, device, -1);
			device += strlen(device) + 1;
		}
	}
	free(devices);
}

static void trigger_load_settings(void)
{
	char *devices = NULL, *device;
	char *current_trigger;
	GtkTreeIter iter;
	unsigned int num;
	int ret;

	gtk_list_store_clear(trigger_list_store);
	
	if (!crt_device ||
		!iio_devattr_exists(crt_device, "trigger/current_trigger"))
		return;

	set_dev_paths(crt_device);
	ret = read_devattr("trigger/current_trigger", &current_trigger);
	if (ret < 0)
		return;

	num = find_iio_names(&devices, "trigger");
	if (devices == NULL)
		return;

	gtk_list_store_append(trigger_list_store, &iter);
	gtk_list_store_set(trigger_list_store, &iter, 0, "None", -1);
	gtk_combo_box_set_active_iter(GTK_COMBO_BOX(trigger_list_widget), &iter);

	device = devices;
	for (; num > 0; num--) {
		gtk_list_store_append(trigger_list_store, &iter);
		gtk_list_store_set(trigger_list_store, &iter, 0, device, -1);

		if (strcmp(current_trigger, device) == 0)
			gtk_combo_box_set_active_iter(GTK_COMBO_BOX(trigger_list_widget), &iter);

		device += strlen(device) + 1;
	}
	free(devices);
	free(current_trigger);

	trigger_change_trigger();
}

static void trigger_save_settings()
{
	const char *current_trigger;
	GtkTreeIter iter;
	int freq;

	if (!crt_device ||
		!iio_devattr_exists(crt_device, "trigger/current_trigger"))
		return;

	if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(trigger_list_widget), &iter)) {
		gtk_tree_model_get(GTK_TREE_MODEL(trigger_list_store), &iter, 0,
			&current_trigger, -1);

		if (strcmp(current_trigger, "None") == 0)
			current_trigger = "";
	} else {
		current_trigger = "";
	}

	set_dev_paths(crt_device);
	write_devattr("trigger/current_trigger", current_trigger);

	if (*current_trigger && iio_devattr_exists(current_trigger, "frequency")) {
		set_dev_paths(current_trigger);
		freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON(frequency_spin_button));
		write_devattr_int("frequency", freq);
	}
	rx_update_labels();
}

static void device_change_trigger(void)
{
	char *crt_dev;
	GtkTreeIter iter;
	
	gtk_combo_box_get_active_iter(GTK_COMBO_BOX(device_list_widget), &iter);
	gtk_tree_model_get(GTK_TREE_MODEL(device_list_store), &iter, 0, &crt_dev, -1);
	if (strcmp(crt_dev, "None") != 0){
		crt_device = realloc(crt_device, strlen(crt_dev) + 1);
		snprintf(crt_device, strlen(crt_dev) + 1, "%s", crt_dev);
		g_free(crt_dev);
	} else {
		g_free(crt_dev);
		free(crt_device);
		crt_device = NULL;
	}
	
	trigger_load_settings();
}

static void trigger_dialog_show(void)
{
	int ret;

	load_device_list();
	trigger_load_settings();
	ret = gtk_dialog_run(GTK_DIALOG(trigger_dialog));
	switch(ret) {
		case GTK_RESPONSE_CANCEL:
			break;
		case GTK_RESPONSE_OK:
			trigger_save_settings();
			break;
		default:
			break;
	}

	gtk_widget_hide(trigger_dialog);
}

void trigger_dialog_init(GtkBuilder *builder)
{
	trigger_dialog = GTK_WIDGET(gtk_builder_get_object(builder, "trigger_dialog"));
	trigger_list_widget = GTK_WIDGET(gtk_builder_get_object(builder, "trigger_list_combobox"));
	device_list_widget = GTK_WIDGET(gtk_builder_get_object(builder, "device_list_combobox"));
	trigger_list_store = GTK_LIST_STORE(gtk_builder_get_object(builder, "trigger_list"));
	device_list_store = GTK_LIST_STORE(gtk_builder_get_object(builder, "device_list"));
	frequency_spin_button = GTK_WIDGET(gtk_builder_get_object(builder,
		"trigger_frequency"));
	frequency_spin_button_label = GTK_WIDGET(gtk_builder_get_object(builder,
		"trigger_frequency_label"));
	trigger_menu = GTK_WIDGET(gtk_builder_get_object(builder,
		"trigger_menu"));

	g_signal_connect(device_list_widget, "changed",
		G_CALLBACK(device_change_trigger), NULL);
	g_signal_connect(trigger_list_widget, "changed",
		G_CALLBACK(trigger_change_trigger), NULL);

	g_signal_connect(trigger_menu, "activate", G_CALLBACK(trigger_dialog_show),
		NULL);
}

void trigger_update_current_device(void)
{
	bool has_trigger;

	if (crt_device)
		has_trigger = iio_devattr_exists(crt_device, "trigger/current_trigger");
	else
		has_trigger = false;
	gtk_widget_set_sensitive(trigger_menu, has_trigger);
}
