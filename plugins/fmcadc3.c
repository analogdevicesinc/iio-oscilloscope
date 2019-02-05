/**
 * Copyright (C) 2015 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <glib.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../libini2.h"
#include "../osc_plugin.h"
#include "../config.h"

#define THIS_DRIVER "FMCADC3"
#define IIO_DEVICE "ada4961"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

static bool can_update_widgets;
static struct iio_widget widgets[10];
static unsigned int num_widgets;

static struct iio_context *ctx;
static struct iio_device *dev;

/* Control Widgets */
static GtkWidget *gain;

/* Default Plugin Variables */
static GtkNotebook *nbook;
static GtkWidget *fmcadc3_panel;

static const char *fmcadc3_sr_attribs[] = {
	IIO_DEVICE".out_voltage0_hardwaregain",
};

static void reload_button_clicked(GtkButton *btn, gpointer data)
{
	iio_update_widgets_of_device(widgets, num_widgets, dev);
}

static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	iio_w->save(iio_w);
}

static void make_widget_update_signal_based(struct iio_widget *widgets,
	unsigned int num_widgets)
{
	char signal_name[25];
	unsigned int i;

	for (i = 0; i < num_widgets; i++) {
		if (GTK_IS_CHECK_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_TOGGLE_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_SPIN_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "value-changed");
		else if (GTK_IS_COMBO_BOX_TEXT(widgets[i].widget))
			sprintf(signal_name, "%s", "changed");
		else
			printf("unhandled widget type, attribute: %s\n", widgets[i].attr_name);

		if (GTK_IS_SPIN_BUTTON(widgets[i].widget) &&
			widgets[i].priv_progress != NULL) {
				iio_spin_button_progress_activate(&widgets[i]);
		} else {
			g_signal_connect(G_OBJECT(widgets[i].widget), signal_name, G_CALLBACK(save_widget_value), &widgets[i]);
		}
	}
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	update_from_ini(ini_fn, THIS_DRIVER, dev, fmcadc3_sr_attribs,
			ARRAY_SIZE(fmcadc3_sr_attribs));

	if (can_update_widgets)
		reload_button_clicked(NULL, NULL);
}

static GtkWidget * fmcadc3_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	struct iio_channel *ch0;

	can_update_widgets = false;

	ctx = osc_create_context();
	if (!ctx)
		goto init_abort;

	dev = iio_context_find_device(ctx,IIO_DEVICE);
	ch0 = iio_device_find_channel(dev, "voltage0", true);

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (osc_load_glade_file(builder, "fmcadc3") < 0)
		return NULL;

	fmcadc3_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcadc3_panel"));
	gain = GTK_WIDGET(gtk_builder_get_object(builder, "gain"));

	/* Bind the IIO device files to the GUI widgets */

	iio_spin_button_int_init_from_builder(&widgets[num_widgets++],
		dev, ch0, "hardwaregain", builder, "gain", NULL);

	if (ini_fn)
		load_profile(NULL, ini_fn);

	/* Update all widgets with current values */
	iio_update_widgets_of_device(widgets, num_widgets, dev);

	/* Connect signals */
	g_builder_connect_signal(builder, "fmcadc3_settings_reload", "clicked",
		G_CALLBACK(reload_button_clicked), NULL);
	make_widget_update_signal_based(widgets, num_widgets);

	can_update_widgets = true;

	return fmcadc3_panel;

init_abort:
	if (ctx)
		osc_destroy_context(ctx);

	return NULL;
}

static void fmcadc3_get_preferred_size(const struct osc_plugin *plugin, int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		save_to_ini(f, THIS_DRIVER, dev, fmcadc3_sr_attribs,
				ARRAY_SIZE(fmcadc3_sr_attribs));
		fclose(f);
	}
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	g_source_remove_by_user_data(ctx);

	if (ini_fn)
		save_profile(NULL, ini_fn);

	osc_destroy_context(ctx);
}

struct osc_plugin plugin;

static bool fmcadc3_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	return !!iio_context_find_device(osc_ctx, IIO_DEVICE);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = fmcadc3_identify,
	.init = fmcadc3_init,
	.get_preferred_size = fmcadc3_get_preferred_size,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
