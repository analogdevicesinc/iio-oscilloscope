/**
 * Copyright (C) 2019 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <iio.h>

#include "../config.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../datatypes.h"

#define THIS_DRIVER "LIDAR"

#define PULSE_CAPTURE_DEV "axi-pulse-capture"
#define AFE_DEVICE "ad5627"
#define CAP_DEVICE "axi-ad9094-hpc"

static struct iio_context *ctx;
static struct iio_device *pulse_dev;
static struct iio_device *afe_dev;
static struct iio_channel *pulse_ch0;
static struct iio_channel *afe_ch0;
static struct iio_channel *afe_ch1;

static struct iio_widget widgets[50];
static unsigned int num_widgets = 0;

static const gdouble khz_scale = 1000.0;
static const gdouble inv_scale = -1.0;

static int auto_cfg_prev_values[4];

static GtkSpinButton *manual_ch_btns[4];
static GtkSpinButton *auto_cfg_btns[4];

static gint this_page;
static gboolean plugin_detached;

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

static void manual_ch_set_cb(void)
{
	const int len = 9; // 4 digits, 4 spaces and NUL character
	char ch_config[len];

	snprintf(ch_config, len, "%i %i %i %i",
		gtk_spin_button_get_value_as_int(manual_ch_btns[0]),
		gtk_spin_button_get_value_as_int(manual_ch_btns[1]),
		gtk_spin_button_get_value_as_int(manual_ch_btns[2]),
		gtk_spin_button_get_value_as_int(manual_ch_btns[3]));

	iio_device_attr_write_raw(pulse_dev,
		"sequencer_manual_chsel", ch_config, len);
}

static void auto_cfg_set_cb(void)
{
	const int len = 9; // 4 digits, 4 spaces and NUL character
	char ch_config[len];

	snprintf(ch_config, len, "%i %i %i %i",
		gtk_spin_button_get_value_as_int(auto_cfg_btns[0]),
		gtk_spin_button_get_value_as_int(auto_cfg_btns[1]),
		gtk_spin_button_get_value_as_int(auto_cfg_btns[2]),
		gtk_spin_button_get_value_as_int(auto_cfg_btns[3]));

	iio_device_attr_write_raw(pulse_dev,
		"sequencer_auto_cfg", ch_config, len);
}

static void auto_cfg_button_changed_cb(GtkSpinButton *btn)
{
	// Force the buttons value to have a valid permutation of (0, 1, 2, 3)
	int i, j, idx;
	int old_val;
	int crt_val = gtk_spin_button_get_value_as_int(btn);

	// Find the index for the button that this callback has been called (current button)
	for (i = 0; i < 4; i++) {
		if (btn == auto_cfg_btns[i])
			break;
	}
	if (i == 4) {
		fprintf(stderr, "Could not find the current button."
			"func: %s\n", __func__);
		return;
	}

	old_val = auto_cfg_prev_values[i];

	// Find the other button with the same value as current button
	for (j = i + 1; j < i + 4; j++) {
		idx = j % 4;
		if (gtk_spin_button_get_value_as_int(auto_cfg_btns[idx]) == crt_val) {
			break;
		}
	}

	// Force the other button to take the old value of the current button. Avoid triggering a new callback
	g_signal_handlers_block_by_func(auto_cfg_btns[idx],
		G_CALLBACK(auto_cfg_button_changed_cb), NULL);
	gtk_spin_button_set_value(auto_cfg_btns[idx], old_val);
	g_signal_handlers_unblock_by_func(auto_cfg_btns[idx],
		G_CALLBACK(auto_cfg_button_changed_cb), NULL);

	// Update previuos values
	auto_cfg_prev_values[i] = crt_val;
	auto_cfg_prev_values[idx] = old_val;
}

static void pulse_freq_changed_cb(GtkSpinButton *btn, GtkAdjustment *pulse_delay_adj)
{
	gdouble freq = gtk_spin_button_get_value(btn);
	gdouble delay_upper_ns = 1e6 / freq; // freq is in kHz, we need ns
	gdouble delay_ns = gtk_adjustment_get_value(pulse_delay_adj);
	
	gtk_adjustment_set_upper(pulse_delay_adj, delay_upper_ns);
	if (delay_ns > delay_upper_ns)
		gtk_adjustment_set_value(pulse_delay_adj, delay_upper_ns);
}

static double adp_bias_volts_to_raw_convert(double value, bool inverse)
{
	double ret;

	if (inverse) {
		ret = -((value * 5 * 18.18) / 4096) + 122;
	} else {
		ret = ((-122 - value) * 4096) / (5 * 18.18);
	}

	return ret;
}

static double tilt_volts_to_raw_convert(double value, bool inverse)
{
	double ret;

	if (inverse) {
		ret = (value * 5) / 4096;
	} else {
		ret = (value * 4096) / 5;
	}

	return ret;
}

static void set_all_iio_atributes_to_default_values()
{
	iio_device_attr_write_bool(pulse_dev, "sequencer_en", false);
	iio_device_attr_write_raw(pulse_dev, "sequencer_mode", "auto", 5);
	iio_device_attr_write_raw(pulse_dev,
		"sequencer_manual_chsel", "0, 0, 0, 0", 9);
	iio_device_attr_write_raw(pulse_dev,
		"sequencer_auto_cfg", "0, 1, 2, 3", 9);
	iio_device_attr_write_longlong(pulse_dev, "sequencer_pulse_delay_ns", 248);
	iio_channel_attr_write_bool(pulse_ch0, "en", false);
	iio_channel_attr_write_longlong(pulse_ch0, "frequency", 50000);
	iio_channel_attr_write_longlong(pulse_ch0, "pulse_width_ns", 20);
	iio_channel_attr_write_longlong(afe_ch0, "raw",
		adp_bias_volts_to_raw_convert(-160, false));
	iio_channel_attr_write_longlong(afe_ch1, "raw",
		tilt_volts_to_raw_convert(0, false));
}

static GtkWidget * lidar_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *lidar_panel;
	int i;

	ctx = osc_create_context();
	if (!ctx) {
		fprintf(stderr, "Could not create context in %s\n", __func__);
		return NULL;
	}

	pulse_dev = iio_context_find_device(ctx, PULSE_CAPTURE_DEV);
	if (!pulse_dev) {
		fprintf(stderr, "Could not find device: %s in %s\n",
			PULSE_CAPTURE_DEV, __func__);
		return NULL;
	}
	afe_dev = iio_context_find_device(ctx, AFE_DEVICE);
	if (!afe_dev) {
		fprintf(stderr, "Could not find device: %s in %s\n",
			AFE_DEVICE, __func__);
		return NULL;
	}

	pulse_ch0 = iio_device_find_channel(pulse_dev, "altvoltage0", true);
	if (!pulse_ch0) {
		fprintf(stderr, "Could not find channel: %s of device %s in %s\n",
			"voltage0", PULSE_CAPTURE_DEV, __func__);
		return NULL;
	}

	afe_ch0 = iio_device_find_channel(afe_dev, "voltage0", true);
	if (!afe_ch0) {
		fprintf(stderr, "Could not find channel: %s of device %s in %s\n",
			"voltage0", AFE_DEVICE, __func__);
		return NULL;
	}

	afe_ch1 = iio_device_find_channel(afe_dev, "voltage1", true);
	if (!afe_ch1) {
		fprintf(stderr, "Could not find channel: %s of device %s in %s\n",
			"voltage1", AFE_DEVICE, __func__);
		return NULL;
	}

	set_all_iio_atributes_to_default_values();

	builder = gtk_builder_new();
	if (osc_load_glade_file(builder, "lidar") < 0)
		return NULL;

	if (!gtk_builder_add_from_file(builder, "./glade/lidar.glade", NULL)) {
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "lidar.glade", NULL);
	} else {
		GtkImage *laser;

		/* We are running locally, so load the local files */
		laser = GTK_IMAGE(gtk_builder_get_object(builder, "img_laser"));
		g_object_set(laser, "file","./icons/laser_symbol.png", NULL);
	}

	lidar_panel = GTK_WIDGET(gtk_builder_get_object(builder, "lidar_panel"));
	manual_ch_btns[0] = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "spin_manual_ch0"));
	manual_ch_btns[1] = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "spin_manual_ch1"));
	manual_ch_btns[2] = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "spin_manual_ch2"));
	manual_ch_btns[3] = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "spin_manual_ch3"));

	auto_cfg_btns[0] = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "spin_auto_cfg0"));
	auto_cfg_btns[1] = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "spin_auto_cfg1"));
	auto_cfg_btns[2] = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "spin_auto_cfg2"));
	auto_cfg_btns[3] = GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "spin_auto_cfg3"));

	for (i = 0; i < 4; i++)
		auto_cfg_prev_values[i] = gtk_spin_button_get_value_as_int(auto_cfg_btns[i]);

	/* Bind the IIO device files to the GUI widgets */

	iio_toggle_button_init_from_builder(&widgets[num_widgets++], pulse_dev,
		NULL, "sequencer_en", builder, "check_en_sequencer", false);

	iio_combo_box_init_from_builder(&widgets[num_widgets++], pulse_dev,
		NULL, "sequencer_mode", "sequencer_mode_available", builder, "cmb_mode", NULL);

	iio_spin_button_int_init_from_builder(&widgets[num_widgets++], pulse_dev,
		NULL, "sequencer_pulse_delay_ns", builder, "spin_pulse_delay", NULL);
	iio_spin_button_add_progress(&widgets[num_widgets - 1]);

	iio_toggle_button_init_from_builder(&widgets[num_widgets++], pulse_dev,
		pulse_ch0, "en", builder, "check_en_generator", false);
		
	iio_spin_button_int_init_from_builder(&widgets[num_widgets++], pulse_dev,
		pulse_ch0, "frequency", builder, "spin_pulse_freq", &khz_scale);
	
	iio_spin_button_int_init_from_builder(&widgets[num_widgets++], pulse_dev,
		pulse_ch0, "pulse_width_ns", builder, "spin_pulse_width", NULL);

	iio_spin_button_int_init_from_builder(&widgets[num_widgets++], afe_dev,
		afe_ch0, "raw", builder, "spin_adp_bias", &inv_scale);
	iio_spin_button_set_convert_function(&widgets[num_widgets - 1], adp_bias_volts_to_raw_convert);

	iio_spin_button_int_init_from_builder(&widgets[num_widgets++], afe_dev,
		afe_ch1, "raw", builder, "spin_tilt", &inv_scale);
	iio_spin_button_set_convert_function(&widgets[num_widgets - 1], tilt_volts_to_raw_convert);

	for (i = 0; i < 4; i++)
		g_signal_connect(auto_cfg_btns[i],
			"value-changed", G_CALLBACK(auto_cfg_button_changed_cb), NULL);
	
	g_builder_connect_signal(builder, "btn_set_auto_cfg", "clicked",
		G_CALLBACK(auto_cfg_set_cb), NULL);
	
	g_builder_connect_signal(builder, "btn_set_manual_ch", "clicked",
		G_CALLBACK(manual_ch_set_cb), NULL);

	g_builder_connect_signal(builder, "spin_pulse_freq", "value-changed",
		G_CALLBACK(pulse_freq_changed_cb),
		GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adj_pulse_delay")));
	
	make_widget_update_signal_based(widgets, num_widgets);

	iio_update_widgets(widgets, num_widgets);

	/* Information to be passed to the osc */
	struct iio_device *adc_dev;
	struct iio_channel *adc_ch4;

	adc_dev = iio_context_find_device(get_context_from_osc(), CAP_DEVICE);
	if (!adc_dev) {
		fprintf(stderr, "device %s not found."
				"This will cause limited functionality.", CAP_DEVICE);
		return lidar_panel;
	}

	adc_ch4 = iio_device_find_channel(adc_dev, "voltage4", false);
	if (adc_ch4) {
		struct extra_info *ch_info = iio_channel_get_data(adc_ch4);
		if (ch_info) {
			ch_info->constraints = CONSTR_CHN_INITIAL_ENABLED | CONSTR_CHN_UNTOGGLEABLE;
		} else {
			fprintf(stderr, "no extra device info for device %s found."
				"This will cause limited functionality.", CAP_DEVICE);
		}
	} else {
		fprintf(stderr, "%s plugin couldn't find device %s."
			"This will cause limited functionality", THIS_DRIVER, CAP_DEVICE);
	}

	return lidar_panel;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	set_all_iio_atributes_to_default_values();
	
	osc_destroy_context(ctx);
}

static void update_active_page(struct osc_plugin *plugin, gint active_page,
			       gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void lidar_get_preferred_size(const struct osc_plugin *plugin,
				      int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static OscPreferences* lidar_get_preferences_for_osc(const struct osc_plugin *plugin)
{
	OscPreferences *pref = osc_preferences_new();

	pref->plot_preferences = osc_plot_preferences_new();
	pref->plot_preferences->sample_count = g_new(unsigned int, 1);
	*pref->plot_preferences->sample_count = 1024;

	/* TO DO: add here the preferences about channel voltage4 */

	return pref;
}

static bool lidar_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();
	return !!iio_context_find_device(osc_ctx, PULSE_CAPTURE_DEV) &&
	 	!!iio_context_find_device(osc_ctx, AFE_DEVICE);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = lidar_identify,
	.init = lidar_init,
	.update_active_page = update_active_page,
	.get_preferred_size = lidar_get_preferred_size,
	.get_preferences_for_osc = lidar_get_preferences_for_osc,
	.destroy = context_destroy,
};
