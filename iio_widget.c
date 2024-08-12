/**
 * Copyright 2012-2013(c) Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#include <gtk/gtk.h>
#include <glib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <math.h>

#include "iio_widget.h"

struct update_widgets_params {
	struct iio_widget *widgets;
	unsigned int nb;
};

void g_builder_connect_signal(GtkBuilder *builder, const gchar *name,
	const gchar *signal, GCallback callback, gpointer data)
{
	GObject *tmp;
	tmp = gtk_builder_get_object(builder, name);
	if (tmp == NULL)
		fprintf(stderr, "Couldn't find object \"%s\".\n", name);
	else
		g_signal_connect(tmp, signal, callback, data);
}

void g_builder_connect_signal_data(GtkBuilder *builder, const gchar *name,
	const gchar *signal, GCallback callback, gpointer data,
	GClosureNotify destroy_data, GConnectFlags connect_flags)
{
	GObject *tmp;
	tmp = gtk_builder_get_object(builder, name);
	if (tmp == NULL)
		fprintf(stderr, "Couldn't find object \"%s\".\n", name);
	else
		g_signal_connect_data(tmp, signal, callback, data, destroy_data, connect_flags);
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


static void iio_widget_init(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	const char *attr_name_avail, GtkWidget *gtk_widget, void *priv,
	void (*update)(struct iio_widget *),
	void (*update_value)(struct iio_widget *, const char *, size_t),
	void (*save)(struct iio_widget *))
{
	if (!gtk_widget) {
		const char *name = iio_device_get_name(dev) ?:
			iio_device_get_id(dev);
		printf("Missing widget for %s/%s\n", name, attr_name);
	}

	memset(widget, 0, sizeof(*widget));

	widget->dev = dev;
	widget->chn = chn;
	widget->attr_name = attr_name;
	widget->attr_name_avail = attr_name_avail;
	widget->widget = gtk_widget;
	widget->update = update;
	widget->update_value = update_value;
	widget->save = save;
	widget->priv = priv;
}

static void iio_spin_button_update_value(struct iio_widget *widget,
		const char *src, size_t len)
{
	gdouble freq, mag, min, max;
	gdouble scale = widget->priv ? *(gdouble *)widget->priv : 1.0;
	char *end;

	mag = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget->widget));
	gtk_spin_button_get_range(GTK_SPIN_BUTTON(widget->widget), &min, &max);

	freq = g_ascii_strtod(src, &end);
	if (end == src)
		return;

	if (widget->priv_convert_function)
		freq = ((double (*)(double, bool))widget->priv_convert_function)(freq, true);

	freq /= fabs(scale);

	/* if scale is negative, we treat things a little differently */
	if (scale < 0) {
		/* if the setting is negative, and it can be set negative */
		if (mag < 0 && min < 0)
			freq *= -1;
		else if (min >= 0)
			freq *= -1;
	}

	gtk_spin_button_set_value(GTK_SPIN_BUTTON (widget->widget), freq);
}

static void iio_spin_button_update(struct iio_widget *widget)
{
	ssize_t ret;
	char buf[0x100];

	if (widget->chn)
		ret = iio_channel_attr_read(widget->chn,
				widget->attr_name, buf, sizeof(buf));
	else
		ret = iio_device_attr_read(widget->dev,
				widget->attr_name, buf, sizeof(buf));
	if (ret > 0)
		iio_spin_button_update_value(widget, buf, ret);
	else if (ret == -ENODEV)
		gtk_widget_hide(widget->widget);
}

static void spin_button_save(struct iio_widget *widget, bool is_double)
{
	gdouble freq, min;
	gdouble scale = widget->priv ? *(gdouble *)widget->priv : 1.0;

	freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON (widget->widget));
	min = gtk_adjustment_get_lower(gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(widget->widget)));
	if (scale < 0 && min < 0)
		freq = fabs(freq * scale);
	else
		freq *= scale;

	if (widget->priv_convert_function)
		freq = ((double (*)(double, bool))widget->priv_convert_function)(freq, false);

	if (widget->chn) {
		if (is_double)
			iio_channel_attr_write_double(widget->chn,
					widget->attr_name, freq);
		else
			iio_channel_attr_write_longlong(widget->chn,
					widget->attr_name, (long long) freq);
	} else {
		if (is_double)
			iio_device_attr_write_double(widget->dev,
					widget->attr_name, freq);
		else
			iio_device_attr_write_longlong(widget->dev,
					widget->attr_name, (long long) freq);
	}
}

static void iio_spin_button_savedbl(struct iio_widget *widget)
{
	return spin_button_save(widget, true);
}

void iio_spin_button_save(struct iio_widget *widget)
{
	return spin_button_save(widget, false);
}

void iio_spin_button_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name,
	GtkWidget *spin_button, const gdouble *scale)
{
	iio_widget_init(widget, dev, chn, attr_name, NULL, spin_button,
		(void *)scale, iio_spin_button_update,
		iio_spin_button_update_value, iio_spin_button_savedbl);
}

void iio_spin_button_int_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name,
	GtkWidget *spin_button, const gdouble *scale)
{
	iio_widget_init(widget, dev, chn, attr_name, NULL, spin_button,
		(void *)scale, iio_spin_button_update,
		iio_spin_button_update_value, iio_spin_button_save);
}

void iio_spin_button_s64_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name,
	GtkWidget *spin_button, const gdouble *scale)
{
	iio_widget_init(widget, dev, chn, attr_name, NULL, spin_button,
		(void *)scale, iio_spin_button_update,
		iio_spin_button_update_value, iio_spin_button_save);
}

static void iio_toggle_button_save(struct iio_widget *widget)
{
	bool active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (widget->widget));
	active = widget->priv ? !active : active;

	if (widget->chn)
		iio_channel_attr_write_bool(widget->chn,
				widget->attr_name, active);
	else
		iio_device_attr_write_bool(widget->dev,
				widget->attr_name, active);
}

static void iio_toggle_button_update_value(struct iio_widget *widget,
		const char *src, size_t len)
{
	bool active;

	if (len != 2)
		return;

	active = src[0] == '1' || src[0] == 'Y';
	active = widget->priv ? !active : active;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (widget->widget), active);
}

static void iio_toggle_button_update(struct iio_widget *widget)
{
	char buf[0x100];
	ssize_t ret;

	if (widget->chn)
		ret = iio_channel_attr_read(widget->chn,
				widget->attr_name, buf, sizeof(buf));
	else
		ret = iio_device_attr_read(widget->dev,
				widget->attr_name, buf, sizeof(buf));
	if (ret > 0)
		iio_toggle_button_update_value(widget, buf, ret);
	else if (ret == -ENODEV)
		gtk_widget_hide(widget->widget);
}

static void iio_toggle_button_init(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkWidget *toggle_button, const bool invert)
{
	iio_widget_init(widget, dev, chn, attr_name, NULL, toggle_button,
		(void *)invert, iio_toggle_button_update,
		iio_toggle_button_update_value, iio_toggle_button_save);
}

static void iio_button_save(struct iio_widget *widget)
{
	if (widget->chn)
		iio_channel_attr_write_bool(widget->chn,
					    widget->attr_name, 1);
	else
		iio_device_attr_write_bool(widget->dev,
						   widget->attr_name, 1);
}

static void iio_button_update_value(struct iio_widget *widget,
				    const char *src, size_t len)
{

}

static void iio_button_update(struct iio_widget *widget)
{

}

static void iio_button_init(struct iio_widget *widget,
			    struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
			    GtkWidget *button)
{
	iio_widget_init(widget, dev, chn, attr_name, NULL, button,
			NULL, iio_button_update,
			iio_button_update_value, iio_button_save);
}

static void iio_combo_box_save(struct iio_widget *widget)
{
	gchar *text;

	text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget->widget));
	if (text == NULL)
		return;

	if (widget->chn)
		iio_channel_attr_write(widget->chn, widget->attr_name, text);
	else
		iio_device_attr_write(widget->dev, widget->attr_name, text);
	g_free(text);
}

static void iio_combo_box_no_avail_flush_update_value(struct iio_widget *widget, const char *src,
						      const size_t len)
{
	GtkComboBox *combo_box = GTK_COMBO_BOX(widget->widget);
	GtkTreeModel *model = gtk_combo_box_get_model(combo_box);
	GtkTreeIter iter;
	gboolean has_iter;
	int (*compare)(const char *, const char *);
	char *item;


	if (widget->priv)
		compare = widget->priv;
	else
		compare = strcmp;

	has_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (has_iter) {
		gtk_tree_model_get(model, &iter, 0, &item, -1);
		if (compare (src, item) == 0) {
			gtk_combo_box_set_active_iter(combo_box, &iter);
			g_free(item);
			break;
		}
		g_free(item);
		has_iter = gtk_tree_model_iter_next(model, &iter);
	}
}

static void iio_combo_box_no_avail_flush_update(struct iio_widget *widget)
{
	ssize_t len;
	char text[256];

	if (widget->chn)
		len = iio_channel_attr_read(widget->chn, widget->attr_name, text, sizeof(text));
	else
		len = iio_device_attr_read(widget->dev, widget->attr_name, text, sizeof(text));

	if (len > 0)
		iio_combo_box_no_avail_flush_update_value(widget, text, len);
}

void iio_combo_box_init_no_avail_flush(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name, const char *attr_name_avail,
	GtkWidget *combo_box, int (*compare)(const char *a, const char *b))
{
	/*
	 * Here we assume that the available list cannot change, hence construct our combo box
	 * alternatives only once...
	 */
	if (attr_name_avail) {
		int ret, item;
		char text[1024];
		gchar **items_avail = NULL;

		if (chn)
			ret = iio_channel_attr_read(chn, attr_name_avail, text,
						    sizeof(text));
		else
			ret = iio_device_attr_read(dev, attr_name_avail, text,
						   sizeof(text));

		if (ret < 0)
			return;

		gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(combo_box));

		items_avail = g_strsplit (text, " ", 0);
		for (item = 0; items_avail[item]; item++) {
			if (items_avail[item][0] == '\0')
				continue;

			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box),
						       items_avail[item]);
		}

		g_strfreev(items_avail);
	}

	iio_widget_init(widget, dev, chn, attr_name, attr_name_avail, combo_box, (void *)compare,
			iio_combo_box_no_avail_flush_update,
			iio_combo_box_no_avail_flush_update_value, iio_combo_box_save);

	gtk_combo_box_set_entry_text_column(GTK_COMBO_BOX(widget->widget), 0);
}

static void iio_combo_box_update_value(struct iio_widget *widget,
		const char *src, size_t len)
{
	int (*compare)(const char *, const char *);
	GtkComboBox *combo_box;
	GtkTreeIter iter;
	GtkTreeModel *model;
	char text2[1024], *item;
	gchar **items_avail = NULL, **saveditems_avail;
	gboolean has_iter;
	ssize_t ret;

	combo_box = GTK_COMBO_BOX(widget->widget);
	model = gtk_combo_box_get_model(combo_box);

	if (widget->attr_name_avail) {
		if (widget->chn)
			ret = iio_channel_attr_read(widget->chn,
					widget->attr_name_avail, text2, sizeof(text2));
		else
			ret = iio_device_attr_read(widget->dev,
					widget->attr_name_avail, text2, sizeof(text2));
		if (ret < 0)
			return;

		gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(combo_box));

		saveditems_avail = items_avail = g_strsplit (text2, " ", 0);

		for (; NULL != *items_avail; items_avail++) {
			if (*items_avail[0] == '\0')
				continue;
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widget->widget),
				*items_avail);
		}

		if (saveditems_avail)
			g_strfreev(saveditems_avail);
	}

	if (widget->priv)
		compare = widget->priv;
	else
		compare = strcmp;

	has_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (has_iter) {
		gtk_tree_model_get(model, &iter, 0, &item, -1);
		if (compare (src, item) == 0) {
			gtk_combo_box_set_active_iter(combo_box, &iter);
			g_free(item);
			break;
		}
		g_free(item);
		has_iter = gtk_tree_model_iter_next(model, &iter);
	}
}

static void iio_combo_box_update(struct iio_widget *widget)
{
	ssize_t len;
	char text[1024];

	if (widget->chn)
		len = iio_channel_attr_read(widget->chn,
				widget->attr_name, text, sizeof(text));
	else
		len = iio_device_attr_read(widget->dev,
				widget->attr_name, text, sizeof(text));
	if (len > 0)
		iio_combo_box_update_value(widget, text, len);
}

void iio_combo_box_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name, const char *attr_name_avail,
	GtkWidget *combo_box, int (*compare)(const char *a, const char *b))
{
	iio_widget_init(widget, dev, chn, attr_name, attr_name_avail, combo_box,
		(void *)compare, iio_combo_box_update,
		iio_combo_box_update_value, iio_combo_box_save);

	gtk_combo_box_set_entry_text_column(GTK_COMBO_BOX(widget->widget), 0);

}

void iio_widget_update(struct iio_widget *widget)
{
	widget->update(widget);
}

static gboolean iio_widget_signal_unblock(gpointer arg)
{
	struct iio_widget *widget = arg;

	g_signal_handlers_unblock_matched(G_OBJECT(widget->widget), G_SIGNAL_MATCH_DATA, 0, 0,
					  NULL, NULL, widget->sig_handler_data);
	/* just meant to run once... */
	return FALSE;
}

void iio_widget_update_value(struct iio_widget *widget, const char *attr_name, size_t len)
{
	guint sig = 0;

	if (widget->sig_handler_data)
		sig = g_signal_handlers_block_matched(G_OBJECT(widget->widget), G_SIGNAL_MATCH_DATA,
						      0, 0, NULL, NULL, widget->sig_handler_data);
	widget->update_value(widget, attr_name, len);

	if (sig)
		g_timeout_add(1, (GSourceFunc)iio_widget_signal_unblock, widget);
}
/*
* The point of these is that when we update a widget, we can receive a different value from the
* iio dev from the one in the GUI (eg: a failed call to widget->save() or an autonomous update).
* In these case, we don't really want our widget signal handler to be called on a value that we
* we know the device is already holding...
*/
void iio_widget_update_block_signals_by_data(struct iio_widget *widget)
{
	guint sig = 0;

	if (widget->sig_handler_data)
		sig = g_signal_handlers_block_matched(G_OBJECT(widget->widget), G_SIGNAL_MATCH_DATA,
						      0, 0, NULL, NULL, widget->sig_handler_data);

	widget->update(widget);
	/*
	* It looks like calling the unblock function does not work for spinbuttons if it's called
	* from it's signal handler context. Hence, we start a timer that will only run once and
	* unblock the signal in ~1ms...
	*/
	if (sig)
		g_timeout_add(1, (GSourceFunc)iio_widget_signal_unblock, widget);
}

void iio_widget_save_block_signals_by_data(struct iio_widget *widget)
{
	widget->save(widget);
	iio_widget_update_block_signals_by_data(widget);
}

void iio_widget_save_block_signals_by_data_cb(GtkWidget *widget, struct iio_widget *iio_widget)
{
	iio_widget_save_block_signals_by_data(iio_widget);
}

void iio_update_widgets_block_signals_by_data(struct iio_widget *widgets, unsigned int num_widgets)
{
	unsigned int i;

	for (i = 0; i < num_widgets; i++)
		iio_widget_update_block_signals_by_data(&widgets[i]);
}

void iio_widget_save(struct iio_widget *widget)
{
	widget->save(widget);
	widget->update(widget);
}

void iio_widget_save_cb(GtkWidget *widget, struct iio_widget *iio_widget)
{
	iio_widget_save(iio_widget);
}

void iio_update_widgets(struct iio_widget *widgets, unsigned int num_widgets)
{
	unsigned int i;

	for (i = 0; i < num_widgets; i++)
		iio_widget_update(&widgets[i]);
}

static int iio_widget_get_signal_name(struct iio_widget *w, char *signal_name, size_t len)
{
	if (GTK_IS_CHECK_BUTTON(w->widget))
		snprintf(signal_name, len, "%s", "toggled");
	else if (GTK_IS_TOGGLE_BUTTON(w->widget))
		snprintf(signal_name, len, "%s", "toggled");
	else if (GTK_IS_SPIN_BUTTON(w->widget))
		snprintf(signal_name, len, "%s", "value-changed");
	else if (GTK_IS_COMBO_BOX_TEXT(w->widget))
		snprintf(signal_name, len, "%s", "changed");
	else {
		printf("unhandled widget type, attribute: %s\n",
			  w->attr_name);
		return -ENOENT;
	}

	return 0;
}

void iio_make_widgets_update_signal_based(struct iio_widget *widgets, unsigned int num_widgets,
					  GCallback handler)
{
	char signal_name[25];
	unsigned int i;

	for (i = 0; i < num_widgets; i++) {
		if (iio_widget_get_signal_name(&widgets[i], signal_name, sizeof(signal_name)))
			return;

		if (GTK_IS_SPIN_BUTTON(widgets[i].widget) &&
		    widgets[i].priv_progress != NULL) {
			iio_spin_button_progress_activate(&widgets[i]);
		} else {
			widgets[i].sig_handler_data = &widgets[i];
			g_signal_connect(G_OBJECT(widgets[i].widget), signal_name,
					 handler, &widgets[i]);
		}
	}
}

void iio_make_widget_update_signal_based(struct iio_widget *widget, GCallback handler, gpointer data)
{
	char signal_name[25];

	if (iio_widget_get_signal_name(widget, signal_name, sizeof(signal_name)))
		return;

	if (GTK_IS_SPIN_BUTTON(widget->widget) &&
		    widget->priv_progress != NULL) {
			iio_spin_button_progress_activate(widget);
	} else {
		widget->sig_handler_data = data;
		g_signal_connect(G_OBJECT(widget->widget), signal_name, handler, data);
	}
}

static int __cb_dev_update(struct iio_device *dev, const char *attr,
		const char *value, size_t len, void *d)
{
	unsigned int i;
	struct update_widgets_params *params = d;

	for (i = 0; i < params->nb; i++) {
		struct iio_widget *widget = &params->widgets[i];
		if (widget->update_value && !widget->chn &&
				widget->dev == dev &&
				!strcmp(widget->attr_name, attr)) {
			widget->update_value(widget, value, len);
			return 0;
		}
	}

	return 0;
}

static int __cb_chn_update(struct iio_channel *chn, const char *attr,
		const char *value, size_t len, void *d)
{
	unsigned int i;
	struct update_widgets_params *params = d;

	for (i = 0; i < params->nb; i++) {
		struct iio_widget *widget = &params->widgets[i];
		if (widget->update_value && widget->chn == chn &&
				!strcmp(widget->attr_name, attr)) {
			widget->update_value(widget, value, len);
			return 0;
		}
	}

	return 0;
}

void iio_update_widgets_of_device(struct iio_widget *widgets,
		unsigned int num_widgets, struct iio_device *dev)
{
	unsigned int i;
	struct update_widgets_params params = {
		.widgets = widgets,
		.nb = num_widgets,
	};

	iio_device_attr_read_all(dev, __cb_dev_update, &params);

	for (i = 0; i < iio_device_get_channels_count(dev); i++)
		iio_channel_attr_read_all(iio_device_get_channel(dev, i),
				__cb_chn_update, &params);
}

void iio_save_widgets(struct iio_widget *widgets, unsigned int num_widgets)
{
	unsigned int i;

	for (i = 0; i < num_widgets; i++)
		iio_widget_save(&widgets[i]);
}

void iio_spin_button_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale)
{
	iio_spin_button_init(widget, dev, chn, attr_name,
		GTK_WIDGET(gtk_builder_get_object(builder, widget_name)),
		scale);
}

void iio_spin_button_int_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale)
{
	iio_spin_button_int_init(widget, dev, chn, attr_name,
		GTK_WIDGET(gtk_builder_get_object(builder, widget_name)),
		scale);
}

void iio_spin_button_s64_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const gdouble *scale)
{
	iio_spin_button_s64_init(widget, dev, chn, attr_name,
		GTK_WIDGET(gtk_builder_get_object(builder, widget_name)),
		scale);
}

void iio_combo_box_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	const char *attr_name_avail,
	GtkBuilder *builder, const char *widget_name,
	int (*compare)(const char *a, const char *b))
{
	iio_combo_box_init(widget, dev, chn, attr_name, attr_name_avail,
		GTK_WIDGET(gtk_builder_get_object(builder, widget_name)),
		compare);
}

/*
 * The difference to @iio_combo_box_init_from_builder() is that the combo_box entries won't be
 * refreshed at every update_value() call. This assumes that the IIO available attr cannot really
 * change at runtime which is true most of the times... Having this done like this, let's us do
 * things like 'widget->save()' followed by 'widget->update()' in combo boxes signal handlers
 * without getting an infinite loop (as updating triggers the handler again).
 */
void iio_combo_box_init_no_avail_flush_from_builder(struct iio_widget *widget, struct iio_device *dev,
		struct iio_channel *chn, const char *attr_name, const char *attr_name_avail,
		GtkBuilder *builder, const char *widget_name, int (*compare)(const char *a, const char *b))
{
	iio_combo_box_init_no_avail_flush(widget, dev, chn, attr_name, attr_name_avail,
		GTK_WIDGET(gtk_builder_get_object(builder, widget_name)),
		compare);
}

void iio_toggle_button_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const bool invert)
{
	iio_toggle_button_init(widget, dev, chn, attr_name,
		GTK_WIDGET(gtk_builder_get_object(builder, widget_name)), invert);
}

void iio_button_init_from_builder(struct iio_widget *widget,
	 struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkBuilder *builder, const char *widget_name)
{
	iio_button_init(widget, dev, chn, attr_name,
	       GTK_WIDGET(gtk_builder_get_object(builder, widget_name)));
}

/*
 * struct progress_data - Information about the progress spinbutton
 *
 * @is_progress_spin_button: Used for progress spinbutton identification
 * @progress: Progress status. Range between 0.0 and 1.0
 * @timeoutID: Handler ID of callback that increases progress step by step
 * @value_changed_hid: Handler ID of callback for a "value-changed" event of the
 *                     progress spinbutton
 * @skip_widget_save: Allows widget to skip writing its value to the
 *                    driver attrbiute when the progress bar completes
 * @on_complete_data: Data to be passed to the on_complete() function
 * @on_complete: Function to be called when progress reaches 1.0.
 */
struct progress_data {
	gboolean is_progress_spin_button;
	gfloat progress;
	gint timeoutID;
	gint value_changed_hid;
	gboolean skip_widget_save;
	void *on_complete_data;
	void (*on_complete)(void *data);
};

/*
 * Gets called periodically to increase the progress with one step.
 * When progress is complete saves the spinbutton value to file, clears the
 * progress and stops the function to be called periodically.
 */
static gboolean spin_button_progress_step(struct iio_widget *iio_w)
{
	struct progress_data *pdata = iio_w->priv_progress;
	void (*on_complete_cb)(void *) = pdata->on_complete;

	if (pdata->progress < 1.0) {
		pdata->progress += 0.095;
		gtk_entry_set_progress_fraction(GTK_ENTRY(iio_w->widget), pdata->progress);

		return TRUE;
	} else {
		pdata->progress = 0.0;
		gtk_entry_set_progress_fraction(GTK_ENTRY(iio_w->widget), pdata->progress);
		if (!pdata->skip_widget_save)
			iio_widget_save(iio_w);
		if (pdata->on_complete != NULL)
			on_complete_cb(pdata->on_complete_data);
		pdata->timeoutID = -1;

		return FALSE;
	}
}

/*
 * When a "value-changed" event of the spinbutton occurs the progress bar of the
 * spinbutton starts to increase until reaches the complete state. If another
 * event occurs while the progress is not finished, the progress will be reset.
 */

static void delayed_spin_button_update_cb(GtkSpinButton *spinbutton,
	struct iio_widget *iio_w)
{
	struct progress_data *pdata = iio_w->priv_progress;

	if (pdata->timeoutID != - 1)
		pdata->progress = 0.0;
	else
		pdata->timeoutID = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 90,
				(GSourceFunc)spin_button_progress_step, iio_w, NULL);
}

/*
 * Customises a iio_widget that contains a GtkSpinButton widget to be able to
 * work as a progress spinbutton.
 */
void iio_spin_button_add_progress(struct iio_widget *iio_w)
{
	struct progress_data *pdata;

	if (GTK_IS_SPIN_BUTTON(iio_w->widget) == FALSE) {
		const char *name = iio_device_get_name(iio_w->dev) ?:
			iio_device_get_id(iio_w->dev);
		printf("The widget connected to the attribute: %s of device: %s is not a GtkSpinButton\n",
			iio_w->attr_name, name);
		return;
	}

	pdata = malloc(sizeof(struct progress_data));
	pdata->is_progress_spin_button = TRUE;
	pdata->progress = 0.0;
	pdata->timeoutID = -1;
	pdata->value_changed_hid = -1;
	pdata->skip_widget_save = FALSE;
	pdata->on_complete_data = NULL;
	pdata->on_complete = NULL;
	iio_w->priv_progress = pdata;
}

/*
 * In order for the progress spinbutton to work, this function must be called
 * to connect the required callback to the widget.
 */
void iio_spin_button_progress_activate(struct iio_widget *iio_w)
{
	struct progress_data *pdata = iio_w->priv_progress;

	if (GTK_IS_SPIN_BUTTON(iio_w->widget) == FALSE) {
		const char *name = iio_device_get_name(iio_w->dev) ?:
			iio_device_get_id(iio_w->dev);
		printf("The widget connected to the attribute: %s of device: %s is not a GtkSpinButton\n",
			iio_w->attr_name, name);
		return;
	}

	pdata->value_changed_hid = g_signal_connect(G_OBJECT(iio_w->widget),
		"value-changed", G_CALLBACK(delayed_spin_button_update_cb), iio_w);
}

/*
 * Set a user function to be called when the progress is completed. The function
 * can take one generic pointer as parameter.
 */
void iio_spin_button_set_on_complete_function(struct iio_widget *iio_w,
	void(*on_complete)(void *), void *data)
{
	struct progress_data *pdata = iio_w->priv_progress;

	if (GTK_IS_SPIN_BUTTON(iio_w->widget) == FALSE) {
		const char *name = iio_device_get_name(iio_w->dev) ?:
			iio_device_get_id(iio_w->dev);
		printf("The widget connected to the attribute: %s of device: %s is not a GtkSpinButton\n",
			iio_w->attr_name, name);
		return;
	}

	pdata->on_complete = on_complete;
	pdata->on_complete_data = data;
}

/*
 * Allow user to disable the function that saves the value of the widget
 * to the driver when progress bar reaches 100%. User may use
 * iio_spin_button_set_on_complete_function() in order to provide a
 * more complex logic before saving the value of the widget.
 */

void iio_spin_button_skip_save_on_complete(struct iio_widget *iio_w,
		gboolean skip)
{
	struct progress_data *pdata = iio_w->priv_progress;

	pdata->skip_widget_save = skip;
}

/*
 * Disconnect the callback of the "value-changed" event from the progress
 * spinbutton.
 */
void iio_spin_button_progress_deactivate(struct iio_widget *iio_w)
{
	struct progress_data *pdata = iio_w->priv_progress;

	if (GTK_IS_SPIN_BUTTON(iio_w->widget) == FALSE) {
		const char *name = iio_device_get_name(iio_w->dev) ?:
			iio_device_get_id(iio_w->dev);
		printf("The widget connected to the attribute: %s of device: %s is not a GtkSpinButton\n",
			iio_w->attr_name, name);
		return;
	}

	g_signal_handler_disconnect(iio_w->widget, pdata->value_changed_hid);
}

/*
 * Remove all implementation made to convert a iio_widget to a progress
 * spinbutton.
 */
void iio_spin_button_remove_progress(struct iio_widget *iio_w)
{
	if (GTK_IS_SPIN_BUTTON(iio_w->widget) == FALSE) {
		const char *name = iio_device_get_name(iio_w->dev) ?:
			iio_device_get_id(iio_w->dev);
		printf("The widget connected to the attribute: %s of device: %s is not a GtkSpinButton\n",
			iio_w->attr_name, name);
		return;
	}

	iio_spin_button_progress_deactivate(iio_w);
	if (iio_w->priv_progress)
		free(iio_w->priv_progress);
}

void iio_spin_button_set_convert_function(struct iio_widget *iio_w,
		double (*convert)(double, bool))
{
	iio_w->priv_convert_function = convert;
}
