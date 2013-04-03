/**
 * Copyright 2012(c) Analog Devices, Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *
 **/

#ifndef __IIO_WIDGET_H__
#define __IIO_WIDGET_H__

struct iio_widget {
	const char *device_name;
	const char *attr_name;
	GtkWidget *widget;
	void *priv;

	void (*save)(struct iio_widget *);
	void (*update)(struct iio_widget *);
};

void g_builder_connect_signal(GtkBuilder *builder, const gchar *name,
	const gchar *signal, GCallback callback, gpointer data);

void iio_update_widgets(struct iio_widget *widgets, unsigned int num_widgets);
void iio_save_widgets(struct iio_widget *widgets, unsigned int num_widgets);

void iio_spin_button_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale);
void iio_combo_box_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkBuilder *builder, const char *widget_name,
	int (*compare)(const char *a, const char *b));
void iio_toggle_button_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkBuilder *builder, const char *widget_name);
void iio_spin_button_int_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale);

#endif
