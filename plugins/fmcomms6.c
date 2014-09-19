/**
 * Copyright (C) 2012-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <glib/gthread.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <malloc.h>
#include <values.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/utsname.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"


#define ADC_DEVICE "ad9652"
#define PLL_DEVICE "adf4351-rx-lpc"

static const gdouble mhz_scale = 1000000.0;
static const gdouble abs_mhz_scale = -1000000.0;
static const gdouble khz_scale = 1000.0;
static const gdouble inv_scale = -1.0;

static struct iio_widget rx_widgets[5];
static unsigned int num_rx;

static struct iio_context *ctx;
static struct iio_device *adc, *pll;

static unsigned int rx_lo;

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *fmcomms6_panel;
static gboolean plugin_detached;

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

static void rx_update_labels_on_complete(void *data)
{
	rx_update_labels();
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

static void reload_button_clicked(GtkButton *btn, gpointer data)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

static int fmcomms6_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	//~ struct iio_channel *ch0 = iio_device_find_channel(pll, "voltage0", false);
	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (!gtk_builder_add_from_file(builder, "fmcomms6.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "fmcomms6.glade", NULL);

	fmcomms6_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms6_panel"));

	/* Bind the IIO device files to the GUI widgets */

	/* Receive Chain */
	iio_spin_button_s64_init_from_builder(&rx_widgets[num_rx++],
		pll, ch0, "RX_LO_frequency", builder,
		"spin_rx_lo_freq", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

	g_builder_connect_signal(builder, "fmcomms6_settings_reload", "clicked",
		G_CALLBACK(reload_button_clicked), NULL);

	make_widget_update_signal_based(rx_widgets, num_rx);
	iio_spin_button_set_on_complete_function(&rx_widgets[rx_lo],
		rx_update_labels_on_complete, NULL);
	rx_update_values();

	this_page = gtk_notebook_append_page(nbook, fmcomms6_panel, NULL);
	gtk_notebook_set_tab_label_text(nbook, fmcomms6_panel, "FMComms6");

	return 0;
}

#define SYNC_RELOAD "SYNC_RELOAD"

static char *handle_item(struct osc_plugin *plugin, const char *attrib,
			 const char *value)
{
	if (MATCH_ATTRIB(SYNC_RELOAD)) {
		if (value)
			reload_button_clicked(NULL, 0);
		else
			return "1";
	} else {
		if (value) {
			printf("Unhandled tokens in ini file,\n"
				"\tSection %s\n\tAtttribute : %s\n\tValue: %s\n",
				"FMComms6", attrib, value);
			return "FAIL";
		}
	}

	return NULL;
}

static const char *fmcomms6_sr_attribs[] = {
	PLL_DEVICE".RX_LO_frequency",
	SYNC_RELOAD,
	NULL,
};

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void fmcomms6_get_preferred_size(int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void context_destroy(void)
{
	iio_context_destroy(ctx);
}

struct osc_plugin plugin;

static bool fmcomms6_identify(void)
{return true;
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	if (!iio_context_find_device(osc_ctx, ADC_DEVICE)
		|| !iio_context_find_device(osc_ctx, PLL_DEVICE))
		return false;

	ctx = osc_create_context();
	adc = iio_context_find_device(ctx, ADC_DEVICE);
	pll = iio_context_find_device(ctx, PLL_DEVICE);

	if (!adc || !pll)
		iio_context_destroy(ctx);
	return !!adc && !!pll;
}

struct osc_plugin plugin = {
	.name = "FMComms6",
	.identify = fmcomms6_identify,
	.init = fmcomms6_init,
	.save_restore_attribs = fmcomms6_sr_attribs,
	.handle_item = handle_item,
	.update_active_page = update_active_page,
	.get_preferred_size = fmcomms6_get_preferred_size,
	.destroy = context_destroy,
};
