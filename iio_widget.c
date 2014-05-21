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
#include <string.h>
#include <math.h>

#include "osc.h"
#include "iio_widget.h"

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
	void (*update)(struct iio_widget *), void (*save)(struct iio_widget *))
{
	if (!gtk_widget) {
		const char *name = iio_device_get_name(dev) ?:
			iio_device_get_id(dev);
		printf("Missing widget for %s/%s\n", name, attr_name);
	}

	widget->dev = dev;
	widget->chn = chn;
	widget->attr_name = attr_name;
	widget->attr_name_avail = attr_name_avail;
	widget->widget = gtk_widget;
	widget->update = update;
	widget->save = save;
	widget->priv = priv;
}

static void iio_spin_button_update(struct iio_widget *widget)
{
	gdouble freq, mag, min, max;
	gdouble scale = widget->priv ? *(gdouble *)widget->priv : 1.0;
	int ret;

	mag = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget->widget));
	gtk_spin_button_get_range(GTK_SPIN_BUTTON(widget->widget), &min, &max);
	if (widget->chn)
		ret = iio_channel_attr_read_double(widget->chn,
				widget->attr_name, &freq);
	else
		ret = iio_device_attr_read_double(widget->dev,
				widget->attr_name, &freq);
	if (ret < 0)
		return;

	freq /= fabs(scale);

	/* if scale is negative, we treat things a little differently */
	if (scale < 0) {
		/* if the setting is negative, and it can be set negative */
		if (mag < 0 && min < 0)
			freq *= -1;
	}

	gtk_spin_button_set_value(GTK_SPIN_BUTTON (widget->widget), freq);
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

static void iio_spin_button_save(struct iio_widget *widget)
{
	return spin_button_save(widget, false);
}

void iio_spin_button_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name,
	GtkWidget *spin_button, const gdouble *scale)
{
	iio_widget_init(widget, dev, chn, attr_name, NULL, spin_button,
		(void *)scale, iio_spin_button_update, iio_spin_button_savedbl);
}

void iio_spin_button_int_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name,
	GtkWidget *spin_button, const gdouble *scale)
{
	iio_widget_init(widget, dev, chn, attr_name, NULL, spin_button,
		(void *)scale, iio_spin_button_update, iio_spin_button_save);
}

void iio_spin_button_s64_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name,
	GtkWidget *spin_button, const gdouble *scale)
{
	iio_widget_init(widget, dev, chn, attr_name, NULL, spin_button,
		(void *)scale, iio_spin_button_update, iio_spin_button_save);
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

static void iio_toggle_button_update(struct iio_widget *widget)
{
	bool active;

	if (widget->chn)
		iio_channel_attr_read_bool(widget->chn,
				widget->attr_name, &active);
	else
		iio_device_attr_read_bool(widget->dev,
				widget->attr_name, &active);
	active = widget->priv ? !active : active;
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (widget->widget), active);
}

static void iio_toggle_button_init(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkWidget *toggle_button, const bool invert)
{
	iio_widget_init(widget, dev, chn, attr_name, NULL, toggle_button,
		(void *)invert, iio_toggle_button_update, iio_toggle_button_save);
}

static void iio_combo_box_save(struct iio_widget *widget)
{
	const char *text;

	text = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget->widget));
	if (text == NULL)
		return;

	if (widget->chn)
		iio_channel_attr_write(widget->chn, widget->attr_name, text);
	else
		iio_device_attr_write(widget->dev, widget->attr_name, text);
}

static void iio_combo_box_update(struct iio_widget *widget)
{
	int (*compare)(const char *, const char *);
	GtkComboBox *combo_box;
	GtkTreeIter iter;
	GtkTreeModel *model;
	char text[1024], text2[1024], *item;
	gchar **items_avail = NULL, **saveditems_avail;
	gboolean has_iter;
	ssize_t ret;

	if (widget->chn)
		ret = iio_channel_attr_read(widget->chn,
				widget->attr_name, text, sizeof(text));
	else
		ret = iio_device_attr_read(widget->dev,
				widget->attr_name, text, sizeof(text));
	if (ret < 0)
		return;

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

		/* may use gtk_combo_box_text_remove_all gtk3 only */
		gtk_list_store_clear (GTK_LIST_STORE (model));
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
		if (compare (text, item) == 0) {
			gtk_combo_box_set_active_iter(combo_box, &iter);
			g_free(item);
			break;
		}
		g_free(item);
		has_iter = gtk_tree_model_iter_next(model, &iter);
	}
}

void iio_combo_box_init(struct iio_widget *widget, struct iio_device *dev,
	struct iio_channel *chn, const char *attr_name, const char *attr_name_avail,
	GtkWidget *combo_box, int (*compare)(const char *a, const char *b))
{
	iio_widget_init(widget, dev, chn, attr_name, attr_name_avail, combo_box,
		(void *)compare, iio_combo_box_update, iio_combo_box_save);
}

void iio_widget_update(struct iio_widget *widget)
{
	widget->update(widget);
}

void iio_widget_save(struct iio_widget *widget)
{
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

void iio_toggle_button_init_from_builder(struct iio_widget *widget,
	struct iio_device *dev, struct iio_channel *chn, const char *attr_name,
	GtkBuilder *builder, const char *widget_name, const bool invert)
{
	iio_toggle_button_init(widget, dev, chn, attr_name,
		GTK_WIDGET(gtk_builder_get_object(builder, widget_name)), invert);
}

/*
 * struct progress_data - Information about the progress spinbutton
 *
 * @is_progress_spin_button: Used for progress spinbutton identification
 * @progress: Progress status. Range between 0.0 and 1.0
 * @timeoutID: Handler ID of callback that increases progress step by step
 * @value_changed_hid: Handler ID of callback for a "value-changed" event of the
 *                     progress spinbutton
 * @on_complete: Function to be called when progress reaches 1.0.
 */
struct progress_data {
	gboolean is_progress_spin_button;
	gfloat progress;
	gint timeoutID;
	gint value_changed_hid;
	void (*on_complete)(void);
};

/*
 * Gets called periodically to increase the progress with one step.
 * When progress is complete saves the spinbutton value to file, clears the
 * progress and stops the function to be called periodically.
 */
static gboolean spin_button_progress_step(struct iio_widget *iio_w)
{
	struct progress_data *pdata = iio_w->priv_progress;
	void (*on_complete_cb)(void) = pdata->on_complete;

	if (pdata->progress < 1.0) {
		pdata->progress += 0.095;
		gtk_entry_set_progress_fraction(GTK_ENTRY(iio_w->widget), pdata->progress);

		return TRUE;
	} else {
		pdata->progress = 0.0;
		gtk_entry_set_progress_fraction(GTK_ENTRY(iio_w->widget), pdata->progress);
		iio_widget_save(iio_w);
		if (pdata->on_complete != NULL)
			on_complete_cb();
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
		pdata->timeoutID = g_timeout_add(90, (GSourceFunc)spin_button_progress_step, iio_w);
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
 * Set the a user function to be called when the progress is completed.
 */
void iio_spin_button_set_on_complete_function(struct iio_widget *iio_w,
	void(*on_complete)(void))
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

	g_signal_handler_disconnect(iio_w, pdata->value_changed_hid);
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
