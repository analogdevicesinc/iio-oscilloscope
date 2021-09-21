/**
 * ADRV9002 (Navassa) Plugin
 *
 * Copyright (C) 2020 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <ctype.h>
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

#ifndef ENOTSUPP
#define ENOTSUPP	524
#endif
#define THIS_DRIVER "ADRV9002"
#define PHY_DEVICE "adrv9002-phy"
#define DDS_DEVICE "axi-adrv9002-tx"
#define CAP_DEVICE "axi-adrv9002-rx"

#define ADRV9002_NUM_CHANNELS	2

/* Max nr of widgets per channel */
#define NUM_MAX_WIDGETS	10
#define NUM_MAX_DDS	2
#define NUM_MAX_ADC	2

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

const gdouble mhz_scale = 1000000.0;

struct adrv9002_gtklabel {
	GtkLabel *labels;
	struct iio_channel *chann;
	const char *iio_attr;
	const char *label_str;
	int scale;
};

/*
 * This wrappes a combox widget. The motivation for this is that in osc
 * implementation of combo boxes, the box is always cleared before updating it
 * with the active value. This assumes that the iio available values can change.
 * This will lead to the gtk 'changed' signal to be always called if we want to
 * update the box value. Hence, we cannot call 'iio_widget_update' on the
 * 'changed' signal since it leads to an infinite loop.
 * In this plugin, we know that our available iio attribute will never change,
 * so we are implementing a mechanism where we won't use osc core and so we
 * can update our combo boxes.
 */
struct adrv9002_combo_box {
	struct iio_widget w;
	/*
	 * This indicates that a manual or automatic update occured
	 * (without using interaction with the GUI) in a combo box widget.
	 * In this case, we don't want to save the value since it will be just
	 * the same...
	 */
	bool m_update;
};

struct adrv9002_common {
	struct adrv9002_combo_box gain_ctrl;
	struct iio_widget gain;
	struct iio_widget nco_freq;
	struct adrv9002_combo_box ensm;
	struct adrv9002_combo_box port_en;
	struct adrv9002_gtklabel rf_bandwidth;
	struct adrv9002_gtklabel sampling_rate;
	/* these are generic widgets that don't need any special attention */
	struct iio_widget w[NUM_MAX_WIDGETS];
	uint16_t num_widgets;
	bool enabled;
};

struct adrv9002_rx {
	struct adrv9002_common rx;
	struct adrv9002_combo_box digital_gain_ctl;
	struct adrv9002_combo_box intf_gain;
	struct adrv9002_gtklabel rssi;
	struct adrv9002_gtklabel decimated_power;
};

struct adrv9002_dac_mgmt {
	struct dac_data_manager *dac_tx_manager;
	const char *dac_name;
	struct iio_channel *ch0;
};

struct plugin_private {
	/* Associated GTK builder */
	GtkBuilder *builder;
	/* notebook */
	GtkNotebook *nbook;
	/* plugin context */
	struct osc_plugin_context plugin_ctx;
	/* iio */
	struct iio_context *ctx;
	struct iio_device *adrv9002;
	/* misc */
	gboolean plugin_detached;
	gint this_page;
	gint refresh_timeout;
	char last_profile[PATH_MAX];
	char last_stream[PATH_MAX];
	struct adrv9002_gtklabel temperature;
	/* rx */
	struct adrv9002_rx rx_widgets[ADRV9002_NUM_CHANNELS];
	/* tx */
	struct adrv9002_common tx_widgets[ADRV9002_NUM_CHANNELS];
	/* dac */
	struct adrv9002_dac_mgmt dac_manager[NUM_MAX_DDS];
	int n_dacs;
	/* adc */
	const char *adc_name[NUM_MAX_ADC];
	int n_adcs;
};

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

static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	iio_w->save(iio_w);
	iio_w->update(iio_w);
}

static void save_gain_value(GtkWidget *widget, struct adrv9002_common *chann)
{
	char *gain_ctl = gtk_combo_box_text_get_active_text(
			GTK_COMBO_BOX_TEXT(chann->gain_ctrl.w.widget));
	/*
	 * Do not save the value if we are in automatic gain control. We can get here if we
	 * change the gain to automatic and the part changes the RX gain...
	 */
	if (!strcmp(gain_ctl, "automatic"))
		goto free_gain_ctl;

	iio_widget_save(&chann->gain);
free_gain_ctl:
	g_free(gain_ctl);
}

static void combo_box_manual_update(struct adrv9002_combo_box *combo)
{
	char text[512], *item;
	int ret, i = 0;
	struct iio_widget *w = &combo->w;
	GtkWidget *widget = w->widget;
	gint idx = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
	GtkTreeIter iter;
	GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(widget));
	gboolean has_iter;

	ret = iio_channel_attr_read(w->chn, w->attr_name,
				    text, sizeof(text));
	if (ret < 0)
		return;

	has_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (has_iter) {
		gtk_tree_model_get(model, &iter, 0, &item, -1);
		if (strcmp(text, item) == 0) {
			if (i != idx) {
				/*
				 * This assumes the gtk signal is connected prior to call this function.
				 * If the signal is connected after this API is called and we get an update,
				 * we won't save the attribute value, the next time the user changes it in
				 * the GUI.
				 */
				combo->m_update = true;
				gtk_combo_box_set_active(GTK_COMBO_BOX(widget), i);
			}

			g_free(item);
			break;
		}
		g_free(item);
		i++;
		has_iter = gtk_tree_model_iter_next(model, &iter);
	}
}

/*
 * This checks if the there was any manual or automatic update before
 * writing the value in the device. Furthermore, it checks if were successful
 * in writing the value.
 */
static void combo_box_save(GtkWidget *widget, struct adrv9002_combo_box *combo)
{
	if (combo->m_update) {
		combo->m_update = false;
		return;
	}
	combo->w.save(&combo->w);
	combo_box_manual_update(combo);
}

static void save_gain_ctl(GtkWidget *widget, struct adrv9002_common *chann)
{
	char *gain_ctl;

	combo_box_save(widget, &chann->gain_ctrl);

	gain_ctl = gtk_combo_box_text_get_active_text(
		GTK_COMBO_BOX_TEXT(widget));

	if (gain_ctl && strcmp(gain_ctl, "spi")) {
		gtk_widget_set_sensitive(chann->gain.widget, false);
	}
	else {
		gtk_widget_set_sensitive(chann->gain.widget, true);
		/*
		 * When changing modes the device might automatically change
		 * some values
		 */
		iio_widget_update(&chann->gain);
	}

	g_free(gain_ctl);
}

static void save_intf_gain(GtkWidget *widget, struct adrv9002_rx *rx)
{
	char *ensm = gtk_combo_box_text_get_active_text(
			GTK_COMBO_BOX_TEXT(rx->rx.ensm.w.widget));

	/*
	 * This is done here to prevent the dialog box from poping up. Sometimes
	 * changing a port state (e.g: from rf_enabled to prime) might affect
	 * other values. In that case we could end up in this callback and
	 * display the error dialog box without any actual misuse from the user.
	 */
	if (rx->intf_gain.m_update == true) {
		rx->intf_gain.m_update = false;
		g_free(ensm);
		return;
	}

	if (ensm && strcmp(ensm, "rf_enabled")) {
		dialog_box_message(widget, "Interface Gain Set Failed",
				   "ENSM must be rf_enabled to change the interface gain");
		combo_box_manual_update(&rx->intf_gain);
	} else {
		combo_box_save(widget, &rx->intf_gain);
	}

	g_free(ensm);
}

static void save_digital_gain_ctl(GtkWidget *widget, struct adrv9002_rx *rx)
{
	char *digital_gain;

	combo_box_save(widget, &rx->digital_gain_ctl);
	digital_gain = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));

	if (digital_gain && strcmp(digital_gain, "spi")) {
		gtk_widget_set_sensitive(rx->intf_gain.w.widget, false);
	} else {
		gtk_widget_set_sensitive(rx->intf_gain.w.widget, true);
		combo_box_manual_update(&rx->intf_gain);
	}

	g_free(digital_gain);
}

static void save_port_en(GtkWidget *widget, struct adrv9002_common *chann)
{
	char *port_en;

	combo_box_save(widget, &chann->port_en);
	port_en = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));

	if (port_en && strcmp(port_en, "spi")) {
		gtk_widget_set_sensitive(chann->ensm.w.widget, false);
	} else {
		gtk_widget_set_sensitive(chann->ensm.w.widget, true);
		combo_box_manual_update(&chann->ensm);
	}

	g_free(port_en);
}

static void handle_section_cb(GtkToggleToolButton *btn, GtkWidget *section)
{
	GtkWidget *toplevel;

	if (gtk_toggle_tool_button_get_active(btn)) {
		g_object_set(GTK_OBJECT(btn), "stock-id", "gtk-go-down", NULL);
		gtk_widget_show(section);
	} else {
		g_object_set(GTK_OBJECT(btn), "stock-id", "gtk-go-up", NULL);
		gtk_widget_hide(section);
		toplevel = gtk_widget_get_toplevel(GTK_WIDGET(btn));

		if (gtk_widget_is_toplevel(toplevel))
			gtk_window_resize(GTK_WINDOW(toplevel), 1, 1);
	}
}

static void adrv9002_gtk_label_init(struct plugin_private *priv,
				    struct adrv9002_gtklabel *adrv9002_label,
				    struct iio_channel *chann,
				    const char *iio_str, const char *label,
				    const int scale)
{
	GtkLabel *glabel = GTK_LABEL(gtk_builder_get_object(priv->builder,
							    label));

	adrv9002_label->chann = chann;
	adrv9002_label->iio_attr = iio_str;
	adrv9002_label->scale = scale ? scale : 1;
	adrv9002_label->labels = glabel;
}

static void update_dac_manager(struct plugin_private *priv)
{
	int i;

	for(i = 0; i < priv->n_dacs; i++) {
		double dac_tx_sampling_freq = 0;
		struct iio_channel *ch0 = priv->dac_manager[i].ch0;
		long long dac_freq = 0;

		if (iio_channel_attr_read_longlong(ch0, "sampling_frequency", &dac_freq) == 0)
			dac_tx_sampling_freq = (double)dac_freq / 1000000ul;

		dac_data_manager_freq_widgets_range_update(priv->dac_manager[i].dac_tx_manager,
							   dac_tx_sampling_freq / 2);

		dac_data_manager_update_iio_widgets(priv->dac_manager[i].dac_tx_manager);
	}
}

static void update_label(const struct adrv9002_gtklabel *label)
{
	double val;
	char attr_val[64];

	if (iio_channel_attr_read_double(label->chann, label->iio_attr, &val) == 0)
		snprintf(attr_val, sizeof(attr_val), "%.4f", val / label->scale);
	else
		snprintf(attr_val, sizeof(attr_val), "%s", "error");

	gtk_label_set_text(label->labels, attr_val);
}

static void update_special_widgets(struct adrv9002_common *chann)
{
	char *gain_ctl = gtk_combo_box_text_get_active_text(
		GTK_COMBO_BOX_TEXT(chann->gain_ctrl.w.widget));
	char *port_en = gtk_combo_box_text_get_active_text(
		GTK_COMBO_BOX_TEXT(chann->port_en.w.widget));

	if (gain_ctl && strcmp(gain_ctl, "spi"))
		iio_widget_update(&chann->gain);

	if (port_en && strcmp(port_en, "spi"))
		combo_box_manual_update(&chann->ensm);

	g_free(gain_ctl);
	g_free(port_en);
}

static void update_special_rx_widgets(struct adrv9002_rx *rx, const int n_widgets)
{
	int i;

	for (i = 0; i < n_widgets; i++) {
		char *digital_gain = gtk_combo_box_text_get_active_text(
			GTK_COMBO_BOX_TEXT(rx[i].digital_gain_ctl.w.widget));

		if (!rx[i].rx.enabled)
			goto nex_widget;

		update_label(&rx[i].rssi);
		update_label(&rx[i].decimated_power);
		update_special_widgets(&rx[i].rx);

		if (digital_gain && strstr(digital_gain, "automatic_control"))
			combo_box_manual_update(&rx[i].intf_gain);
nex_widget:
		g_free(digital_gain);
	}
}

static void update_special_tx_widgets(struct adrv9002_common *tx, const int n_widgets)
{
	int i;

	for (i = 0; i < n_widgets; i++) {
		if (!tx[i].enabled)
			continue;

		update_special_widgets(&tx[i]);
	}
}

static gboolean update_display(gpointer arg)
{
	struct plugin_private *priv = arg;

	if (priv->this_page == gtk_notebook_get_current_page(priv->nbook) ||
	    priv->plugin_detached) {
		update_special_rx_widgets(priv->rx_widgets,
					  ARRAY_SIZE(priv->rx_widgets));
		update_special_tx_widgets(priv->tx_widgets,
					  ARRAY_SIZE(priv->tx_widgets));
		update_label(&priv->temperature);
	}

	return true;
}

static void adrv9002_update_rx_widgets(struct plugin_private *priv, const int chann)
{
	struct adrv9002_rx *rx = &priv->rx_widgets[chann];

	/* rx */
	if (!rx->rx.enabled)
		return;

	combo_box_manual_update(&rx->rx.gain_ctrl);
	iio_widget_update(&rx->rx.gain);
	iio_widget_update(&rx->rx.nco_freq);
	combo_box_manual_update(&rx->rx.ensm);
	combo_box_manual_update(&rx->rx.port_en);
	combo_box_manual_update(&rx->digital_gain_ctl);
	combo_box_manual_update(&rx->intf_gain);
	/* generic widgets */
	iio_update_widgets(rx->rx.w, rx->rx.num_widgets);
	/* labels */
	update_label(&rx->rssi);
	update_label(&rx->decimated_power);
	update_label(&rx->rx.rf_bandwidth);
	update_label(&rx->rx.sampling_rate);
}

static void adrv9002_update_tx_widgets(struct plugin_private *priv, const int chann)
{
	struct adrv9002_common *tx = &priv->tx_widgets[chann];

	if (!tx->enabled)
		return;

	combo_box_manual_update(&tx->gain_ctrl);
	iio_widget_update(&tx->gain);
	iio_widget_update(&tx->nco_freq);
	combo_box_manual_update(&tx->ensm);
	combo_box_manual_update(&tx->port_en);
	/* generic widgets */
	iio_update_widgets(tx->w, tx->num_widgets);
	/* labels */
	update_label(&tx->rf_bandwidth);
	update_label(&tx->sampling_rate);
}

static void rx_sample_rate_update(struct plugin_private *priv)
{
	int i;

	for (i = 0; i < priv->n_adcs; i++) {
		rx_update_device_sampling_freq(priv->adc_name[i],
					       USE_INTERN_SAMPLING_FREQ);
	}
}

static void update_all(struct plugin_private *priv)
{
	int i;

	for(i = 0; i < ADRV9002_NUM_CHANNELS; i++) {
		adrv9002_update_rx_widgets(priv, i);
		adrv9002_update_tx_widgets(priv, i);
	}
	update_label(&priv->temperature);
	update_dac_manager(priv);
	rx_sample_rate_update(priv);
}

static void reload_settings(GtkButton *btn, struct plugin_private *priv)
{
	g_source_remove(priv->refresh_timeout);
	update_all(priv);
	/* re-arm the timer */
	priv->refresh_timeout = g_timeout_add(1000, (GSourceFunc)update_display,
					      priv);
}

static void adrv9002_check_channel_status(struct plugin_private *priv,
					  struct iio_channel *channel,
					  struct adrv9002_common *chan,
					  const char *gtk_str)
{
	int ret;
	double dummy;
	/*
	 * We use this attr to check if the channel is enabled or not since it should only
	 * return error if the channel is disabled (assuming there's nothing seriously
	 * wrong with the device).
	 */
	ret = iio_channel_attr_read_double(channel, "rf_bandwidth", &dummy);
	if (ret == -ENODEV) {
		chan->enabled = false;
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(priv->builder,
								  gtk_str)));
	} else {
		chan->enabled = true;
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(priv->builder,
								  gtk_str)));
	}
}

static void adrv9002_check_nco_freq_support(struct plugin_private *priv, const int channel,
					    const bool tx)
{
	int ret;
	long long dummy;
	char gtk_str[32], label_str[32];
	struct iio_widget *nco;

	if (tx) {
		nco = &priv->tx_widgets[channel].nco_freq;
		sprintf(gtk_str, "nco_freq_tx%d", channel + 1);
		sprintf(label_str, "nco_label_tx%d", channel + 1);
	} else {
		nco = &priv->rx_widgets[channel].rx.nco_freq;
		sprintf(gtk_str, "nco_freq_rx%d", channel + 1);
		sprintf(label_str, "nco_label_rx%d", channel + 1);
	}

	ret = iio_channel_attr_read_longlong(nco->chn, "nco_frequency", &dummy);
	if (ret == -ENOTSUPP) {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(priv->builder,
								  label_str)));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(priv->builder,
								  gtk_str)));
	} else {
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(priv->builder,
								  label_str)));
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(priv->builder,
								  gtk_str)));
	}
}

void check_enabled_channels(struct plugin_private *priv)
{
	int i;
	struct iio_channel *chann;
	char chan_str[32];
	char gtk_str[32];

	for(i = 0; i < ADRV9002_NUM_CHANNELS; i++) {
		sprintf(chan_str, "voltage%d", i);
		sprintf(gtk_str, "frame_rx%d", i + 1);
		/* rx */
		chann = iio_device_find_channel(priv->adrv9002, chan_str, false);
		if (chann)
			adrv9002_check_channel_status(priv, chann, &priv->rx_widgets[i].rx,
						      gtk_str);
		/* tx */
		sprintf(gtk_str, "frame_tx%d", i + 1);
		chann = iio_device_find_channel(priv->adrv9002, chan_str, true);
		if (!chann)
			continue;

		adrv9002_check_channel_status(priv, chann, &priv->tx_widgets[i], gtk_str);
	}
}

static void check_nco_support(struct plugin_private *priv)
{
	int i;

	for(i = 0; i < ADRV9002_NUM_CHANNELS; i++) {
		adrv9002_check_nco_freq_support(priv, i, false);
		adrv9002_check_nco_freq_support(priv, i, true);
	}
}

static char *read_file(const char *file, ssize_t *f_size)
{
	FILE *f;
	char *buf;
	ssize_t size;

	f = fopen(file, "rb");
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

static void load_stream(GtkFileChooser *chooser, gpointer data)
{
	struct plugin_private *priv = data;
	char *file_name = gtk_file_chooser_get_filename(chooser);
	char *buf;
	ssize_t size;
	int ret;

	buf = read_file(file_name, &size);
	if (!buf)
		goto err;

	ret = iio_device_attr_write_raw(priv->adrv9002, "stream_config", buf, size);
	free(buf);
	if (ret < 0)
		goto err;

	gtk_file_chooser_set_filename(chooser, file_name);
	strncpy(priv->last_stream, file_name, sizeof(priv->last_stream) - 1);
	g_free(file_name);
	return;
err:
	g_free(file_name);
	dialog_box_message(GTK_WIDGET(chooser), "Stream Loading Failed",
			   "Failed to load stream using the selected file!");

	if (priv->last_stream[0])
		gtk_file_chooser_set_filename(chooser, priv->last_stream);
	else
		gtk_file_chooser_set_filename(chooser, "(None)");
}

static void load_profile(GtkFileChooser *chooser, gpointer data)
{
	struct plugin_private *priv = data;
	char *file_name = gtk_file_chooser_get_filename(chooser);
	char *buf;
	int ret;
	ssize_t size;

	buf = read_file(file_name, &size);
	if (!buf)
		goto err;

	g_source_remove(priv->refresh_timeout);
	iio_context_set_timeout(priv->ctx, 30000);
	ret = iio_device_attr_write_raw(priv->adrv9002, "profile_config", buf,
					size);
	free(buf);
	iio_context_set_timeout(priv->ctx, 5000);
	if (ret < 0)
		goto err;

	gtk_file_chooser_set_filename(chooser, file_name);
	strncpy(priv->last_profile, file_name, sizeof(priv->last_profile) - 1);
	g_free(file_name);
	/* check which channels are enabled */
	check_enabled_channels(priv);
	/* check if nco is available on profile */
	check_nco_support(priv);
	/* update widgets*/
	update_all(priv);
	/* re-arm the timer */
	priv->refresh_timeout = g_timeout_add(1000, (GSourceFunc)update_display,
					      priv);
	return;
err:
	g_free(file_name);
	dialog_box_message(GTK_WIDGET(chooser), "Profile Configuration Failed",
			   "Failed to load profile using the selected file!");

	if (priv->last_profile[0])
		gtk_file_chooser_set_filename(chooser, priv->last_profile);
	else
		gtk_file_chooser_set_filename(chooser, "(None)");

	/* re-arm the timer */
	priv->refresh_timeout = g_timeout_add(1000, (GSourceFunc)update_display,
					      priv);
}

static void adrv9002_combo_box_init(struct adrv9002_combo_box *combo, const char *w_str,
				    const char *attr, const char *attr_avail,
				    struct plugin_private *priv, struct iio_channel *chann)
{
	char text[1024];
	struct iio_widget *w = &combo->w;
	int ret;
	gchar **saved_list, **available;

	iio_combo_box_init_from_builder(&combo->w, priv->adrv9002, chann, attr, attr_avail,
					priv->builder, w_str, NULL);


	ret = iio_channel_attr_read(w->chn, w->attr_name_avail, text,
				    sizeof(text));
	if (ret < 0)
		return;

	available = saved_list = g_strsplit(text, " ", 0);

	/* our available is static so we can just set it here once */
	for (; *available; available++) {
		if (*available[0] == '\0')
			continue;
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo->w.widget),
					       *available);
	}

	g_strfreev(saved_list);
}

static int adrv9002_tx_widgets_init(struct plugin_private *priv, const int chann)
{
	struct iio_channel *channel, *tx_lo;
	char chann_str[32];
	char widget_str[256];
	const char *lo_attr = chann ? "TX2_LO_frequency" : "TX1_LO_frequency";
	uint16_t *n_w = &priv->tx_widgets[chann].num_widgets;

	sprintf(chann_str, "voltage%d", chann);
	channel = iio_device_find_channel(priv->adrv9002, chann_str, true);
	if (!channel)
		return -ENODEV;

	/* LO goes from 0 to 3, the first 2 for RX and the other for TX */
	sprintf(chann_str, "altvoltage%d", chann + 2);
	tx_lo = iio_device_find_channel(priv->adrv9002, chann_str, true);
	if (!tx_lo)
		return -ENODEV;

	sprintf(widget_str, "attenuation_control_tx%d", chann + 1);
	adrv9002_combo_box_init(&priv->tx_widgets[chann].gain_ctrl, widget_str,
				"atten_control_mode", "atten_control_mode_available", priv,
				channel);

	sprintf(widget_str, "port_en_tx%d", chann + 1);
	adrv9002_combo_box_init(&priv->tx_widgets[chann].port_en, widget_str,
				"port_en_mode", "port_en_mode_available", priv, channel);

	sprintf(widget_str, "ensm_tx%d", chann + 1);
	adrv9002_combo_box_init(&priv->tx_widgets[chann].ensm, widget_str,
				"ensm_mode", "ensm_mode_available", priv, channel);

	sprintf(widget_str, "lo_leakage_tracking_en_tx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->tx_widgets[chann].w[(*n_w)++],
					    priv->adrv9002, channel,
					    "lo_leakage_tracking_en", priv->builder,
					    widget_str, false);

	sprintf(widget_str, "quadrature_tracking_en_tx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->tx_widgets[chann].w[(*n_w)++],
					    priv->adrv9002, channel,
					    "quadrature_tracking_en", priv->builder,
					    widget_str, false);

	sprintf(widget_str, "pa_correction_tracking_en_tx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->tx_widgets[chann].w[(*n_w)++],
					    priv->adrv9002, channel,
					    "pa_correction_tracking_en", priv->builder,
					    widget_str, false);

	sprintf(widget_str, "close_loop_gain_tracking_en_tx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->tx_widgets[chann].w[(*n_w)++],
					    priv->adrv9002, channel,
					    "close_loop_gain_tracking_en", priv->builder,
					    widget_str, false);

	sprintf(widget_str, "loopback_delay_tracking_en_tx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->tx_widgets[chann].w[(*n_w)++],
					    priv->adrv9002, channel,
					    "loopback_delay_tracking_en", priv->builder,
					    widget_str, false);

	sprintf(widget_str, "powerdown_en_tx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->tx_widgets[chann].w[(*n_w)++],
					    priv->adrv9002, channel,
					    "en", priv->builder, widget_str, true);

	sprintf(widget_str, "nco_freq_tx%d", chann + 1);
	iio_spin_button_int_init_from_builder(&priv->tx_widgets[chann].nco_freq,
					      priv->adrv9002, channel,
					      "nco_frequency",
					      priv->builder, widget_str, NULL);
	adrv9002_check_nco_freq_support(priv, chann, true);

	sprintf(widget_str, "hardware_gain_tx%d", chann + 1);
	iio_spin_button_init_from_builder(&priv->tx_widgets[chann].gain,
					  priv->adrv9002, channel,
					  "hardwaregain",
					  priv->builder, widget_str, NULL);

	sprintf(widget_str, "lo_freq_tx%d", chann + 1);
	iio_spin_button_int_init_from_builder(&priv->tx_widgets[chann].w[(*n_w)++],
					      priv->adrv9002, tx_lo, lo_attr,
					      priv->builder, widget_str, &mhz_scale);

	sprintf(widget_str, "sampling_rate_tx%d", chann + 1);
	adrv9002_gtk_label_init(priv, &priv->tx_widgets[chann].sampling_rate, channel,
				"sampling_frequency", widget_str, 1000000);

	sprintf(widget_str, "bandwidth_tx%d", chann + 1);
	adrv9002_gtk_label_init(priv, &priv->tx_widgets[chann].rf_bandwidth, channel, "rf_bandwidth",
				widget_str, 1000000);

	sprintf(widget_str, "frame_tx%d", chann + 1);
	adrv9002_check_channel_status(priv, channel, &priv->tx_widgets[chann],
				      widget_str);

	return 0;
}

static int adrv9002_rx_widgets_init(struct plugin_private *priv, const int chann)
{
	struct iio_channel *channel, *rx_lo;
	char chann_str[32];
	char widget_str[256];
	const char *lo_attr = chann ? "RX2_LO_frequency" : "RX1_LO_frequency";
	uint16_t *n_w = &priv->rx_widgets[chann].rx.num_widgets;

	sprintf(chann_str, "voltage%d", chann);
	channel = iio_device_find_channel(priv->adrv9002, chann_str, false);
	if (!channel)
		return -ENODEV;

	sprintf(chann_str, "altvoltage%d", chann);
	rx_lo = iio_device_find_channel(priv->adrv9002, chann_str, true);
	if (!rx_lo)
		return -ENODEV;

	sprintf(widget_str, "gain_control_rx%d", chann + 1);
	adrv9002_combo_box_init(&priv->rx_widgets[chann].rx.gain_ctrl, widget_str,
				"gain_control_mode", "gain_control_mode_available", priv,
				channel);

	sprintf(widget_str, "port_en_rx%d", chann + 1);
	adrv9002_combo_box_init(&priv->rx_widgets[chann].rx.port_en, widget_str,
				"port_en_mode", "port_en_mode_available", priv, channel);

	sprintf(widget_str, "interface_gain_rx%d", chann + 1);
	adrv9002_combo_box_init(&priv->rx_widgets[chann].intf_gain, widget_str,
				"interface_gain", "interface_gain_available", priv, channel);

	sprintf(widget_str, "ensm_rx%d", chann + 1);
	adrv9002_combo_box_init(&priv->rx_widgets[chann].rx.ensm, widget_str,
				"ensm_mode", "ensm_mode_available", priv, channel);

	sprintf(widget_str, "digital_gain_control_rx%d", chann + 1);
	adrv9002_combo_box_init(&priv->rx_widgets[chann].digital_gain_ctl, widget_str,
				"digital_gain_control_mode", "digital_gain_control_mode_available",
				priv, channel);

	sprintf(widget_str, "powerdown_en_rx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->rx_widgets[chann].rx.w[(*n_w)++],
					    priv->adrv9002, channel,
					    "en", priv->builder, widget_str, true);

	sprintf(widget_str, "agc_tracking_en_rx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->rx_widgets[chann].rx.w[(*n_w)++],
					    priv->adrv9002, channel,
					    "agc_tracking_en", priv->builder,
					    widget_str, false);

	sprintf(widget_str, "bbdc_rejection_tracking_en_rx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->rx_widgets[chann].rx.w[(*n_w)++],
					    priv->adrv9002, channel,
					    "bbdc_rejection_tracking_en", priv->builder,
					    widget_str, false);

	sprintf(widget_str, "hd2_tracking_en_rx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->rx_widgets[chann].rx.w[(*n_w)++],
					    priv->adrv9002, channel,
					    "hd_tracking_en", priv->builder, widget_str,
					    false);

	sprintf(widget_str, "quadrature_fic_tracking_en_rx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->rx_widgets[chann].rx.w[(*n_w)++],
					    priv->adrv9002, channel,
					    "quadrature_fic_tracking_en", priv->builder,
					    widget_str, false);

	sprintf(widget_str, "quadrature_poly_tracking_en_rx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->rx_widgets[chann].rx.w[(*n_w)++],
					    priv->adrv9002, channel,
					    "quadrature_w_poly_tracking_en", priv->builder,
					    widget_str, false);

	sprintf(widget_str, "rfdc_tracking_en_rx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->rx_widgets[chann].rx.w[(*n_w)++],
					    priv->adrv9002, channel,
					    "rfdc_tracking_en", priv->builder, widget_str,
					    false);

	sprintf(widget_str, "rssi_tracking_en_rx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->rx_widgets[chann].rx.w[(*n_w)++],
					    priv->adrv9002, channel,
					    "rssi_tracking_en", priv->builder, widget_str,
					    false);

	sprintf(widget_str, "nco_freq_rx%d", chann + 1);
	iio_spin_button_int_init_from_builder(&priv->rx_widgets[chann].rx.nco_freq,
					      priv->adrv9002, channel,
					      "nco_frequency",
					      priv->builder, widget_str, NULL);
	adrv9002_check_nco_freq_support(priv, chann, false);

	sprintf(widget_str, "hardware_gain_rx%d", chann + 1);
	iio_spin_button_init_from_builder(&priv->rx_widgets[chann].rx.gain,
					  priv->adrv9002, channel,
					  "hardwaregain",
					  priv->builder, widget_str, NULL);

	sprintf(widget_str, "lo_freq_rx%d", chann + 1);
	iio_spin_button_int_init_from_builder(&priv->rx_widgets[chann].rx.w[(*n_w)++],
					      priv->adrv9002, rx_lo, lo_attr,
					      priv->builder, widget_str, &mhz_scale);

	sprintf(widget_str, "decimated_power_rx%d", chann + 1);
	adrv9002_gtk_label_init(priv, &priv->rx_widgets[chann].decimated_power, channel,
				"decimated_power", widget_str, 1);

	sprintf(widget_str, "rssi_rx%d", chann + 1);
	adrv9002_gtk_label_init(priv, &priv->rx_widgets[chann].rssi, channel, "rssi",
				widget_str, 1);

	sprintf(widget_str, "sampling_rate_rx%d", chann + 1);
	adrv9002_gtk_label_init(priv, &priv->rx_widgets[chann].rx.sampling_rate, channel,
				"sampling_frequency", widget_str, 1000000);

	sprintf(widget_str, "bandwidth_rx%d", chann + 1);
	adrv9002_gtk_label_init(priv, &priv->rx_widgets[chann].rx.rf_bandwidth, channel,
				"rf_bandwidth", widget_str, 1000000);

	sprintf(widget_str, "frame_rx%d", chann + 1);
	adrv9002_check_channel_status(priv, channel, &priv->rx_widgets[chann].rx,
				      widget_str);

	return 0;
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
		else {
			printf("unhandled widget type, attribute: %s\n",
			       widgets[i].attr_name);
			return;
		}

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

static void connect_special_signal_widgets(struct plugin_private *priv, const int chann)
{
	/* rx gain handling */
	g_signal_connect(G_OBJECT(priv->rx_widgets[chann].rx.gain_ctrl.w.widget),
			 "changed", G_CALLBACK(save_gain_ctl),
			 &priv->rx_widgets[chann].rx);
	g_signal_connect(G_OBJECT(priv->rx_widgets[chann].rx.gain.widget),
			 "value-changed", G_CALLBACK(save_gain_value),
			 &priv->rx_widgets[chann].rx);
	/* nco freq */
	g_signal_connect(G_OBJECT(priv->rx_widgets[chann].rx.nco_freq.widget),
			 "value-changed", G_CALLBACK(save_widget_value),
			 &priv->rx_widgets[chann].rx.nco_freq);
	/* ensm mode and port en */
	g_signal_connect(G_OBJECT(priv->rx_widgets[chann].rx.ensm.w.widget),
			 "changed", G_CALLBACK(combo_box_save),
			 &priv->rx_widgets[chann].rx.ensm);
	g_signal_connect(G_OBJECT(priv->rx_widgets[chann].rx.port_en.w.widget),
			 "changed", G_CALLBACK(save_port_en),
			 &priv->rx_widgets[chann].rx);
	/* digital gain control */
	g_signal_connect(G_OBJECT(priv->rx_widgets[chann].intf_gain.w.widget),
			 "changed", G_CALLBACK(save_intf_gain),
			 &priv->rx_widgets[chann]);
	g_signal_connect(G_OBJECT(priv->rx_widgets[chann].digital_gain_ctl.w.widget),
			 "changed", G_CALLBACK(save_digital_gain_ctl),
			 &priv->rx_widgets[chann]);
	/* tx atten handling */
	g_signal_connect(G_OBJECT(priv->tx_widgets[chann].gain_ctrl.w.widget),
			 "changed", G_CALLBACK(save_gain_ctl),
			 &priv->tx_widgets[chann]);
	/* nco freq */
	g_signal_connect(G_OBJECT(priv->tx_widgets[chann].nco_freq.widget),
			 "value-changed", G_CALLBACK(save_widget_value),
			 &priv->tx_widgets[chann].nco_freq);
	g_signal_connect(G_OBJECT(priv->tx_widgets[chann].gain.widget),
			 "value-changed", G_CALLBACK(save_widget_value),
			 &priv->tx_widgets[chann].gain);
	/* ensm mode and port en */
	g_signal_connect(G_OBJECT(priv->tx_widgets[chann].ensm.w.widget),
			 "changed", G_CALLBACK(combo_box_save),
			 &priv->tx_widgets[chann].ensm);
	g_signal_connect(G_OBJECT(priv->tx_widgets[chann].port_en.w.widget),
			 "changed", G_CALLBACK(save_port_en),
			 &priv->tx_widgets[chann]);
}

static int adrv9002_adc_get_name(struct plugin_private *priv)
{
	GArray *devices;
	struct iio_device *adc;
	unsigned int i;

	devices = get_iio_devices_starting_with(priv->ctx, CAP_DEVICE);
	if (devices->len < 1 || devices->len > 2) {
		printf("Warning: Got %d CAP devices. We should have 1 or 2\n",
		       devices->len);
		g_array_free(devices, FALSE);
		return -ENODEV;
	}

	for (i = 0; i < devices->len; i++) {
		adc = g_array_index(devices, struct iio_device*, i);
		priv->adc_name[i] = iio_device_get_name(adc);
		priv->n_adcs++;
	}

	g_array_free(devices, FALSE);
	return 0;
}

static int adrv9002_dds_init(struct plugin_private *priv)
{
	GArray *devices;
	struct iio_device *dac;
	int i, ret;

	devices = get_iio_devices_starting_with(priv->ctx, DDS_DEVICE);

	if (devices->len < 1 || devices->len > 2) {
		printf("Warning: Got %d DDS devices. We should have 1 or 2\n",
		       devices->len);
		g_array_free(devices, FALSE);
		return -ENODEV;
	}

	for (i = 0; i < (int)devices->len; i++) {
		GtkWidget *dds_container;
		double dac_tx_sampling_freq = 0;
		struct iio_channel *ch0;
		long long dac_freq = 0;
		const char *dds_str;

		dac = g_array_index(devices, struct iio_device*, i);
		priv->dac_manager[i].dac_name = iio_device_get_name(dac);
		/*
		 * Make sure the dds gets into a consistent block with the rest of the plugin
		 * (dds1 aligned with TX1 and dds2 with TX2). @iio_device_get_name() returns a
		 * NULL terminated string that is __at least__ the size of @DDS_DEVICE
		 * (via @get_iio_devices_starting_with). Hence, even if the sizes are the same,
		 * the next condition should be fine as we will be calling @isdigit() on the NULL
		 * terminating character (which is not a digit and not out of bounds)
		 */
		if (!isdigit(priv->dac_manager[i].dac_name[strlen(DDS_DEVICE)]))
			dds_str = "dds_transmit_block";
		else
			dds_str = "dds_transmit_block1";

		priv->dac_manager[i].dac_tx_manager = dac_data_manager_new(dac, NULL, priv->ctx);
		if (!priv->dac_manager[i].dac_tx_manager) {
			printf("%s: Failed to start dac Manager...\n",
			       priv->dac_manager[i].dac_name);
			g_array_free(devices, FALSE);
			ret = -EFAULT;
			goto free_dac;
		}

		dds_container = GTK_WIDGET(gtk_builder_get_object(priv->builder, dds_str));
		gtk_container_add(GTK_CONTAINER(dds_container),
				  dac_data_manager_get_gui_container(priv->dac_manager[i].dac_tx_manager));
		gtk_widget_show_all(dds_container);

		ch0 = iio_device_find_channel(dac, "altvoltage0", true);
		if (!ch0) {
			printf("%s: Cannot get dac channel 0\n",
			       priv->dac_manager[i].dac_name);
			g_array_free(devices, FALSE);
			ret = -EFAULT;
			goto free_dac;
		}
		priv->dac_manager[i].ch0 = ch0;
		if (iio_channel_attr_read_longlong(ch0, "sampling_frequency", &dac_freq) == 0)
			dac_tx_sampling_freq = (double)dac_freq / 1000000ul;

		dac_data_manager_freq_widgets_range_update(priv->dac_manager[i].dac_tx_manager,
							   dac_tx_sampling_freq / 2);
		dac_data_manager_set_buffer_size_alignment(priv->dac_manager[i].dac_tx_manager, 64);
		dac_data_manager_set_buffer_chooser_current_folder(priv->dac_manager[i].dac_tx_manager,
								   OSC_WAVEFORM_FILE_PATH);
		priv->n_dacs++;
	}

	g_array_free(devices, FALSE);
	if (i == NUM_MAX_DDS)
		return 0;

	/* hide second dds */
	gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(priv->builder,
							  "dds_transmit_block1")));

	return 0;

free_dac:
	while (--i >= 0)
		dac_data_manager_free(priv->dac_manager[i].dac_tx_manager);

	return ret;
}

static GtkWidget *adrv9002_init(struct osc_plugin *plugin, GtkWidget *notebook,
				const char *ini_fn)
{
	GtkWidget *adrv9002_panel;
	struct plugin_private *priv = plugin->priv;
	const char *dev_name = g_list_first(priv->plugin_ctx.required_devices)->data;
	int i, ret;
	GtkWidget *global, *rx, *tx, *fpga;
	GtkToggleToolButton *global_btn, *rx_btn, *tx_btn, *fpga_btn;
	GtkButton *reload_btn;
	struct iio_channel *temp;

	priv->builder = gtk_builder_new();
	if (!priv->builder)
		goto error_free_priv;

	priv->ctx = osc_create_context();
	if (!priv->ctx)
		goto error_free_priv;

	priv->adrv9002 = iio_context_find_device(priv->ctx, dev_name);
	if (!priv->adrv9002) {
		printf("Could not find iio device:%s\n", dev_name);
		goto error_free_ctx;
	}

	if (osc_load_glade_file(priv->builder, "adrv9002") < 0)
		goto error_free_ctx;

	priv->nbook = GTK_NOTEBOOK(notebook);
	adrv9002_panel = GTK_WIDGET(gtk_builder_get_object(priv->builder,
							   "adrv9002_panel"));
	if (!adrv9002_panel)
		goto error_free_ctx;

	for (i = 0; i < ADRV9002_NUM_CHANNELS; i++) {
		ret = adrv9002_rx_widgets_init(priv, i);
		if (ret)
			goto error_free_ctx;

		ret = adrv9002_tx_widgets_init(priv, i);
		if (ret)
			goto error_free_ctx;
	}
	/* handle sections buttons and reload settings */
	global = GTK_WIDGET(gtk_builder_get_object(priv->builder, "global_settings"));
	global_btn = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(priv->builder,
								   "global_settings_toggle"));
	rx = GTK_WIDGET(gtk_builder_get_object(priv->builder, "rx_settings"));
	rx_btn = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(priv->builder,
							       "rx_toggle"));
	tx = GTK_WIDGET(gtk_builder_get_object(priv->builder, "tx_settings"));
	tx_btn = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(priv->builder,
							       "tx_toggle"));
	fpga = GTK_WIDGET(gtk_builder_get_object(priv->builder, "fpga_settings"));
	fpga_btn = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(priv->builder,
								 "fpga_toggle"));

	reload_btn = GTK_BUTTON(gtk_builder_get_object(priv->builder,
						       "settings_reload"));

	g_signal_connect(G_OBJECT(global_btn), "clicked", G_CALLBACK(handle_section_cb),
			 global);
	g_signal_connect(G_OBJECT(rx_btn), "clicked", G_CALLBACK(handle_section_cb),
			 rx);
	g_signal_connect(G_OBJECT(tx_btn), "clicked", G_CALLBACK(handle_section_cb),
			 tx);
	g_signal_connect(G_OBJECT(fpga_btn), "clicked", G_CALLBACK(handle_section_cb),
			 fpga);

	g_signal_connect(G_OBJECT(reload_btn), "clicked", G_CALLBACK(reload_settings),
			 priv);
	/* load profile cb */
	g_builder_connect_signal(priv->builder, "profile_config", "file-set",
	                         G_CALLBACK(load_profile), priv);

	gtk_file_chooser_set_current_folder(
		GTK_FILE_CHOOSER(gtk_builder_get_object(priv->builder, "profile_config")),
		OSC_FILTER_FILE_PATH"/adrv9002");

	/* load stream cb */
	g_builder_connect_signal(priv->builder, "stream_config", "file-set",
	                         G_CALLBACK(load_stream), priv);

	gtk_file_chooser_set_current_folder(
		GTK_FILE_CHOOSER(gtk_builder_get_object(priv->builder, "stream_config")),
		OSC_FILTER_FILE_PATH"/adrv9002");

	/* init temperature label */
	temp = iio_device_find_channel(priv->adrv9002, "temp0", false);
	if (!temp)
		goto error_free_ctx;

	adrv9002_gtk_label_init(priv, &priv->temperature, temp, "input", "temperature", 1000);

	/* init dds container */
	ret = adrv9002_dds_init(priv);
	if (ret)
		goto error_free_ctx;

	ret = adrv9002_adc_get_name(priv);
	if (ret)
		goto error_free_ctx;

	/* update widgets and connect signals */
	for (i = 0; i < ADRV9002_NUM_CHANNELS; i++) {
		connect_special_signal_widgets(priv, i);
		adrv9002_update_rx_widgets(priv, i);
		adrv9002_update_tx_widgets(priv, i);
		make_widget_update_signal_based(priv->rx_widgets[i].rx.w,
						priv->rx_widgets[i].rx.num_widgets);
		make_widget_update_signal_based(priv->tx_widgets[i].w,
						priv->tx_widgets[i].num_widgets);
	}
	/* update dac */
	for (i = 0; i < priv->n_dacs; i++)
		dac_data_manager_update_iio_widgets(priv->dac_manager[i].dac_tx_manager);

	priv->refresh_timeout = g_timeout_add(1000, (GSourceFunc)update_display,
					      priv);
	return adrv9002_panel;

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

static void adrv9002_get_preferred_size(const struct osc_plugin *plugin,
					int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	struct plugin_private *priv = plugin->priv;
	int i;

	osc_plugin_context_free_resources(&priv->plugin_ctx);
	osc_destroy_context(priv->ctx);

	for (i = 0; i < priv->n_dacs; i++) {
		dac_data_manager_free(priv->dac_manager[i].dac_tx_manager);
	}

	g_source_remove(priv->refresh_timeout);
	g_free(priv);
}

GSList* get_dac_dev_names(const struct osc_plugin *plugin)
{
	struct plugin_private *priv = plugin->priv;
	GSList *list = NULL;
	int i;

	for(i = 0; i < priv->n_dacs; i++) {
		if (priv->dac_manager[i].dac_name)
			list = g_slist_append(list, (gpointer)priv->dac_manager[i].dac_name);
	}

	return list;
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
	plugin->init = adrv9002_init;
	plugin->get_preferred_size = adrv9002_get_preferred_size;
	plugin->update_active_page = update_active_page;
	plugin->destroy = context_destroy;
	plugin->get_dac_dev_names = get_dac_dev_names;

	return plugin;
}

int iio_device_cmp_by_name(gconstpointer a, gconstpointer b)
{
	const char *str_a = iio_device_get_name(*(struct iio_device **)a);
	const char *str_b = iio_device_get_name(*(struct iio_device **)b);

	return g_strcmp0(str_a, str_b);
}

GArray* get_data_for_possible_plugin_instances(void)
{
	GArray *data = g_array_new(FALSE, TRUE,
				   sizeof(struct osc_plugin_context *));
	struct iio_context *osc_ctx = get_context_from_osc();
	GArray *devices = get_iio_devices_starting_with(osc_ctx, PHY_DEVICE);
	guint i = 0;

	g_array_sort(devices, iio_device_cmp_by_name);

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
