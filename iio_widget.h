/**
 * Copyright 2012-2013(c) Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __IIO_WIDGET_H__
#define __IIO_WIDGET_H__

#include <gtk/gtk.h>
#include <iio.h>

struct iio_widget {
	struct iio_device *dev;
	struct iio_channel *chn;
	const char *attr_name;
	const char *attr_name_avail;
	GtkWidget *widget;
	void *priv;
	void *priv_progress;
	void *priv_convert_function;

	void (*save)(struct iio_widget *);
	void (*update)(struct iio_widget *);
	void (*update_value)(struct iio_widget *, const char *, size_t);
	void *sig_handler_data;
};

void g_builder_connect_signal(GtkBuilder *builder, const gchar *name,
	const gchar *signal, GCallback callback, gpointer data);
void g_builder_connect_signal_data(GtkBuilder *builder, const gchar *name,
	const gchar *signal, GCallback callback, gpointer data,
	GClosureNotify destroy_data, GConnectFlags connect_flags);
void g_builder_bind_property(GtkBuilder *builder,
	const gchar *source_name, const gchar *source_property,
	const gchar *target_name, const gchar *target_property,
	GBindingFlags flags);

void iio_make_widgets_update_signal_based(struct iio_widget *widgets, unsigned int num_widgets,
					  GCallback handler);
void iio_make_widget_update_signal_based(struct iio_widget *widget, GCallback handler,
					 gpointer data);
void iio_update_widgets(struct iio_widget *widgets, unsigned int num_widgets);
void iio_update_widgets_block_signals_by_data(struct iio_widget *widgets, unsigned int num_widgets);
void iio_widget_update(struct iio_widget *widget);
void iio_widget_update_value(struct iio_widget *widget, const char *ensm, size_t len);
void iio_update_widgets_of_device(struct iio_widget *widgets,
		unsigned int num_widgets, struct iio_device *dev);
void iio_widget_update_block_signals_by_data(struct iio_widget *widget);
void iio_widget_save(struct iio_widget *widget);
void iio_widget_save_cb(GtkWidget *widget, struct iio_widget *iio_widget);
void iio_save_widgets(struct iio_widget *widgets, unsigned int num_widgets);
void iio_widget_save_block_signals_by_data(struct iio_widget *widget);
void iio_widget_save_block_signals_by_data_cb(GtkWidget *widget, struct iio_widget *iio_widget);

void iio_spin_button_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name,
	GtkWidget *spin_button, const gdouble *scale);
void iio_spin_button_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale);

void iio_combo_box_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name, const char *attr_name_avail,
	GtkWidget *combo_box, int (*compare)(const char *a, const char *b));
void iio_combo_box_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	const char *attr_name_avail,
	GtkBuilder *builder, const char *widget_name,
	int (*compare)(const char *a, const char *b));

void iio_combo_box_init_no_avail_flush(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name, const char *attr_name_avail,
	GtkWidget *combo_box, int (*compare)(const char *a, const char *b));
void iio_combo_box_init_no_avail_flush_from_builder(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name, const char *attr_name_avail,
	GtkBuilder *builder, const char *widget_name, int (*compare)(const char *a, const char *b));

void iio_toggle_button_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const bool invert);

void iio_button_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkBuilder *builder, const char *widget_name);

void iio_spin_button_int_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale);
void iio_spin_button_int_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name,
	GtkWidget *spin_button, const gdouble *scale);

void iio_spin_button_s64_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale);
void iio_spin_button_s64_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name,
	GtkWidget *spin_button, const gdouble *scale);

void iio_spin_button_add_progress(struct iio_widget *iio_w);
void iio_spin_button_progress_activate(struct iio_widget *iio_w);
void iio_spin_button_set_on_complete_function(struct iio_widget *iio_w,
		void(*on_complete)(void *), void *data);
void iio_spin_button_skip_save_on_complete(struct iio_widget *iio_w,
		gboolean skip);
void iio_spin_button_progress_deactivate(struct iio_widget *iio_w);
void iio_spin_button_remove_progress(struct iio_widget *iio_w);

void iio_spin_button_set_convert_function(struct iio_widget *iio_w,
		double (*convert)(double, bool inverse));

void iio_spin_button_save(struct iio_widget *widget);
#endif
