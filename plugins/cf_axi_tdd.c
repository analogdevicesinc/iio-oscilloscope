/**
 * CF_AXI_TDD Core Plugin
 *
 * Copyright (C) 2020 Analog Devices, Inc.
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

#define THIS_DRIVER     	"AXI-CORE-TDD"
#define TDD_DEVICE      	"axi-core-tdd"
#define NUM_MAX_WIDGETS		32

struct plugin_private {
        /* Associated GTK builder */
	GtkBuilder *builder;
        /* plugin context */
	struct osc_plugin_context plugin_ctx;
	/* iio */
	struct iio_context *ctx;
        /* misc */
	gboolean plugin_detached;
	gint this_page;
	/* widgets */
	GtkWidget *secondary_frame;
	struct iio_widget w[NUM_MAX_WIDGETS];
	struct iio_widget secondary;
	int n_w;
};

static void save_secondary(GtkToggleButton *check_button, struct plugin_private *priv)
{
	iio_widget_save_block_signals_by_data(&priv->secondary);

	if (!gtk_toggle_button_get_active(check_button))
		gtk_widget_set_sensitive(priv->secondary_frame, false);
	else
		gtk_widget_set_sensitive(priv->secondary_frame, true);
}

static void reload_settings(GtkButton *btn, struct plugin_private *priv)
{
	iio_update_widgets_block_signals_by_data(priv->w, priv->n_w);
}

static int cf_axi_tdd_chann_widgets_init(struct plugin_private *priv, struct iio_device *dev)
{
	const unsigned int n_channels = iio_device_get_channels_count(dev);
	struct iio_channel *chan;
	unsigned int i;
	char widget_str[32];

	if (!n_channels) {
		printf("Could not find any iio channel\n");
		return -ENODEV;
	}

	for (i = 0; i < n_channels; i++) {
		chan = iio_device_get_channel(dev, i);
		if (!chan)
			return -ENODEV;

		sprintf(widget_str, "%s_%s_on", iio_channel_is_output(chan) ? "tx" : "rx",
			iio_channel_get_id(chan));
		iio_spin_button_int_init_from_builder(&priv->w[priv->n_w++], dev, chan,
						      "on_ms", priv->builder, widget_str, NULL);
		sprintf(widget_str, "%s_%s_off", iio_channel_is_output(chan) ? "tx" : "rx",
			iio_channel_get_id(chan));
		iio_spin_button_int_init_from_builder(&priv->w[priv->n_w++], dev, chan,
						      "off_ms", priv->builder, widget_str, NULL);
		sprintf(widget_str, "%s_%s_dp_on", iio_channel_is_output(chan) ? "tx" : "rx",
			iio_channel_get_id(chan));
		iio_spin_button_int_init_from_builder(&priv->w[priv->n_w++], dev, chan,
						      "dp_on_ms", priv->builder, widget_str, NULL);
		sprintf(widget_str, "%s_%s_dp_off", iio_channel_is_output(chan) ? "tx" : "rx",
			iio_channel_get_id(chan));
		iio_spin_button_int_init_from_builder(&priv->w[priv->n_w++], dev, chan,
						      "dp_off_ms", priv->builder, widget_str, NULL);
		sprintf(widget_str, "%s_%s_vco_on", iio_channel_is_output(chan) ? "tx" : "rx",
			iio_channel_get_id(chan));
		iio_spin_button_int_init_from_builder(&priv->w[priv->n_w++], dev, chan,
						      "vco_on_ms", priv->builder, widget_str, NULL);
		sprintf(widget_str, "%s_%s_vco_off", iio_channel_is_output(chan) ? "tx" : "rx",
			iio_channel_get_id(chan));
		iio_spin_button_int_init_from_builder(&priv->w[priv->n_w++], dev, chan,
						      "vco_off_ms", priv->builder, widget_str, NULL);
	}

	return 0;
}

static GtkWidget *cf_axi_tdd_init(struct osc_plugin *plugin, GtkWidget *notebook,
                                  const char *ini_fn)
{
	GtkWidget *cf_axi_tdd_panel;
	struct plugin_private *priv = plugin->priv;
	struct iio_device *dev;
	const char *dev_name = g_list_first(priv->plugin_ctx.required_devices)->data;
	GtkWidget *global, *primary, *secondary;
	GtkToggleToolButton *global_btn, *primary_btn, *secondary_btn;
	GtkButton *reload_btn;
	int ret;

	priv->builder = gtk_builder_new();
	if (!priv->builder)
		return NULL;

	priv->ctx = osc_create_context();
	if (!priv->ctx)
		return NULL;

	dev = iio_context_find_device(priv->ctx, dev_name);
	if (!dev)
		goto context_destroy;

	if (osc_load_glade_file(priv->builder, "cf_axi_tdd") < 0)
		goto context_destroy;

	cf_axi_tdd_panel = GTK_WIDGET(gtk_builder_get_object(priv->builder, "cf_axi_tdd_panel"));
	if (!cf_axi_tdd_panel)
		goto context_destroy;

	/* init device widgets */
	iio_spin_button_int_init_from_builder(&priv->w[priv->n_w++], dev, NULL, "burst_count",
					      priv->builder, "burst_count", NULL);
	iio_spin_button_int_init_from_builder(&priv->w[priv->n_w++], dev, NULL, "frame_length_ms",
					      priv->builder, "frame_length", NULL);
	iio_spin_button_int_init_from_builder(&priv->w[priv->n_w++], dev, NULL, "counter_int",
					      priv->builder, "counter_int", NULL);
	iio_combo_box_init_no_avail_flush_from_builder(&priv->w[priv->n_w++], dev, NULL,"dma_gateing_mode",
						       "dma_gateing_mode_available", priv->builder,
						       "dma_gateing_mode", NULL);
	iio_combo_box_init_no_avail_flush_from_builder(&priv->w[priv->n_w++], dev, NULL, "en_mode",
						       "en_mode_available", priv->builder, "enable_mode", NULL);
	iio_toggle_button_init_from_builder(&priv->w[priv->n_w++], dev, NULL, "sync_terminal_type",
					    priv->builder, "sync_terminal_type", false);
	iio_toggle_button_init_from_builder(&priv->w[priv->n_w++], dev, NULL, "en", priv->builder,
					    "enable", false);
	iio_toggle_button_init_from_builder(&priv->secondary, dev, NULL, "secondary", priv->builder,
					    "secondary_frame", false);
	/* init channel widgets */
	ret = cf_axi_tdd_chann_widgets_init(priv, dev);
	if (ret)
		goto context_destroy;

	/* handle sections buttons and reload settings */
	global = GTK_WIDGET(gtk_builder_get_object(priv->builder, "global_settings"));
	global_btn = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(priv->builder,
								   "global_settings_toggle"));
	primary = GTK_WIDGET(gtk_builder_get_object(priv->builder, "tdd_primary"));
	primary_btn = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(priv->builder,
								    "tdd_primary_toggle"));
	secondary = GTK_WIDGET(gtk_builder_get_object(priv->builder, "tdd_secondary"));
	secondary_btn = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(priv->builder,
								      "tdd_secondary_toggle"));

	reload_btn = GTK_BUTTON(gtk_builder_get_object(priv->builder, "settings_reload"));
	priv->secondary_frame = GTK_WIDGET(gtk_builder_get_object(priv->builder,
								  "tdd_secondary_frame"));

	g_signal_connect(G_OBJECT(global_btn), "clicked", G_CALLBACK(handle_toggle_section_cb),
			 global);
	g_signal_connect(G_OBJECT(primary_btn), "clicked", G_CALLBACK(handle_toggle_section_cb),
			 primary);
	g_signal_connect(G_OBJECT(secondary_btn), "clicked", G_CALLBACK(handle_toggle_section_cb),
			 secondary);
	g_signal_connect(G_OBJECT(reload_btn), "clicked", G_CALLBACK(reload_settings),
			 priv);

	iio_update_widgets(priv->w, priv->n_w);
	iio_widget_update(&priv->secondary);
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->secondary.widget)))
		gtk_widget_set_sensitive(priv->secondary_frame, false);

	iio_make_widgets_update_signal_based(priv->w, priv->n_w,
					     G_CALLBACK(iio_widget_save_block_signals_by_data_cb));
	iio_make_widget_update_signal_based(&priv->secondary, G_CALLBACK(save_secondary), priv);

	return cf_axi_tdd_panel;

context_destroy:
	osc_destroy_context(priv->ctx);
	osc_plugin_context_free_resources(&priv->plugin_ctx);
	g_free(priv);

	return NULL;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	plugin->priv->this_page = active_page;
	plugin->priv->plugin_detached = is_detached;
}

static void cf_axi_tdd_get_preferred_size(const struct osc_plugin *plugin, int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	struct plugin_private *priv = plugin->priv;

	osc_plugin_context_free_resources(&priv->plugin_ctx);
	osc_destroy_context(priv->ctx);
	g_free(priv);
}

static gpointer copy_gchar_array(gconstpointer src, gpointer data)
{
	return (gpointer)g_strdup(src);
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
	plugin->init = cf_axi_tdd_init;
	plugin->get_preferred_size = cf_axi_tdd_get_preferred_size;
	plugin->update_active_page = update_active_page;
	plugin->destroy = context_destroy;

	return plugin;
}

GArray* get_data_for_possible_plugin_instances(void)
{
	return get_data_for_possible_plugin_instances_helper(TDD_DEVICE, THIS_DRIVER);
}
