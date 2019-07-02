/**
 * Copyright (C) 2019 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <glib.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"

#define THIS_DRIVER "CN0508"

#define ADC_DEVICE "ad7124-4"
#define DAC_DEVICE "ad5683r"

#define N_BITS 24

#define V_REF_ADC  2.5
#define GAIN_ADC   1

struct _cn0508_data {
	double adc_conversion;
	double dac_output;
	double output_current;
	double output_voltage;
	double u2_temp;
	double u3_temp;

	double pot_voltage;
	double pot_current;

	double input_voltage;
};

static struct _cn0508_data cn0508_data;

static struct iio_context *ctx;
static struct iio_device *adc, *dac;
static struct iio_channel *u2_temp_ch, *u3_temp_ch, *out_current_ch;
static struct iio_channel *in_v_attenuator_ch, *out_v_attenuator_ch,
	       *current_pot_pos_ch, *voltage_pot_pos_ch;
static struct iio_channel *dac_ch;

static struct iio_widget iio_widgets[25];
static unsigned int num_widgets;

static GtkWidget *u2_temp_monitor;
static GtkWidget *u3_temp_monitor;
static GtkWidget *dc_supply_voltage;
static GtkWidget *dc_supply_current;
static GtkWidget *pot_voltage;
static GtkWidget *pot_current;
static GtkWidget *in_voltage;

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *cn0508_panel;
static gboolean plugin_detached;

static void entry_set_double(GtkWidget *entry, double value, int digits)
{
	gchar *s;

	g_return_if_fail(GTK_IS_ENTRY(entry));
	s = g_strdup_printf("%.*f", digits, value);
	gtk_entry_set_text(GTK_ENTRY(entry), s);
	g_free(s);
}

static double ad7124_voltage_conversion(long long raw, double vref, double gain)
{
	return ((raw / pow(2, N_BITS)) * vref / gain);
}

static int get_adc_voltage(struct iio_channel *adc_ch, double *out_data)
{
	long long raw;
	int ret;

	ret = iio_channel_attr_read_longlong(adc_ch, "raw", &raw);
	if (!ret)
		*out_data = ad7124_voltage_conversion(raw, V_REF_ADC, GAIN_ADC);

	return ret;
}

static void cn0508_update_widgets(struct _cn0508_data *data)
{
	gchar text[10];

	entry_set_double(u2_temp_monitor, data->u2_temp, 2);
	entry_set_double(u3_temp_monitor, data->u3_temp, 2);
	entry_set_double(dc_supply_current, data->output_current, 2);
	entry_set_double(dc_supply_voltage, data->output_voltage, 2);
	entry_set_double(in_voltage, data->input_voltage, 2);

	snprintf(text, 5, "%4.2f", data->pot_voltage * 100);
	strcat(text, "%");
	gtk_progress_bar_set_fraction ((GtkProgressBar *)pot_voltage,
				       data->pot_voltage);
	gtk_progress_bar_set_text ((GtkProgressBar *)pot_voltage, text);

	snprintf(text, 5, "%4.2f", data->pot_current * 100);
	strcat(text, "%");
	gtk_progress_bar_set_fraction ((GtkProgressBar *)pot_current,
				       data->pot_current);
	gtk_progress_bar_set_text ((GtkProgressBar *)pot_current, text);
}

static int cn0508_get_data(struct _cn0508_data *data)
{
	int ret;

	ret = get_adc_voltage(u2_temp_ch, &data->adc_conversion);
	if (ret)
		return ret;
	data->u2_temp = data->adc_conversion * 1000; /* 1mV/ºC */

	ret = get_adc_voltage(u3_temp_ch, &data->adc_conversion);
	if (ret)
		return ret;
	data->u3_temp = data->adc_conversion * 1000; /* 1mV/ºC */

	ret = get_adc_voltage(out_current_ch, &data->adc_conversion);
	if (ret)
		return ret;
	data->output_current = data->adc_conversion / 0.2; /* 200mV/A */

	ret = get_adc_voltage(voltage_pot_pos_ch, &data->adc_conversion);
	if (ret)
		return ret;
	data->pot_voltage = data->adc_conversion / V_REF_ADC; /* 0% = 0V, 100% = 2.5V */

	ret = get_adc_voltage(current_pot_pos_ch, &data->adc_conversion);
	if (ret)
		return ret;
	data->pot_current = data->adc_conversion / V_REF_ADC; /* 0% = 0V, 100% = 2.5V */

	ret = get_adc_voltage(out_v_attenuator_ch, &data->adc_conversion);
	if (ret)
		return ret;
	data->output_voltage = data->adc_conversion * 10.52;

	ret = get_adc_voltage(in_v_attenuator_ch, &data->adc_conversion);
	if (ret)
		return ret;
	data->input_voltage = data->adc_conversion * 14.33;

	return ret;
}

static void update_values(void)
{
	iio_update_widgets(iio_widgets, num_widgets);
}

static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	iio_w->save(iio_w);
}

static gboolean update_display(gpointer foo)
{
	int ret = FALSE;

	if (this_page == gtk_notebook_get_current_page(nbook) || plugin_detached) {
		ret = cn0508_get_data(&cn0508_data);
		cn0508_update_widgets(&cn0508_data);
	}
	if (ret)
		return ret;

	return TRUE;
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
			g_signal_connect(G_OBJECT(widgets[i].widget), signal_name,
					 G_CALLBACK(save_widget_value), &widgets[i]);
		}
	}
}

static GtkWidget* cn0508_init(struct osc_plugin *plugin, GtkWidget *notebook,
			      const char *ini_fn)
{
	GtkBuilder *builder;
	double val;

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	adc = iio_context_find_device(ctx, ADC_DEVICE);
	dac = iio_context_find_device(ctx, DAC_DEVICE);

	if (osc_load_glade_file(builder, "cn0508") < 0)
		return NULL;

	cn0508_panel = GTK_WIDGET(gtk_builder_get_object(builder, "cn0508_panel"));
	u2_temp_monitor = GTK_WIDGET(gtk_builder_get_object(builder, "entry_u2_temp"));
	u3_temp_monitor = GTK_WIDGET(gtk_builder_get_object(builder, "entry_u3_temp"));
	dc_supply_voltage = GTK_WIDGET(gtk_builder_get_object(builder,
				       "entry_voltage"));
	dc_supply_current = GTK_WIDGET(gtk_builder_get_object(builder,
				       "entry_current"));
	pot_voltage = GTK_WIDGET(gtk_builder_get_object(builder,
				 "progressbar_voltage"));
	pot_current = GTK_WIDGET(gtk_builder_get_object(builder,
				 "progressbar_current"));
	in_voltage = GTK_WIDGET(gtk_builder_get_object(builder, "entry_in_voltage"));

	u2_temp_ch = iio_device_find_channel(adc, "voltage0-voltage19", false);
	u3_temp_ch = iio_device_find_channel(adc, "voltage1-voltage19", false);
	out_current_ch = iio_device_find_channel(adc, "voltage2-voltage19", false);
	in_v_attenuator_ch = iio_device_find_channel(adc, "voltage3-voltage19", false);
	out_v_attenuator_ch = iio_device_find_channel(adc, "voltage4-voltage19", false);

	current_pot_pos_ch = iio_device_find_channel(adc, "voltage5-voltage19", false);
	voltage_pot_pos_ch = iio_device_find_channel(adc, "voltage6-voltage19", false);

	dac_ch = iio_device_find_channel(dac, "voltage0", true);

	iio_channel_attr_read_double(dac_ch, "scale", &val);

	iio_spin_button_int_init_from_builder(&iio_widgets[num_widgets++], dac,
					      dac_ch, "raw", builder,
					      "spinbutton_dac_voltage", NULL);

	make_widget_update_signal_based(iio_widgets, num_widgets);
	update_values();

	g_timeout_add(1000, (GSourceFunc) update_display, ctx);

	return cn0508_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page,
			       gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void cn0508_get_preferred_size(const struct osc_plugin *plugin,
				      int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	g_source_remove_by_user_data(ctx);
	osc_destroy_context(ctx);
}

struct osc_plugin plugin;

static bool cn0508_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	return !!iio_context_find_device(osc_ctx, DAC_DEVICE) &&
		!!iio_context_find_device(osc_ctx, ADC_DEVICE);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = cn0508_identify,
	.init = cn0508_init,
	.update_active_page = update_active_page,
	.get_preferred_size = cn0508_get_preferred_size,
	.destroy = context_destroy,
};
