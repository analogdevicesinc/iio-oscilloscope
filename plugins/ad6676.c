/**
 * Copyright (C) 2012-2015 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include "../osc.h"
#include "../iio_widget.h"
#include "../libini2.h"
#include "../osc_plugin.h"
#include "../config.h"

#define THIS_DRIVER "AD6676"
#define IIO_DEVICE "axi-ad6676-hpc"
#define IIO_CHANNEL0 "voltage0"
#define IIO_CHANNEL1 "voltage1"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

static const gdouble mhz_scale = 1000000.0;

static struct iio_widget widgets[25];
static struct iio_context *ctx;
static struct iio_device *dev;

static unsigned int num_w;
static unsigned int interm_freq;

static GtkWidget *spin_adc_freq;
static GtkWidget *spin_interm_freq;
static GtkAdjustment *adj_bandwidth;

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *ad6676_panel;
static gboolean plugin_detached;

static const char *ad6676_sr_attribs[] = {
	IIO_DEVICE".in_voltage_adc_frequency",
	IIO_DEVICE".in_voltage_bandwidth",
	IIO_DEVICE".in_voltage_bw_margin_high",
	IIO_DEVICE".in_voltage_bw_margin_if",
	IIO_DEVICE".in_voltage_bw_margin_low",
	IIO_DEVICE".in_voltage_hardwaregain",
	IIO_DEVICE".in_voltage_intermediate_frequency",
	IIO_DEVICE".in_voltage_sampling_frequency",
	IIO_DEVICE".in_voltage_scale",
	IIO_DEVICE".in_voltage_shuffler_control",
	IIO_DEVICE".in_voltage_shuffler_thresh",
	IIO_DEVICE".in_voltage_stest_mode",
};

static double db_full_scale_convert(double value, bool inverse)
{
	if (inverse) {
		if (value == 0)
			return DBL_MAX;
		return (int)((20 * log10(1 / value)) + 0.5);
	} else {
		return pow(10, -value / 20.0);
	}
}

static void adc_freq_val_changed_cb (GtkSpinButton *button, gpointer data)
{
	double freq;

	freq = gtk_spin_button_get_value(button);
	gtk_adjustment_set_lower(adj_bandwidth, 0.005 * freq);
	gtk_adjustment_set_upper(adj_bandwidth, 0.05 * freq);
}

static void rx_freq_info_update(void)
{
	double lo_freq;

	rx_update_device_sampling_freq(IIO_DEVICE,
		USE_INTERN_SAMPLING_FREQ);
	lo_freq = mhz_scale * gtk_spin_button_get_value(
				GTK_SPIN_BUTTON(spin_interm_freq));
	rx_update_channel_lo_freq(IIO_DEVICE, "all", lo_freq);
}

static void reload_button_clicked(GtkButton *btn, gpointer data)
{
	iio_update_widgets_of_device(widgets, num_w, dev);
	rx_freq_info_update();
}

static void rx_update_labels_on_complete(void *data)
{
	rx_freq_info_update();
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
	update_from_ini(ini_fn, THIS_DRIVER, dev, ad6676_sr_attribs,
			ARRAY_SIZE(ad6676_sr_attribs));

	reload_button_clicked(NULL, NULL);
}

static GtkWidget * ad6676_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	struct iio_channel *ch;

	ctx = osc_create_context();
	if (!ctx)
		goto init_abort;

	dev = iio_context_find_device(ctx, IIO_DEVICE);
	if (!dev) {
		printf("Error: Could not find %s device\n", IIO_DEVICE);
		goto init_abort;
	}

	ch = iio_device_find_channel(dev, IIO_CHANNEL0, false);
	if (!ch) {
		printf("Error: Could not find %s channel\n", IIO_CHANNEL0);
		goto init_abort;
	}

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (osc_load_glade_file(builder, "ad6676") < 0)
		return NULL;

	ad6676_panel = GTK_WIDGET(gtk_builder_get_object(builder, "ad6676_panel"));
	spin_adc_freq = GTK_WIDGET(gtk_builder_get_object(builder, "spin_adc_freq"));
	spin_interm_freq = GTK_WIDGET(gtk_builder_get_object(builder, "spin_intermediate_freq"));
	adj_bandwidth = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adj_bandwidth"));

	if (ini_fn)
		load_profile(NULL, ini_fn);

	/* Bind the IIO device files to the GUI widgets */

	iio_spin_button_int_init_from_builder(&widgets[num_w++], dev, ch,
		"adc_frequency", builder, "spin_adc_freq", &mhz_scale);
	iio_spin_button_add_progress(&widgets[num_w - 1]);
	iio_spin_button_int_init_from_builder(&widgets[num_w++], dev, ch,
		"bandwidth", builder, "spin_bandwidth", &mhz_scale);
	iio_spin_button_add_progress(&widgets[num_w - 1]);
	iio_spin_button_int_init_from_builder(&widgets[num_w++], dev, ch,
		"bw_margin_low", builder, "spin_margin_low", NULL);
	iio_spin_button_add_progress(&widgets[num_w - 1]);
	iio_spin_button_int_init_from_builder(&widgets[num_w++], dev, ch,
		"bw_margin_high", builder, "spin_margin_high", NULL);
	iio_spin_button_add_progress(&widgets[num_w - 1]);
	iio_spin_button_int_init_from_builder(&widgets[num_w++], dev, ch,
		"bw_margin_if", builder, "spin_margin_if", NULL);
	iio_spin_button_add_progress(&widgets[num_w - 1]);
	interm_freq = num_w;
	iio_spin_button_int_init_from_builder(&widgets[num_w++], dev, ch,
		"intermediate_frequency", builder, "spin_intermediate_freq", &mhz_scale);
	iio_spin_button_add_progress(&widgets[num_w - 1]);
	iio_spin_button_int_init_from_builder(&widgets[num_w++], dev, ch,
		"sampling_frequency", builder, "spin_sampl_freq", &mhz_scale);
	iio_spin_button_add_progress(&widgets[num_w - 1]);
	iio_spin_button_init_from_builder(&widgets[num_w++], dev, ch,
		"hardwaregain", builder, "spin_hardwaregain", NULL);
	iio_spin_button_init_from_builder(&widgets[num_w++], dev, ch,
		"scale", builder, "spin_scale", NULL);
	iio_spin_button_set_convert_function(&widgets[num_w - 1], db_full_scale_convert);
	iio_spin_button_add_progress(&widgets[num_w - 1]);
	iio_combo_box_init_from_builder(&widgets[num_w++], dev, ch,
		"shuffler_control", "shuffler_control_available",
		builder, "cmb_shuffler_control", NULL);
	iio_combo_box_init_from_builder(&widgets[num_w++], dev, ch,
		"shuffler_thresh", "shuffler_thresh_available",
		builder, "cbm_shuffler_thresh", NULL);
	iio_combo_box_init_from_builder(&widgets[num_w++], dev, ch,
		"test_mode", "test_mode_available", builder, "cmb_test_modes", NULL);

	/* Update all widgets with current values */
	iio_update_widgets_of_device(widgets, num_w, dev);
	rx_freq_info_update();

	/* Connect signals */

	g_builder_connect_signal(builder, "ad6676_settings_reload", "clicked",
		G_CALLBACK(reload_button_clicked), NULL);
	g_builder_connect_signal(builder, "spin_adc_freq", "value-changed",
		G_CALLBACK(adc_freq_val_changed_cb), NULL);

	make_widget_update_signal_based(widgets, num_w);
	iio_spin_button_set_on_complete_function(&widgets[interm_freq],
		rx_update_labels_on_complete, NULL);

	/* Other plugin initializations */
	adc_freq_val_changed_cb(GTK_SPIN_BUTTON(spin_adc_freq), NULL);

	return ad6676_panel;

init_abort:
	if (ctx)
		osc_destroy_context(ctx);

	return NULL;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void ad6676_get_preferred_size(const struct osc_plugin *plugin, int *width, int *height)
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
		save_to_ini(f, THIS_DRIVER, dev, ad6676_sr_attribs,
				ARRAY_SIZE(ad6676_sr_attribs));
		fclose(f);
	}
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	if (ini_fn)
		save_profile(NULL, ini_fn);

	osc_destroy_context(ctx);
}

struct osc_plugin plugin;

static bool ad6676_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	return !!iio_context_find_device(osc_ctx, IIO_DEVICE);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = ad6676_identify,
	.init = ad6676_init,
	.update_active_page = update_active_page,
	.get_preferred_size = ad6676_get_preferred_size,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
