/**
 * Copyright 2012-2013(c) Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#include <gtk/gtk.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "iio_widget.h"
#include "iio_utils.h"

void g_builder_connect_signal(GtkBuilder *builder, const gchar *name,
	const gchar *signal, GCallback callback, gpointer data)
{
	GObject *tmp;
	tmp = gtk_builder_get_object(builder, name);
	g_signal_connect(tmp, signal, callback, data);
}

void g_builder_bind_property(GtkBuilder *builder,
	const gchar *source_name, const gchar *source_property,
	const gchar *target_name, const gchar *target_property,
	GBindingFlags flags)
{
	GObject *source_object, *target_object;

	source_object = gtk_builder_get_object(builder, source_name);
	if (!source_object) {
		fprintf(stderr, "Couldn't find object \"%s\"\n", source_name);
		return;
	}

	target_object = gtk_builder_get_object(builder, target_name);
	if (!target_object) {
		fprintf(stderr, "Couldn't find object \"%s\"\n", target_name);
		g_object_unref(source_object);	
		return;
	}

	g_object_bind_property(source_object, source_property, target_object,
			target_property, flags);
}


static void iio_widget_init(struct iio_widget *widget, const char *device_name,
	const char *attr_name, GtkWidget *gtk_widget, void *priv,
	void (*update)(struct iio_widget *), void (*save)(struct iio_widget *))
{
	if (!gtk_widget)
		printf("Missing widget for %s/%s\n", device_name, attr_name);

	widget->device_name = device_name;
	widget->attr_name = attr_name;
	widget->widget = gtk_widget;
	widget->update = update;
	widget->save = save;
	widget->priv = priv;
}

static void iio_spin_button_update(struct iio_widget *widget)
{
	gdouble freq;
	gdouble scale = widget->priv ? *(gdouble *)widget->priv : 1.0;

	read_devattr_double(widget->attr_name, &freq);
	freq /= scale;
	gtk_spin_button_set_value(GTK_SPIN_BUTTON (widget->widget), freq);
}

static void iio_spin_button_save(struct iio_widget *widget)
{
	gdouble freq;
	gdouble scale = widget->priv ? *(gdouble *)widget->priv : 1.0;

	freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON (widget->widget));
	freq *= scale;
	write_devattr_double(widget->attr_name, freq);
}

static void iio_spin_button_int_save(struct iio_widget *widget)
{
	gdouble freq;
	gdouble scale = widget->priv ? *(gdouble *)widget->priv : 1.0;

	freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON (widget->widget));
	freq *= scale;
	write_devattr_int(widget->attr_name, freq);
}

static void iio_spin_button_init(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkWidget *spin_button, const gdouble *scale)
{
	iio_widget_init(widget, device_name, attr_name, spin_button,
		(void *)scale, iio_spin_button_update, iio_spin_button_save);
}

static void iio_spin_button_int_init(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkWidget *spin_button, const gdouble *scale)
{
	iio_widget_init(widget, device_name, attr_name, spin_button,
		(void *)scale, iio_spin_button_update, iio_spin_button_int_save);
}


static void iio_toggle_button_save(struct iio_widget *widget)
{
	bool active;

	active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (widget->widget));

	write_devattr(widget->attr_name, active ? "1" : "0");
}

static void iio_toggle_button_update(struct iio_widget *widget)
{
	bool active = false;

	read_devattr_bool(widget->attr_name, &active);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (widget->widget), active);
}

static void iio_toggle_button_init(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkWidget *toggle_button)
{
	iio_widget_init(widget, device_name, attr_name, toggle_button, NULL,
		iio_toggle_button_update, iio_toggle_button_save);
}

static void iio_combo_box_save(struct iio_widget *widget)
{
	const char *text;

	text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget->widget));
	if (text == NULL)
		return;

	write_devattr(widget->attr_name, text);
}

static void iio_combo_box_update(struct iio_widget *widget)
{
	int (*compare)(const char *, const char *);
	GtkComboBox *combo_box;
	GtkTreeIter iter;
	GtkTreeModel *model;
	char *text, *item;
	gboolean has_iter;
	int ret;

	ret = read_devattr(widget->attr_name, &text);
	if (ret < 0)
		return;

	if (widget->priv)
		compare = widget->priv;
	else
		compare = strcmp;

	combo_box = GTK_COMBO_BOX(widget->widget);
	model = gtk_combo_box_get_model(combo_box);

	has_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (has_iter) {
		gtk_tree_model_get(model, &iter, 0, &item, -1);
		if (compare (text, item) == 0) {
			gtk_combo_box_set_active_iter(combo_box, &iter);
			g_free(item);
			break;
		}
		g_free(item);
		has_iter = gtk_tree_model_iter_next(model, &iter);
	}

	free(text);
}

static void iio_combo_box_init(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkWidget *combo_box, int (*compare)(const char *a, const char *b))
{
	iio_widget_init(widget, device_name, attr_name, combo_box,
		(void *)compare, iio_combo_box_update, iio_combo_box_save);
}

static void iio_widget_update(struct iio_widget *widget)
{
	set_dev_paths(widget->device_name);
	widget->update(widget);
}

static void iio_widget_save(struct iio_widget *widget)
{
	set_dev_paths(widget->device_name);
	widget->save(widget);
	widget->update(widget);
}

void iio_update_widgets(struct iio_widget *widgets, unsigned int num_widgets)
{
	unsigned int i;

	for (i = 0; i < num_widgets; i++)
		iio_widget_update(&widgets[i]);
}

void iio_save_widgets(struct iio_widget *widgets, unsigned int num_widgets)
{
	unsigned int i;

	for (i = 0; i < num_widgets; i++)
		iio_widget_save(&widgets[i]);
}

void iio_spin_button_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale)
{
	iio_spin_button_init(widget, device_name, attr_name,
		GTK_WIDGET(gtk_builder_get_object(builder, widget_name)),
		scale);
}

void iio_spin_button_int_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale)
{
	iio_spin_button_int_init(widget, device_name, attr_name,
		GTK_WIDGET(gtk_builder_get_object(builder, widget_name)),
		scale);
}

void iio_combo_box_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkBuilder *builder, const char *widget_name,
	int (*compare)(const char *a, const char *b))
{
	iio_combo_box_init(widget, device_name, attr_name,
		GTK_WIDGET(gtk_builder_get_object(builder, widget_name)),
		compare);
}

void iio_toggle_button_init_from_builder(struct iio_widget *widget,
	const char *device_name, const char *attr_name,
	GtkBuilder *builder, const char *widget_name)
{
	iio_toggle_button_init(widget, device_name, attr_name,
		GTK_WIDGET(gtk_builder_get_object(builder, widget_name)));
}
