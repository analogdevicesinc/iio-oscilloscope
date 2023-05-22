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
#include "iio_widget.h"

static void trigger_change_trigger(GtkComboBox *box, GtkBuilder *builder)
{
	struct iio_context *ctx;
	struct iio_device *trigger;
	long long trigger_freq;
	GtkComboBoxText *trigger_combobox;
	GtkSpinButton *spinbtn_freq;
	GtkLabel *label_freq;
	gchar *current_trigger;
	gboolean has_frequency = false;
	const char *attr;

	trigger_combobox = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder,
			"comboboxtext_triggers"));
	spinbtn_freq = GTK_SPIN_BUTTON(gtk_builder_get_object(builder,
			"trigger_frequency"));
	label_freq = GTK_LABEL(gtk_builder_get_object(builder,
			"trigger_frequency_label"));
	current_trigger = gtk_combo_box_text_get_active_text(trigger_combobox);

	if (current_trigger && strcmp(current_trigger, "None")) {
		ctx = get_context_from_osc();
		if (!ctx)
			goto abort;
		trigger = iio_context_find_device(ctx, current_trigger);
		if (!trigger)
			goto abort;
		attr = iio_device_find_attr(trigger, "sampling_frequency");

		/* "frequency" was used for the sampling frequency by some non ABI
		 * compliant drivers. Drop this at some point */
		if (!attr)
			attr = iio_device_find_attr(trigger, "frequency");
		if (!attr)
			goto abort;

		if (iio_device_attr_read_longlong(trigger, attr, &trigger_freq))
			goto abort;

		has_frequency = true;
	}

abort:
	if (!has_frequency)
		trigger_freq = 0;

	g_free(current_trigger);
	gtk_widget_set_sensitive(GTK_WIDGET(spinbtn_freq), has_frequency);
	gtk_widget_set_sensitive(GTK_WIDGET(label_freq), has_frequency);
	gtk_spin_button_set_value(spinbtn_freq, trigger_freq);
}

static void trigger_load_settings(GtkBuilder *builder, const char *device)
{
	struct iio_context *ctx;
	struct iio_device *dev;
	const struct iio_device *trigger;
	unsigned nb_devices, i;
	GtkComboBoxText *trigger_combobox;
	GtkListStore *liststore;
	int pos, t_cnt, ret;

	ctx = get_context_from_osc();
	if (!ctx)
		return;

	trigger_combobox = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder,
				"comboboxtext_triggers"));
	liststore = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(trigger_combobox)));
	gtk_list_store_clear(liststore);
	gtk_combo_box_text_append_text(trigger_combobox, "None");

	dev = iio_context_find_device(ctx, device);
	if (!dev)
		return;

	ret = iio_device_get_trigger(dev, &trigger);
	if (ret < 0)
		trigger = NULL;

	pos = 0;

	nb_devices = iio_context_get_devices_count(ctx);
	for (i = 0, t_cnt = 0; i < nb_devices; i++) {
		dev = iio_context_get_device(ctx, i);
		if (dev && iio_device_is_trigger(dev)) {
			gtk_combo_box_text_append_text(trigger_combobox,
					iio_device_get_name(dev));
			t_cnt++;
			if (trigger && !strcmp(iio_device_get_name(dev),
					iio_device_get_name(trigger)))
				pos = t_cnt;
		}
	}

	gtk_combo_box_set_active(GTK_COMBO_BOX(trigger_combobox), pos);
}

static void trigger_save_settings(GtkBuilder *builder, const char *device)
{
	struct iio_context *ctx;
	struct iio_device *dev;
	struct iio_device *trigger;
	GtkComboBoxText *trigger_combobox;
	GtkSpinButton *spinbtn_freq;
	gchar *current_trigger;
	const char *dev_name;
	const char *attr;

	trigger_combobox = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(builder,
			"comboboxtext_triggers"));
	spinbtn_freq = GTK_SPIN_BUTTON(gtk_builder_get_object(builder,
			"trigger_frequency"));

	current_trigger = gtk_combo_box_text_get_active_text(trigger_combobox);
	if (current_trigger) {
		ctx = get_context_from_osc();
		if (!ctx)
			goto abort;
		dev = iio_context_find_device(ctx, device);
		if (!dev)
			goto abort;
		if (strcmp(current_trigger, "None")) {
			trigger = iio_context_find_device(ctx, current_trigger);
			if (!trigger)
				goto abort;

			attr = iio_device_find_attr(trigger, "sampling_frequency");
			if (!attr)
				attr = iio_device_find_attr(trigger, "frequency");

			if (attr) {
				iio_device_attr_write_longlong(trigger, attr,
					(long long)gtk_spin_button_get_value(spinbtn_freq));
			}

			iio_device_set_trigger(dev, trigger);
		} else {
			iio_device_set_trigger(dev, NULL);
		}
		dev_name = iio_device_get_name(dev) ?:
				iio_device_get_id(dev);
		rx_update_device_sampling_freq(dev_name,
			USE_INTERN_SAMPLING_FREQ);
	}

abort:
	g_free(current_trigger);
	return;
}

void trigger_settings_for_device(GtkBuilder *builder, const char *device)
{
	GtkDialog *dialog;
	int ret;

	dialog = GTK_DIALOG(gtk_builder_get_object(builder, "trigger_dialog"));
	trigger_load_settings(builder, device);
	ret = gtk_dialog_run(dialog);
	switch(ret) {
		case GTK_RESPONSE_CANCEL:
			break;
		case GTK_RESPONSE_OK:
			trigger_save_settings(builder, device);
			break;
		default:
			break;
	}

	gtk_widget_hide(GTK_WIDGET(dialog));
}

void trigger_dialog_init(GtkBuilder *builder)
{
	GtkWidget *trigger_combobox;

	trigger_combobox = GTK_WIDGET(gtk_builder_get_object(builder,
				"comboboxtext_triggers"));

	g_signal_connect(trigger_combobox, "changed",
		G_CALLBACK(trigger_change_trigger), builder);
}
