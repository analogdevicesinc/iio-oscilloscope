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
#include <gtk/gtk.h>
#include <glib.h>

#include "../osc.h"
#include "../osc_plugin.h"
#include "../iio_widget.h"
#include "../iio_utils.h"
#include "../config.h"
#include "dac_data_manager.h"

#define THIS_DRIVER			"ad9081"
#define AD9081				"axi-ad9081-rx"
#define DAC_DEVICE			"axi-ad9081-tx"
#define NUM_MAX_CHANNEL			8
#define AD9081_MAX_ADC_FREQ_MHZ		4000
#define AD9081_MAX_DAC_FREQ_MHZ		12000
/*
 * 81 = 2 widgets per maximmum of 8 rx channels + 8 widgets per maximun 8 tx
 * channels + 1 combo box for global RX configuration...
 */
#define NUM_MAX_WIDGETS			81

const gdouble mhz_scale = 1000000.0;

struct plugin_private {
	/* plugin context */
	struct osc_plugin_context plugin_ctx;
	/* iio */
	struct iio_context *ctx;
	/* misc */
	gboolean plugin_detached;
	gint this_page;
	/* widget info */
	uint16_t num_widgets;
	struct iio_widget iio_widgets[NUM_MAX_WIDGETS];
	/* dac */
	struct dac_data_manager *dac_tx_manager;
};

static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	iio_w->save(iio_w);
	/* refresh widgets so that, we know if our value was updated */
	if (!GTK_IS_COMBO_BOX_TEXT(widget))
		iio_w->update(iio_w);
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

/*
 * This is taken from iio_info utility. It should be provided by the iio lib
 * itself. Alternatively, it could be added to osc iio_utilities if we find the
 * need of more users of this...
 */
static bool ad9081_is_buffer_capable(const struct iio_device *dev)
{
	unsigned int i;

	for(i = 0; i < iio_device_get_channels_count(dev); i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);

		if (iio_channel_is_scan_element(chn))
			return true;
	}

	return false;
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
	} rx_widgets[NUM_MAX_CHANNEL] = {
		{ "rx_nco_freq1", "rx_main_nco_freq1" },
		{ "rx_nco_freq2", "rx_main_nco_freq2" },
		{ "rx_nco_freq3", "rx_main_nco_freq3" },
		{ "rx_nco_freq4", "rx_main_nco_freq4" },
		{ "rx_nco_freq5", "rx_main_nco_freq5" },
		{ "rx_nco_freq6", "rx_main_nco_freq6" },
		{ "rx_nco_freq7", "rx_main_nco_freq7" },
		{ "rx_nco_freq8", "rx_main_nco_freq8" },
	};
	struct {
		char *nco;
		char *main_nco;
		char *en;
		char *nco_gain_scale;
		char *test_tone_en;
		char *test_tone_scale;
		char *main_nco_test_tone_en;
		char *main_nco_test_tone_scale;
	} tx_widgets[NUM_MAX_CHANNEL] = {
		{"tx_nco_freq1", "tx_main_nco_freq1", "tx_enable1", "tx_nco_gain_scale1",
		 "tx_test_tone_en1", "tx_test_tone_scale1", "tx_main_test_tone_en1",
		 "tx_main_test_tone_scale1"},
		{"tx_nco_freq2", "tx_main_nco_freq2", "tx_enable2", "tx_nco_gain_scale2",
		 "tx_test_tone_en2", "tx_test_tone_scale2", "tx_main_test_tone_en2",
		 "tx_main_test_tone_scale2"},
		{"tx_nco_freq3", "tx_main_nco_freq3", "tx_enable3", "tx_nco_gain_scale3",
		 "tx_test_tone_en3", "tx_test_tone_scale3", "tx_main_test_tone_en3",
		 "tx_main_test_tone_scale3"},
		{"tx_nco_freq4", "tx_main_nco_freq4", "tx_enable4", "tx_nco_gain_scale4",
		 "tx_test_tone_en4", "tx_test_tone_scale4", "tx_main_test_tone_en4",
		 "tx_main_test_tone_scale4"},
		{"tx_nco_freq5", "tx_main_nco_freq5", "tx_enable5", "tx_nco_gain_scale5",
		 "tx_test_tone_en5", "tx_test_tone_scale5", "tx_main_test_tone_en5",
		 "tx_main_test_tone_scale5"},
		{"tx_nco_freq6", "tx_main_nco_freq6", "tx_enable6", "tx_nco_gain_scale6",
		 "tx_test_tone_en6", "tx_test_tone_scale6", "tx_main_test_tone_en6",
		 "tx_main_test_tone_scale6"},
		{"tx_nco_freq7", "tx_main_nco_freq7", "tx_enable7", "tx_nco_gain_scale7",
		 "tx_test_tone_en7", "tx_test_tone_scale7", "tx_main_test_tone_en7",
		 "tx_main_test_tone_scale7"},
		{"tx_nco_freq8", "tx_main_nco_freq8", "tx_enable8", "tx_nco_gain_scale8",
		 "tx_test_tone_en8", "tx_test_tone_scale8", "tx_main_test_tone_en8",
		 "tx_main_test_tone_scale8"},
	};
	const char *nco = output ? tx_widgets[chann_nr].nco :
					rx_widgets[chann_nr].nco;
	const char *main_nco = output ? tx_widgets[chann_nr].main_nco :
						rx_widgets[chann_nr].main_nco;

	iio_spin_button_int_init_from_builder(&iio_widgets[priv->num_widgets++],
					      ad9081, voltage,
					      "channel_nco_frequency",
					      builder, nco, &mhz_scale);

	iio_spin_button_int_init_from_builder(&iio_widgets[priv->num_widgets++],
					      ad9081, voltage,
					      "main_nco_frequency",
					      builder, main_nco, &mhz_scale);

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

static GtkWidget *ad9081_init(struct osc_plugin *plugin, GtkWidget *notebook,
			      const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *ad9081_panel;
	struct iio_device *ad9081_dev;
	struct iio_channel *ch0;
	char attr_val[256];
	long long adc_freq = 0, dac_freq = 0;
	GtkTextBuffer *adc_buff, *dac_buff;
	struct plugin_private *priv = plugin->priv;
	const char *dev_name = g_list_first(priv->plugin_ctx.required_devices)->data;
	int idx;
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

	builder = gtk_builder_new();

	priv->ctx = osc_create_context();
	if (!priv->ctx)
		goto error_free_priv;

	ad9081_dev = iio_context_find_device(priv->ctx, dev_name);
	if (!ad9081_dev) {
		printf("Could not find iio device:%s\n", dev_name);
		goto error_free_ctx;
	}

	if (osc_load_glade_file(builder, THIS_DRIVER) < 0)
		goto error_free_ctx;

	ad9081_panel = GTK_WIDGET(gtk_builder_get_object(builder,
							 "ad9081_panel"));
	/* RX Global */
	ch0 = iio_device_find_channel(ad9081_dev, "voltage0_i", FALSE);

	if (iio_channel_attr_read_longlong(ch0, "adc_frequency", &adc_freq) == 0)
		snprintf(attr_val, sizeof(attr_val), "%.2f",
			 (double)(adc_freq / 1000000ul));
	else
		snprintf(attr_val, sizeof(attr_val), "%s", "error");

	adc_buff = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(adc_buff, attr_val, -1);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(builder, "text_view_adc_freq")),
				 adc_buff);


	iio_combo_box_init_from_builder(&priv->iio_widgets[priv->num_widgets++],
					ad9081_dev, ch0, "test_mode",
					"test_mode_available", builder,
					"rx_test_mode", NULL);

	/* TX Global */
	ch0 = iio_device_find_channel(ad9081_dev, "voltage0_i", TRUE);

	if (iio_channel_attr_read_longlong(ch0, "dac_frequency", &dac_freq) == 0)
		snprintf(attr_val, sizeof(attr_val), "%.2f",
			 (double)(dac_freq / 1000000ul));
	else
		snprintf(attr_val, sizeof(attr_val), "%s", "error");

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
		if (in_voltage) {
			ret = ad9081_add_chan_widgets(builder, priv,
						      ad9081_dev,
						      in_voltage, idx);
			if (ret)
				goto error_free_ctx;

			if (adc_freq != 0 && adc_freq < AD9081_MAX_ADC_FREQ_MHZ)
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
		if (out_voltage) {
			ret = ad9081_add_chan_widgets(builder, priv,
						      ad9081_dev,
						      out_voltage, idx);
			if (ret)
				goto error_free_ctx;

			if (dac_freq != 0 && dac_freq < AD9081_MAX_DAC_FREQ_MHZ)
				ad9081_adjust_main_nco(builder, idx, dac_freq,
						       TRUE);
			continue;
		}
		/* hide non existing tx channel */
		channel = GTK_WIDGET(gtk_builder_get_object(builder,
							    channels[idx].tx_name));
		gtk_widget_hide(channel);
	}
	/* If buffer capable we need to initialize the DDS container */
	if (ad9081_is_buffer_capable(ad9081_dev)) {
		struct iio_device *dac;
		GArray *devices = get_iio_devices_starting_with(priv->ctx,
								DAC_DEVICE);
		double dac_tx_sampling_freq = 0;
		GtkWidget *dds_container;

		if (devices->len != 1) {
			printf("Warning: Got %d DDS devices. We should have 1\n",
			       devices->len);
			g_array_free(devices, FALSE);
			goto error_free_ctx;
		}

		dac = g_array_index(devices, struct iio_device*, 0);
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

		dac_data_manager_update_iio_widgets(priv->dac_tx_manager);
		g_array_free(devices, FALSE);
	} else {
		GtkWidget *dds_container;

		dds_container = GTK_WIDGET(gtk_builder_get_object(builder,
								  "dds_transmit_block"));
		gtk_widget_hide(dds_container);
	}

	make_widget_update_signal_based(priv->iio_widgets, priv->num_widgets);
	iio_update_widgets(priv->iio_widgets, priv->num_widgets);

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

	osc_plugin_context_free_resources(&priv->plugin_ctx);
	osc_destroy_context(priv->ctx);
	if (priv->dac_tx_manager)
		dac_data_manager_free(priv->dac_tx_manager);
	g_free(priv);
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

	return plugin;
}

GArray* get_data_for_possible_plugin_instances(void)
{
	GArray *data = g_array_new(FALSE, TRUE,
				   sizeof(struct osc_plugin_context *));
	struct iio_context *osc_ctx = get_context_from_osc();
	GArray *devices = get_iio_devices_starting_with(osc_ctx, AD9081);
	guint i = 0;

	for (; i < devices->len; i++) {
		struct osc_plugin_context *context = g_new0(struct osc_plugin_context, 1);
		struct iio_device *dev = g_array_index(devices,
						       struct iio_device*, i);
		/* Construct the name of the plugin */
		char *name;

		if (devices->len > 1)
			name = g_strdup_printf("%s-%i", THIS_DRIVER, i);
		else
			name = g_strdup(THIS_DRIVER);

		context->required_devices = g_list_append(context->required_devices,
							  g_strdup(iio_device_get_name(dev)));
		context->plugin_name = name;
		g_array_append_val(data, context);
	}

	g_array_free(devices, FALSE);

	return data;
}
