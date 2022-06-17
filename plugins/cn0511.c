/**
 * Copyright (C) 2019 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <errno.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <math.h>

#include <ad9166.h>
#include <iio.h>

#include "../osc.h"
#include "../osc_plugin.h"
#include "../iio_widget.h"

#define THIS_DRIVER	"CN0511"
#define DAC_DEVICE	"ad9166"
#define DAC_AMPLIFIER	"ad9166-amp"
/*
 * For now leave this define as it is but, in future it would be nice if
 * the scaling is done in the driver so that, the plug in does not have
 * to knwow about  DAC_MAX_AMPLITUDE.
 */
#define DAC_MAX_AMPLITUDE	32767.0
#define SCALE_MINUS_INFINITE	-91.0
#define SCALE_MAX		0

static struct iio_context *ctx;
struct iio_channel *dac_ch;
struct iio_device *dac;

static struct iio_widget iio_widgets[25];
static unsigned int num_widgets;

static GtkWidget *cn0511_panel;
static GtkWidget *calib_frequency;
static GtkWidget *calib_amplitude;
static GtkButton *calib_en;

static gboolean plugin_detached;
static gint this_page;
static int amplitude = 0;
static double calib_freq = 4500000000;

const gdouble mhz_scale = 1000000.0;

static void save_calib_freq(GtkSpinButton *btn, gpointer data)
{
	calib_freq = gtk_spin_button_get_value(btn) * mhz_scale;
}

static void save_calib_ampl(GtkSpinButton *btn, gpointer data)
{
	amplitude = gtk_spin_button_get_value(btn);
}

static void save_calib_config(GtkSpinButton *btn, gpointer data)
{
	struct ad9166_calibration_data *calib_data;

	ad9166_context_find_calibration_data(ctx, "cn0511", &calib_data);

	ad9166_channel_set_freq(dac_ch, calib_freq);

	ad9166_device_set_amplitude(dac, amplitude);

	ad9166_device_set_iofs(dac, calib_data, calib_freq);
}

static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	iio_w->save(iio_w);
	/* refresh widgets so that, we know if our value was updated */
	iio_update_widgets(iio_widgets, num_widgets);
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
			printf("unhandled widget type, attribute: %s\n",
			       widgets[i].attr_name);

		if (GTK_IS_SPIN_BUTTON(widgets[i].widget) &&
		    widgets[i].priv_progress != NULL) {
			iio_spin_button_progress_activate(&widgets[i]);
		} else {
			g_signal_connect(G_OBJECT(widgets[i].widget),
					 signal_name,
					 G_CALLBACK(save_widget_value),
					 &widgets[i]);
		}
	}
}

static GtkWidget *cn0511_init(struct osc_plugin *plugin, GtkWidget *notebook,
			      const char *ini_fn)
{
	GtkBuilder *builder;
	struct iio_device *dac_amp;
	int ret;

	builder = gtk_builder_new();

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	if (osc_load_glade_file(builder, "cn0511") < 0) {
		osc_destroy_context(ctx);
		return NULL;
	}

	dac = iio_context_find_device(ctx, DAC_DEVICE);
	dac_amp = iio_context_find_device(ctx, DAC_AMPLIFIER);

	if (!dac || !dac_amp) {
		printf("Could not find expected iio devices\n");
		osc_destroy_context(ctx);
		return NULL;
	}

	cn0511_panel = GTK_WIDGET(gtk_builder_get_object(builder,
							 "cn0511_panel"));
	calib_frequency = GTK_WIDGET(gtk_builder_get_object(builder,
						"spinbutton_calib_freq"));
	calib_amplitude = GTK_WIDGET(gtk_builder_get_object(builder,
						"spinbutton_amplitude"));
	calib_en = GTK_BUTTON(gtk_builder_get_object(builder,
						"calib_btn"));

	dac_ch = iio_device_find_channel(dac, "altvoltage0", true);

	ret = iio_device_attr_write_longlong(dac, "fir85_enable", 1);
	if (ret < 0) {
		fprintf(stderr, "Failed to enable FIR85. Error: %d\n", ret);
	}

	ret = iio_device_attr_write_longlong(dac, "sampling_frequency", 6000000000);
	if (ret < 0) {
		fprintf(stderr, "Failed to set sampling frequency. Error: %d\n", ret);
	}

	iio_toggle_button_init_from_builder(&iio_widgets[num_widgets++],
					    dac_amp, NULL, "en", builder,
					    "dac_amplifier_enable", 0);


	make_widget_update_signal_based(iio_widgets, num_widgets);
	iio_update_widgets(iio_widgets, num_widgets);

	g_signal_connect(G_OBJECT(calib_frequency), "value-changed",
			 G_CALLBACK(save_calib_freq), NULL);

	g_signal_connect(G_OBJECT(calib_amplitude), "value-changed",
			 G_CALLBACK(save_calib_ampl), NULL);

	g_signal_connect(G_OBJECT(calib_en), "clicked",
			 G_CALLBACK(save_calib_config), NULL);


	return cn0511_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page,
			       gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void cn0511_get_preferred_size(const struct osc_plugin *plugin,
				      int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	osc_destroy_context(ctx);
}

struct osc_plugin plugin;

static bool cn0511_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	return !!iio_context_find_device(osc_ctx, DAC_DEVICE) &&
		!!iio_context_find_device(osc_ctx, DAC_AMPLIFIER);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = cn0511_identify,
	.init = cn0511_init,
	.update_active_page = update_active_page,
	.get_preferred_size = cn0511_get_preferred_size,
	.destroy = context_destroy,
};
