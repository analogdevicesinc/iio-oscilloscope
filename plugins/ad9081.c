/**
 * AD9081 (MxFE) Plugin
 *
 * Copyright (C) 2019 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "../osc.h"
#include "../osc_plugin.h"
#include "../iio_widget.h"
#include "../iio_utils.h"
#include "../config.h"
#include "dac_data_manager.h"

#define THIS_DRIVER			"AD9081"
#define AD9081				"axi-ad9081-rx"
#define DAC_DEVICE			"axi-ad9081-tx"
#define NUM_MAX_CHANNEL			8
#define AD9081_MAX_ADC_FREQ_HZ		6000000000LL /* AD9081 4000MHz, AD9082 6000MHz */
#define AD9081_MAX_DAC_FREQ_HZ		12000000000LL
/*
 * 81 = 2 widgets per maximmum of 8 rx channels + 8 widgets per maximun 8 tx
 * channels + 1 combo box for global RX configuration...
 */
#define NUM_MAX_WIDGETS			163

const gdouble mhz_scale = 1000000.0;
const gdouble k_scale = 1000.0;

#define dialog_box_message(widget, title, msg) { 					\
	GtkWidget *toplevel = gtk_widget_get_toplevel(widget);				\
											\
	if (gtk_widget_is_toplevel(toplevel)) {						\
		GtkWidget *dialog;							\
											\
		dialog = gtk_message_dialog_new(GTK_WINDOW(toplevel),			\
					GTK_DIALOG_DESTROY_WITH_PARENT,			\
					GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,		\
					msg);						\
											\
		gtk_window_set_title(GTK_WINDOW(dialog), title);			\
		gtk_dialog_run(GTK_DIALOG(dialog));					\
		gtk_widget_destroy (dialog);						\
	} else {									\
		printf("Cannot display dialog: Toplevel wigdet not found\n");		\
	}										\
}

struct plugin_private {
	/* plugin context */
	struct osc_plugin_context plugin_ctx;
	/* iio */
	struct iio_context *ctx;
	struct iio_device *ad9081;
	/* misc */
	gboolean plugin_detached;
	gint this_page;
	/* widget info */
	uint16_t num_widgets;
	struct iio_widget iio_widgets[NUM_MAX_WIDGETS];
	/* dac */
	struct dac_data_manager *dac_tx_manager;
	gboolean has_once_updated;
	const char *dac_name;
	char last_pfir[PATH_MAX];
};

static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	iio_w->save(iio_w);
	/* refresh widgets so that, we know if our value was updated */
	if (!GTK_IS_COMBO_BOX_TEXT(widget))
		iio_w->update(iio_w);
}

static void __iio_update_widgets(GtkWidget *widget, struct plugin_private *priv)
{
	iio_update_widgets(priv->iio_widgets, priv->num_widgets);
	if (priv->dac_tx_manager)
		dac_data_manager_update_iio_widgets(priv->dac_tx_manager);

	priv->has_once_updated = true;
}

static void select_page_cb(GtkNotebook *notebook, gpointer arg1, gint page,
			   struct plugin_private *priv)
{
	if (!priv->has_once_updated && page == priv->this_page)
		__iio_update_widgets(NULL, priv);
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

static int ad9081_add_chan_widgets(GtkBuilder *builder,
				   struct plugin_private *priv,
				   struct iio_device *ad9081,
				   struct iio_channel *voltage,
				   const int chann_nr)
{
	const int output = iio_channel_is_output(voltage);
	struct iio_widget *iio_widgets = priv->iio_widgets;
	struct {
		char *nco;
		char *main_nco;
		char *nco_phase;
		char *main_nco_phase;
	} rx_widgets[NUM_MAX_CHANNEL] = {
		{ "rx_nco_freq1", "rx_main_nco_freq1", "rx_nco_phase1", "rx_main_nco_phase1" },
		{ "rx_nco_freq2", "rx_main_nco_freq2", "rx_nco_phase2", "rx_main_nco_phase2" },
		{ "rx_nco_freq3", "rx_main_nco_freq3", "rx_nco_phase3", "rx_main_nco_phase3" },
		{ "rx_nco_freq4", "rx_main_nco_freq4", "rx_nco_phase4", "rx_main_nco_phase4" },
		{ "rx_nco_freq5", "rx_main_nco_freq5", "rx_nco_phase5", "rx_main_nco_phase5" },
		{ "rx_nco_freq6", "rx_main_nco_freq6", "rx_nco_phase6", "rx_main_nco_phase6" },
		{ "rx_nco_freq7", "rx_main_nco_freq7", "rx_nco_phase7", "rx_main_nco_phase7" },
		{ "rx_nco_freq8", "rx_main_nco_freq8", "rx_nco_phase8", "rx_main_nco_phase8" },
	};
	struct {
		char *nco;
		char *main_nco;
		char *nco_phase;
		char *main_nco_phase;
		char *en;
		char *nco_gain_scale;
		char *test_tone_en;
		char *test_tone_scale;
		char *main_nco_test_tone_en;
		char *main_nco_test_tone_scale;
	} tx_widgets[NUM_MAX_CHANNEL] = {
		 {"tx_nco_freq1", "tx_main_nco_freq1", "tx_nco_phase1",
		  "tx_main_nco_phase1", "tx_enable1", "tx_nco_gain_scale1",
		  "tx_test_tone_en1", "tx_test_tone_scale1", "tx_main_test_tone_en1",
		  "tx_main_test_tone_scale1"},
		 {"tx_nco_freq2", "tx_main_nco_freq2", "tx_nco_phase2",
		  "tx_main_nco_phase2", "tx_enable2", "tx_nco_gain_scale2",
		  "tx_test_tone_en2", "tx_test_tone_scale2", "tx_main_test_tone_en2",
		  "tx_main_test_tone_scale2"},
		 {"tx_nco_freq3", "tx_main_nco_freq3", "tx_nco_phase3",
		  "tx_main_nco_phase3", "tx_enable3", "tx_nco_gain_scale3",
		  "tx_test_tone_en3", "tx_test_tone_scale3", "tx_main_test_tone_en3",
		  "tx_main_test_tone_scale3"},
		 {"tx_nco_freq4", "tx_main_nco_freq4", "tx_nco_phase4",
		  "tx_main_nco_phase4", "tx_enable4", "tx_nco_gain_scale4",
		  "tx_test_tone_en4", "tx_test_tone_scale4", "tx_main_test_tone_en4",
		  "tx_main_test_tone_scale4"},
		 {"tx_nco_freq5", "tx_main_nco_freq5", "tx_nco_phase5",
		  "tx_main_nco_phase5", "tx_enable5", "tx_nco_gain_scale5",
		  "tx_test_tone_en5", "tx_test_tone_scale5", "tx_main_test_tone_en5",
		  "tx_main_test_tone_scale5"},
		 {"tx_nco_freq6", "tx_main_nco_freq6", "tx_nco_phase6",
		  "tx_main_nco_phase6", "tx_enable6", "tx_nco_gain_scale6",
		  "tx_test_tone_en6", "tx_test_tone_scale6", "tx_main_test_tone_en6",
		  "tx_main_test_tone_scale6"},
		 {"tx_nco_freq7", "tx_main_nco_freq7", "tx_nco_phase7",
		  "tx_main_nco_phase7", "tx_enable7", "tx_nco_gain_scale7",
		  "tx_test_tone_en7", "tx_test_tone_scale7", "tx_main_test_tone_en7",
		  "tx_main_test_tone_scale7"},
		 {"tx_nco_freq8", "tx_main_nco_freq8", "tx_nco_phase8",
		  "tx_main_nco_phase8", "tx_enable8", "tx_nco_gain_scale8",
		  "tx_test_tone_en8", "tx_test_tone_scale8", "tx_main_test_tone_en8",
		  "tx_main_test_tone_scale8"},
	};
	const char *nco = output ? tx_widgets[chann_nr].nco :
					rx_widgets[chann_nr].nco;
	const char *main_nco = output ? tx_widgets[chann_nr].main_nco :
						rx_widgets[chann_nr].main_nco;

	const char *nco_phase = output ? tx_widgets[chann_nr].nco_phase :
					rx_widgets[chann_nr].nco_phase;
	const char *main_nco_phase = output ? tx_widgets[chann_nr].main_nco_phase :
						rx_widgets[chann_nr].main_nco_phase;

	iio_spin_button_int_init_from_builder(&iio_widgets[priv->num_widgets++],
					      ad9081, voltage,
					      "channel_nco_frequency",
					      builder, nco, &mhz_scale);

	iio_spin_button_int_init_from_builder(&iio_widgets[priv->num_widgets++],
					      ad9081, voltage,
					      "main_nco_frequency",
					      builder, main_nco, &mhz_scale);

	iio_spin_button_int_init_from_builder(&iio_widgets[priv->num_widgets++],
					      ad9081, voltage,
					      "channel_nco_phase",
					      builder, nco_phase, &k_scale);

	iio_spin_button_int_init_from_builder(&iio_widgets[priv->num_widgets++],
					      ad9081, voltage,
					      "main_nco_phase",
					      builder, main_nco_phase, &k_scale);

	if (!output)
		return 0;

	/* add extra tx bindings */
	iio_toggle_button_init_from_builder(&iio_widgets[priv->num_widgets++],
					    ad9081, voltage,"en", builder,
					    tx_widgets[chann_nr].en, FALSE);


	iio_spin_button_init_from_builder(&iio_widgets[priv->num_widgets++],
					  ad9081, voltage,
					  "channel_nco_gain_scale", builder,
					  tx_widgets[chann_nr].nco_gain_scale,
					  NULL);


	iio_toggle_button_init_from_builder(&iio_widgets[priv->num_widgets++],
					    ad9081, voltage,
					    "channel_nco_test_tone_en", builder,
					    tx_widgets[chann_nr].test_tone_en,
					    FALSE);


	iio_spin_button_init_from_builder(&iio_widgets[priv->num_widgets++],
					  ad9081, voltage,
					  "channel_nco_test_tone_scale",
					  builder,
					  tx_widgets[chann_nr].test_tone_scale,
					  NULL);


	iio_toggle_button_init_from_builder(&iio_widgets[priv->num_widgets++],
					    ad9081, voltage,
					    "main_nco_test_tone_en", builder,
					    tx_widgets[chann_nr].main_nco_test_tone_en,
					    FALSE);


	iio_spin_button_init_from_builder(&iio_widgets[priv->num_widgets++],
					   ad9081, voltage,
					   "main_nco_test_tone_scale",
					   builder,
					   tx_widgets[chann_nr].main_nco_test_tone_scale,
					   NULL);

	return 0;
}

static void ad9081_adjust_main_nco(GtkBuilder *builder,
				   const int chann_nr, const long long freq,
				   const bool output)
{
	GtkAdjustment *main_nco_adjust;
	struct {
		char *rx_main_nco;
		char *tx_main_nco;
	} main_nco_str[NUM_MAX_CHANNEL] = {
		{"main_rx_nco_adjust_1", "main_tx_nco_adjust_1"},
		{"main_rx_nco_adjust_2", "main_tx_nco_adjust_2"},
		{"main_rx_nco_adjust_3", "main_tx_nco_adjust_3"},
		{"main_rx_nco_adjust_4", "main_tx_nco_adjust_4"},
		{"main_rx_nco_adjust_5", "main_tx_nco_adjust_5"},
		{"main_rx_nco_adjust_6", "main_tx_nco_adjust_6"},
		{"main_rx_nco_adjust_7", "main_tx_nco_adjust_7"},
		{"main_rx_nco_adjust_8", "main_tx_nco_adjust_8"},
	};
	const char *main_nco = output ? main_nco_str[chann_nr].tx_main_nco :
			main_nco_str[chann_nr].rx_main_nco;

	main_nco_adjust = GTK_ADJUSTMENT(gtk_builder_get_object(builder,
								main_nco));

	gtk_adjustment_set_upper(main_nco_adjust, freq / 2);
	gtk_adjustment_set_lower(main_nco_adjust, -freq / 2);
}

static void ad9081_label_writer(GtkBuilder *builder, struct iio_channel *voltage,
	const int chann_nr, const char *name)
{
	char buf[32];
	int ret;

	if (!voltage)
		return;

	ret = iio_channel_attr_read(voltage, "label", buf, sizeof(buf));
	if (ret > 0) {
		GtkWidget *label;
		gchar *text, *id;

		id = g_strdup_printf("label_%s", name);
		label = GTK_WIDGET(gtk_builder_get_object(builder, id));
		g_free(id);

		text = g_strdup_printf("<b>Channel%d</b> [%s]", chann_nr + 1, buf);
		gtk_label_set_markup(GTK_LABEL(label), text);
		g_free(text);
	}
}

static char *read_file(const char *file, ssize_t *f_size)
{
	FILE *f;
	char *buf;
	ssize_t size;

	f = fopen(file, "r");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	rewind(f);

	buf = malloc(size);
	if (!buf) {
		fclose(f);
		return NULL;
	}

	*f_size = fread(buf, sizeof(char), size, f);
	if (*f_size < size) {
		free(buf);
		buf = NULL;
	}

	fclose(f);

	return buf;
}

static void load_pfir(GtkFileChooser *chooser, gpointer data)
{
	struct plugin_private *priv = data;
	char *file_name = gtk_file_chooser_get_filename(chooser);
	char *buf;
	ssize_t size;
	int ret;

	buf = read_file(file_name, &size);
	if (!buf)
		goto err;

	ret = iio_device_attr_write_raw(priv->ad9081, "filter_fir_config", buf, size);
	free(buf);
	if (ret < 0)
		goto err;

	gtk_file_chooser_set_filename(chooser, file_name);
	strncpy(priv->last_pfir, file_name, sizeof(priv->last_pfir) - 1);
	g_free(file_name);

	return;
err:
	g_free(file_name);
	dialog_box_message(GTK_WIDGET(chooser), "PFIR Loading Failed",
			   "Failed to PFIR using the selected file!");

	if (priv->last_pfir[0])
		gtk_file_chooser_set_filename(chooser, priv->last_pfir);
	else
		gtk_file_chooser_set_filename(chooser, "(None)");
}

static GtkWidget *ad9081_init(struct osc_plugin *plugin, GtkWidget *notebook,
			      const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *ad9081_panel, *refresh_button;
	struct iio_device *ad9081_dev;
	struct iio_channel *ch0;
	char attr_val[256];
	long long adc_freq = 0, dac_freq = 0;
	GtkTextBuffer *adc_buff, *dac_buff;
	struct plugin_private *priv = plugin->priv;
	const char *dev_name = g_list_first(priv->plugin_ctx.required_devices)->data;
	int idx;
	GArray *devices;

	struct {
		const char *iio_name;
		const char *rx_name;
		const char *tx_name;
	} channels[NUM_MAX_CHANNEL] = {
		{"voltage0_i", "channel_rx_0", "channel_tx_0"},
		{"voltage1_i", "channel_rx_1", "channel_tx_1"},
		{"voltage2_i", "channel_rx_2", "channel_tx_2"},
		{"voltage3_i", "channel_rx_3", "channel_tx_3"},
		{"voltage4_i", "channel_rx_4", "channel_tx_4"},
		{"voltage5_i", "channel_rx_5", "channel_tx_5"},
		{"voltage6_i", "channel_rx_6", "channel_tx_6"},
		{"voltage7_i", "channel_rx_7", "channel_tx_7"},
	};
	struct {
		const char *iio_name;
		const char *rx_name;
		const char *tx_name;
	} channels_real[NUM_MAX_CHANNEL] = {
		{"voltage0", "channel_rx_0", "channel_tx_0"},
		{"voltage1", "channel_rx_1", "channel_tx_1"},
		{"voltage2", "channel_rx_2", "channel_tx_2"},
		{"voltage3", "channel_rx_3", "channel_tx_3"},
		{"voltage4", "channel_rx_4", "channel_tx_4"},
		{"voltage5", "channel_rx_5", "channel_tx_5"},
		{"voltage6", "channel_rx_6", "channel_tx_6"},
		{"voltage7", "channel_rx_7", "channel_tx_7"},
	};

	builder = gtk_builder_new();

	priv->ctx = osc_create_context();
	if (!priv->ctx)
		goto error_free_priv;

	ad9081_dev = iio_context_find_device(priv->ctx, dev_name);
	if (!ad9081_dev) {
		printf("Could not find iio device:%s\n", dev_name);
		goto error_free_ctx;
	}

	priv->ad9081 = ad9081_dev;

	if (osc_load_glade_file(builder, "ad9081") < 0)
		goto error_free_ctx;

	ad9081_panel = GTK_WIDGET(gtk_builder_get_object(builder,
							 "ad9081_panel"));
	/* RX Global */
	ch0 = iio_device_find_channel(ad9081_dev, "voltage0_i", FALSE);
	if (!ch0)
		ch0 = iio_device_find_channel(ad9081_dev, "voltage0", FALSE);

	if (ch0 && iio_channel_attr_read_longlong(ch0, "adc_frequency", &adc_freq) == 0)
		snprintf(attr_val, sizeof(attr_val), "%.2f",
			 (double)(adc_freq / 1000000ul));
	else
		snprintf(attr_val, sizeof(attr_val), "%s", "N/A");

	adc_buff = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(adc_buff, attr_val, -1);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(builder, "text_view_adc_freq")),
				 adc_buff);


	iio_combo_box_init_from_builder(&priv->iio_widgets[priv->num_widgets++],
					ad9081_dev, ch0, "test_mode",
					"test_mode_available", builder,
					"rx_test_mode", NULL);

	iio_combo_box_init_from_builder(&priv->iio_widgets[priv->num_widgets++],
					ad9081_dev, ch0, "nyquist_zone",
					"nyquist_zone_available", builder,
					"rx_nyquist_zone", NULL);

	iio_spin_button_int_init_from_builder(&priv->iio_widgets[priv->num_widgets++],
					ad9081_dev, NULL, "loopback_mode", builder,
					"loopback_mode", 0);

	/* TX Global */
	ch0 = iio_device_find_channel(ad9081_dev, "voltage0_i", TRUE);
	if (!ch0)
		ch0 = iio_device_find_channel(ad9081_dev, "voltage0", TRUE);

	if (ch0 && iio_channel_attr_read_longlong(ch0, "dac_frequency", &dac_freq) == 0)
		snprintf(attr_val, sizeof(attr_val), "%.2f",
			 (double)(dac_freq / 1000000ul));
	else
		snprintf(attr_val, sizeof(attr_val), "%s", "N/A");

	dac_buff = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(dac_buff, attr_val, -1);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(builder, "text_view_dac_freq")),
				 dac_buff);

	/* setup RX and TX channels */
	for (idx = 0; idx < NUM_MAX_CHANNEL; idx++) {
		GtkWidget *channel;
		struct iio_channel *in_voltage, *out_voltage;
		int ret;

		in_voltage = iio_device_find_channel(ad9081_dev,
						     channels[idx].iio_name, 0);

		if (!in_voltage)
			in_voltage = iio_device_find_channel(ad9081_dev,
						     channels_real[idx].iio_name, 0);

		ad9081_label_writer(builder, in_voltage, idx, channels[idx].rx_name);

		if (in_voltage && iio_channel_find_attr(in_voltage, "channel_nco_frequency")) {
			ret = ad9081_add_chan_widgets(builder, priv,
						      ad9081_dev,
						      in_voltage, idx);
			if (ret)
				goto error_free_ctx;

			if (adc_freq != 0 && adc_freq <= AD9081_MAX_ADC_FREQ_HZ)
				ad9081_adjust_main_nco(builder, idx, adc_freq,
						       0);

			goto tx_chann;
		}

		/* hide non existing rx channel */
		channel = GTK_WIDGET(gtk_builder_get_object(builder,
							    channels[idx].rx_name));
		gtk_widget_hide(channel);
tx_chann:
		out_voltage = iio_device_find_channel(ad9081_dev,
						      channels[idx].iio_name, TRUE);
		if (!out_voltage)
			out_voltage = iio_device_find_channel(ad9081_dev,
								channels_real[idx].iio_name, TRUE);

		ad9081_label_writer(builder, out_voltage, idx, channels[idx].tx_name);

		if (out_voltage) {
			ret = ad9081_add_chan_widgets(builder, priv,
						      ad9081_dev,
						      out_voltage, idx);
			if (ret)
				goto error_free_ctx;

			if (dac_freq != 0 && dac_freq <= AD9081_MAX_DAC_FREQ_HZ)
				ad9081_adjust_main_nco(builder, idx, dac_freq,
						       TRUE);
			continue;
		}
		/* hide non existing tx channel */
		channel = GTK_WIDGET(gtk_builder_get_object(builder,
							    channels[idx].tx_name));
		gtk_widget_hide(channel);
	}

	devices = get_iio_devices_starting_with(priv->ctx, DAC_DEVICE);
	/* If buffer capable we need to initialize the DDS container */
	if (devices->len == 1) {
		struct iio_device *dac, *hmc425;
		double dac_tx_sampling_freq = 0;
		GtkWidget *dds_container;

		dac = g_array_index(devices, struct iio_device*, 0);
		priv->dac_name = iio_device_get_name(dac);
		priv->dac_tx_manager = dac_data_manager_new(dac, NULL, priv->ctx);
		if (!priv->dac_tx_manager) {
			printf("%s: Failed to start dac Manager...\n",
			       iio_device_get_name(dac));
			g_array_free(devices, FALSE);
			goto error_free_ctx;
		}

		dds_container = GTK_WIDGET(gtk_builder_get_object(builder,
								  "dds_transmit_block"));
		gtk_container_add(GTK_CONTAINER(dds_container),
				  dac_data_manager_get_gui_container(priv->dac_tx_manager));
		gtk_widget_show_all(dds_container);

		ch0 = iio_device_find_channel(dac, "altvoltage0", true);
		if (iio_channel_attr_read_longlong(ch0, "sampling_frequency", &dac_freq) == 0)
			dac_tx_sampling_freq = (double)(dac_freq / 1000000ul);

		dac_data_manager_freq_widgets_range_update(priv->dac_tx_manager,
							   dac_tx_sampling_freq / 2);
		dac_data_manager_set_buffer_size_alignment(priv->dac_tx_manager, 64);
		dac_data_manager_set_buffer_chooser_current_folder(priv->dac_tx_manager,
								   OSC_WAVEFORM_FILE_PATH);

		g_array_free(devices, FALSE);

		hmc425 = iio_context_find_device(priv->ctx, "hmc425a");
		if (!hmc425)
			hmc425 = iio_context_find_device(priv->ctx, "hmc540s");

		if (!hmc425) {
			gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder,
									  "hmc425")));
		} else {
			ch0 = iio_device_find_channel(hmc425, "voltage0", true);
			iio_spin_button_int_init_from_builder(
				&priv->iio_widgets[priv->num_widgets++],
				hmc425, ch0, "hardwaregain", builder,
				"hmc425_attenuation", NULL);
		}
	} else {
		GtkWidget *dds_container;

		printf("Warning: Got %d DDS devices\n", devices->len);
		g_array_free(devices, FALSE);

		dds_container = GTK_WIDGET(gtk_builder_get_object(builder,
								  "dds_transmit_block"));
		gtk_widget_hide(dds_container);

		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder,
								  "hmc425")));
	}

	make_widget_update_signal_based(priv->iio_widgets, priv->num_widgets);
	/* setup the refresh button */
	refresh_button = GTK_WIDGET(gtk_builder_get_object(builder, "refresh_button"));
	g_signal_connect(G_OBJECT(refresh_button), "clicked",
			 G_CALLBACK(__iio_update_widgets), priv);

	g_signal_connect_after(G_OBJECT(notebook), "switch-page",
			       G_CALLBACK(select_page_cb), priv);

	/* load pfir cb */
	g_builder_connect_signal(builder, "pfir_config", "file-set",
	                         G_CALLBACK(load_pfir), priv);

	gtk_file_chooser_set_current_folder(
		GTK_FILE_CHOOSER(gtk_builder_get_object(builder, "pfir_config")),
		OSC_FILTER_FILE_PATH"/ad9081");

	return ad9081_panel;

error_free_ctx:
	osc_destroy_context(priv->ctx);
error_free_priv:
	osc_plugin_context_free_resources(&priv->plugin_ctx);
	g_free(priv);

	return NULL;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page,
			       gboolean is_detached)
{
	plugin->priv->this_page = active_page;
	plugin->priv->plugin_detached = is_detached;
}

static void ad9081_get_preferred_size(const struct osc_plugin *plugin,
				      int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static gpointer copy_gchar_array(gconstpointer src, gpointer data)
{
	return (gpointer)g_strdup(src);
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	struct plugin_private *priv = plugin->priv;

	if (priv->dac_tx_manager)
		dac_data_manager_free(priv->dac_tx_manager);

	osc_destroy_context(priv->ctx);
	osc_plugin_context_free_resources(&priv->plugin_ctx);
	g_free(priv);
}

GSList* get_dac_dev_names(const struct osc_plugin *plugin) {
	struct plugin_private *priv = plugin->priv;
	GSList *list = NULL;

	if (priv->dac_name)
		list = g_slist_append (list, (gpointer) priv->dac_name);

	return list;
}

struct osc_plugin *create_plugin(struct osc_plugin_context *plugin_ctx)
{
	struct osc_plugin *plugin;

	if (!plugin_ctx ) {
		printf("Cannot create plugin: plugin context not provided!\n");
		return NULL;
	}

	plugin = g_new0(struct osc_plugin, 1);
	plugin->priv = g_new0(struct plugin_private, 1);
	plugin->priv->plugin_ctx.plugin_name = g_strdup(plugin_ctx->plugin_name);
	plugin->priv->plugin_ctx.required_devices = g_list_copy_deep(plugin_ctx->required_devices,
								     (GCopyFunc)copy_gchar_array,
								     NULL);
	plugin->name = plugin->priv->plugin_ctx.plugin_name;
	plugin->dynamically_created = TRUE;
	plugin->init = ad9081_init;
	plugin->get_preferred_size = ad9081_get_preferred_size;
	plugin->update_active_page = update_active_page;
	plugin->destroy = context_destroy;
	plugin->get_dac_dev_names = get_dac_dev_names;

	return plugin;
}

GArray* get_data_for_possible_plugin_instances(void)
{
	return get_data_for_possible_plugin_instances_helper(AD9081, THIS_DRIVER);
}
