/**
 * Copyright (C) 2012-2014 Analog Devices, Inc.
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

#include "../libini2.h"
#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"

#define THIS_DRIVER "CN0357"

#define ADC_DEVICE "ad7790"
#define DPOT_DEVICE "ad5270_iio"

struct _cn0357_data {
	double adc_conversion;
	double adc_supply;
	double ppm;
	double rdac_resistance;
	double sensor_sensitivity;
	double ppm_mv;
	double mv_ppm;
};

enum feedback_options {
	RHEOSTAT_FEEDBACK,
	FIXED_RESISTOR_FEEDBACK
};

static struct _cn0357_data cn0357_data;

static struct iio_widget adc_iio_widgets[10];
static int num_adc_iio_widgets;

static struct iio_context *ctx;
static struct iio_device *adc, *dpot;
static struct iio_channel *adc_ch, *pwr_ch;

static GtkWidget *update_rates;
static GtkWidget *feedback_type;
static GtkWidget *rdac_val;
static GtkWidget *fixed_res;
static GtkWidget *program_rdac;
static GtkWidget *ppm;
static GtkWidget *adc_voltage;
static GtkWidget *adc_supply;
static GtkWidget *sensor;
static GtkWidget *feedback;
static GtkWidget *ppm_mv_coef;
static GtkWidget *mv_ppm_coef;
static GtkWidget *lbl_rdac;
static GtkWidget *lbl_fixed_res;

static double feedback_resistance;
static double sensor_sensitivity;

static int cn0357_read_status;

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *cn0357_panel;
static gboolean plugin_detached;

#define RDAC_BITS 10
#define RDAC_END_TO_END_RES 20E3

#define V_TO_MV(x) (x * 1000)
#define N_BITS 16

#define V_REF_ADC  1.20
#define GAIN_ADC   1

#define V_REF_PWR 1.17
#define GAIN_PWR (1 / 5.0)

static double ad7790_voltage_conversion(long long raw, double vref, double gain)
{
	return (((raw / pow(2, N_BITS - 1)) - 1) * vref / gain);
}

static int get_adc_voltage(double *out_data)
{
	long long raw;
	int ret;

	ret = iio_channel_attr_read_longlong(adc_ch, "raw", &raw);
	if (!ret)
		*out_data = V_TO_MV(ad7790_voltage_conversion(raw, V_REF_ADC, GAIN_ADC));

	return ret;
}

static int get_adc_power_supply(double *out_data)
{
	long long raw;
	int ret;

	ret = iio_channel_attr_read_longlong(pwr_ch, "raw", &raw);
	if (!ret)
		*out_data = ad7790_voltage_conversion(raw, V_REF_PWR, GAIN_PWR);

	return ret;
}

static void entry_set_double(GtkWidget *entry, double value, int digits)
{
	gchar *s;

	g_return_if_fail(GTK_IS_ENTRY(entry));
	s = g_strdup_printf("%.*f", digits, value);
	gtk_entry_set_text(GTK_ENTRY(entry), s);
	g_free(s);
}

static int cn0357_get_data(struct _cn0357_data *data)
{
	int ret;

	ret = get_adc_voltage(&data->adc_conversion);
	if (ret)
		return ret;
	ret = get_adc_power_supply(&data->adc_supply);
	if (ret)
		return ret;
	data->rdac_resistance = feedback_resistance;
	data->sensor_sensitivity = sensor_sensitivity;
	/* Sensitivity[nA/ppm] x Resistance[Ohm] x E-6 = Coefficient[mV/ppm] */
	data->mv_ppm = (data->sensor_sensitivity * data->rdac_resistance) * 1E-6;
	data->ppm_mv = 1 / data->mv_ppm;
	data->ppm = data->adc_conversion * data->ppm_mv;
	if (data->ppm < 0)
		data->ppm = 0;

	return ret;
}

static void cn0357_update_widgets(struct _cn0357_data *data)
{
	gtk_widget_set_sensitive(ppm, !cn0357_read_status);
	gtk_widget_set_sensitive(adc_voltage, !cn0357_read_status);
	gtk_widget_set_sensitive(adc_supply, !cn0357_read_status);

	if (cn0357_read_status != 0)
		return;

	entry_set_double(ppm, data->ppm, -1);
	entry_set_double(adc_voltage, data->adc_conversion, 2);
	entry_set_double(adc_supply, data->adc_supply, 2);
	entry_set_double(mv_ppm_coef, data->mv_ppm, -1);
	entry_set_double(ppm_mv_coef, data->ppm_mv, -1);
	entry_set_double(feedback, data->rdac_resistance, 2);
}

static void rdac_value_changed_cb(GtkSpinButton *btn, gpointer data)
{
	unsigned raw;
	double resistance;
	int active_fback = gtk_combo_box_get_active(GTK_COMBO_BOX(feedback_type));

	if (active_fback != RHEOSTAT_FEEDBACK)
		return;

	raw = (unsigned)gtk_spin_button_get_value(btn);
	resistance = raw / pow(2, RDAC_BITS) * RDAC_END_TO_END_RES;
	entry_set_double(feedback, resistance, 2);

	feedback_resistance = resistance;
}

static void fixed_resistor_changed_cb(GtkSpinButton *btn, gpointer data)
{
	int active_fback = gtk_combo_box_get_active(GTK_COMBO_BOX(feedback_type));

	if (active_fback == FIXED_RESISTOR_FEEDBACK)
		feedback_resistance = gtk_spin_button_get_value(btn);
}

static void sensor_sensitivity_changed_cb(GtkSpinButton *btn, gpointer data)
{
	sensor_sensitivity = gtk_spin_button_get_value(btn);
}

static void feedback_type_changed_cb(GtkComboBox *box, gpointer data)
{
	bool rheostat_active = (gtk_combo_box_get_active(box) == RHEOSTAT_FEEDBACK);

	if (rheostat_active)
		g_signal_emit_by_name(fixed_res, "value-changed", NULL);
	else
		g_signal_emit_by_name(rdac_val, "value-changed", NULL);

	gtk_widget_set_sensitive(program_rdac, rheostat_active);
	gtk_widget_set_visible(rdac_val, rheostat_active);
	gtk_widget_set_visible(lbl_rdac, rheostat_active);
	gtk_widget_set_visible(fixed_res, !rheostat_active);
	gtk_widget_set_visible(lbl_fixed_res, !rheostat_active);
}

static void program_rdac_clicked_cb(GtkButton *btn, gpointer data)
{
	struct iio_channel *rdac_ch;

	if (!dpot)
		return;

	rdac_ch = iio_device_find_channel(dpot, "voltage0", true);

	if (!rdac_ch)
		return;

	iio_channel_attr_write(rdac_ch, "raw", gtk_entry_get_text(GTK_ENTRY(rdac_val)));
}

static gboolean update_display(gpointer foo)
{
	if (this_page == gtk_notebook_get_current_page(nbook) || plugin_detached) {
		cn0357_read_status = cn0357_get_data(&cn0357_data);
		cn0357_update_widgets(&cn0357_data);
	}

	return TRUE;
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

static GtkWidget* cn0357_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	adc = iio_context_find_device(ctx, ADC_DEVICE);

	if (osc_load_glade_file(builder, "cn0357") < 0)
		return NULL;

	cn0357_panel = GTK_WIDGET(gtk_builder_get_object(builder, "cn0357_panel"));
	update_rates = GTK_WIDGET(gtk_builder_get_object(builder, "comboboxtext_update_rates"));
	feedback_type = GTK_WIDGET(gtk_builder_get_object(builder, "comboboxtext_feedback_type"));
	rdac_val = GTK_WIDGET(gtk_builder_get_object(builder, "spinbutton_rdac_val"));
	fixed_res = GTK_WIDGET(gtk_builder_get_object(builder, "spinbutton_fixed_resistor"));
	program_rdac = GTK_WIDGET(gtk_builder_get_object(builder, "button_program_rdac"));
	ppm = GTK_WIDGET(gtk_builder_get_object(builder, "entry_ppm"));
	adc_voltage = GTK_WIDGET(gtk_builder_get_object(builder, "entry_adc_conversion"));
	adc_supply = GTK_WIDGET(gtk_builder_get_object(builder, "entry_adc_power"));
	sensor = GTK_WIDGET(gtk_builder_get_object(builder, "spinbutton_sensor_sensitivity"));
	feedback = GTK_WIDGET(gtk_builder_get_object(builder, "entry_feedback_resistance"));
	ppm_mv_coef = GTK_WIDGET(gtk_builder_get_object(builder, "entry_ppm_mv"));
	mv_ppm_coef = GTK_WIDGET(gtk_builder_get_object(builder, "entry_mv_ppm"));
	lbl_fixed_res = GTK_WIDGET(gtk_builder_get_object(builder, "label_fixed_resistor"));
	lbl_rdac = GTK_WIDGET(gtk_builder_get_object(builder, "label_rdac_val"));

	dpot = iio_context_find_device(ctx, DPOT_DEVICE);
	adc_ch = iio_device_find_channel(adc, "voltage0-voltage0", false);
	pwr_ch = iio_device_find_channel(adc, "supply", false);

	iio_combo_box_init(&adc_iio_widgets[num_adc_iio_widgets++],
		adc, NULL, "sampling_frequency",
		"sampling_frequency_available",
		update_rates, NULL);

	g_signal_connect(rdac_val, "value-changed",
		G_CALLBACK(rdac_value_changed_cb), NULL);
	g_signal_connect(fixed_res, "value-changed",
		G_CALLBACK(fixed_resistor_changed_cb), NULL);
	g_signal_connect(sensor, "value-changed",
		G_CALLBACK(sensor_sensitivity_changed_cb), NULL);
	g_signal_connect(feedback_type, "changed",
		G_CALLBACK(feedback_type_changed_cb), NULL);
	g_signal_connect(program_rdac, "clicked",
		G_CALLBACK(program_rdac_clicked_cb), NULL);

	make_widget_update_signal_based(adc_iio_widgets, num_adc_iio_widgets);
	iio_update_widgets(adc_iio_widgets, num_adc_iio_widgets);

	gtk_combo_box_set_active(GTK_COMBO_BOX(feedback_type), RHEOSTAT_FEEDBACK);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(rdac_val), 307);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(sensor), 65);
	program_rdac_clicked_cb(GTK_BUTTON(program_rdac), NULL);

	g_timeout_add(1000, (GSourceFunc) update_display, ctx);

	return cn0357_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void cn0357_get_preferred_size(const struct osc_plugin *plugin, int *width, int *height)
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

static bool cn0357_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	return !!iio_context_find_device(osc_ctx, ADC_DEVICE);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = cn0357_identify,
	.init = cn0357_init,
	.update_active_page = update_active_page,
	.get_preferred_size = cn0357_get_preferred_size,
	.destroy = context_destroy,
};
