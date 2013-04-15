/**
 * Copyright 2012-2013(c) Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __IIO_WIDGET_H__
#define __IIO_WIDGET_H__

struct iio_widget {
	const char *device_name;
	const char *attr_name;
	const char *attr_name_avail;
	GtkWidget *widget;
	void *priv;

	void (*save)(struct iio_widget *);
	void (*update)(struct iio_widget *);
};

void g_builder_connect_signal(GtkBuilder *builder, const gchar *name,
	const gchar *signal, GCallback callback, gpointer data);
void g_builder_bind_property(GtkBuilder *builder,
	const gchar *source_name, const gchar *source_property,
	const gchar *target_name, const gchar *target_property,
	GBindingFlags flags);

void iio_update_widgets(struct iio_widget *widgets, unsigned int num_widgets);
void iio_save_widgets(struct iio_widget *widgets, unsigned int num_widgets);

void iio_spin_button_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale);
void iio_combo_box_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	const char *attr_name_avail,
	GtkBuilder *builder, const char *widget_name,
	int (*compare)(const char *a, const char *b));
void iio_toggle_button_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkBuilder *builder, const char *widget_name);
void iio_spin_button_int_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale);

#endif
