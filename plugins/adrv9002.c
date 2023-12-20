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
#include <math.h>
#include <unistd.h>

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
#define NUM_MAX_ORX_WIDGETS 3
#define NUM_MAX_DDS	2
#define NUM_MAX_ADC	2
#define NUM_DEVICE_MAX_WIDGETS 1

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

const gdouble mhz_scale = 1000000.0;

#define BBDC_LOOP_GAIN_RES	2147483648U
static const gdouble bbdc_adjust_min = 1.0 / BBDC_LOOP_GAIN_RES;
static const gdouble bbdc_adjust_max = 1.0 / BBDC_LOOP_GAIN_RES * UINT32_MAX;

struct adrv9002_gtklabel {
	GtkLabel *labels;
	struct iio_channel *chann;
	const char *iio_attr;
	const char *label_str;
	int scale;
};

struct adrv9002_common {
	struct plugin_private *priv;
	struct iio_widget gain_ctrl;
	struct iio_widget gain;
	struct iio_widget nco_freq;
	struct iio_widget carrier;
	struct iio_widget ensm;
	struct iio_widget port_en;
	struct adrv9002_gtklabel rf_bandwidth;
	struct adrv9002_gtklabel sampling_rate;
	/* these are generic widgets that don't need any special attention */
	struct iio_widget w[NUM_MAX_WIDGETS];
	uint16_t num_widgets;
	bool enabled;
	uint8_t idx;
};

struct adrv9002_rx {
	struct adrv9002_common rx;
	struct iio_widget digital_gain_ctl;
	struct iio_widget intf_gain;
	struct adrv9002_gtklabel rssi;
	struct adrv9002_gtklabel decimated_power;
};

struct adrv9002_orx {
	struct iio_widget w[NUM_MAX_ORX_WIDGETS];
	struct iio_widget orx_en;
	struct plugin_private *priv;
	bool enabled;
	uint16_t num_widgets;
	uint8_t idx;
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
	struct iio_widget device_w[NUM_DEVICE_MAX_WIDGETS];
	int num_widgets;
	/* rx */
	struct adrv9002_rx rx_widgets[ADRV9002_NUM_CHANNELS];
	/* tx */
	struct adrv9002_common tx_widgets[ADRV9002_NUM_CHANNELS];
	int n_txs;
	/* orx */
	struct adrv9002_orx orx_widgets[ADRV9002_NUM_CHANNELS];
	/* dac */
	struct adrv9002_dac_mgmt dac_manager[NUM_MAX_DDS];
	int n_dacs;
	/* adc */
	const char *adc_name[NUM_MAX_ADC];
	int n_adcs;
};

#define dialog_box_message(widget, title, level, msg) { 				\
	GtkWidget *toplevel = gtk_widget_get_toplevel(widget);				\
											\
	if (gtk_widget_is_toplevel(toplevel)) {						\
		GtkWidget *dialog;							\
		const char *icon;							\
											\
		dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(toplevel),	\
					GTK_DIALOG_DESTROY_WITH_PARENT,			\
					level, GTK_BUTTONS_CLOSE,			\
					msg);						\
											\
		gtk_window_set_title(GTK_WINDOW(dialog), title);			\
		if (level == GTK_MESSAGE_INFO)						\
			icon = "dialog-information-symbolic";				\
		else									\
			icon = "dialog-error-symbolic";					\
		gtk_window_set_icon_name(GTK_WINDOW(dialog), icon);			\
		gtk_dialog_run(GTK_DIALOG(dialog));					\
		gtk_widget_destroy (dialog);						\
	} else {									\
		printf("Cannot display dialog: Toplevel wigdet not found\n");		\
	}										\
}

#define dialog_box_message_error(widget, title, msg, ...) \
	dialog_box_message(widget, title, GTK_MESSAGE_ERROR, msg)

#define dialog_box_message_info(widget, title, msg, ...) \
	dialog_box_message(widget, title, GTK_MESSAGE_INFO, msg)

static void save_gain_ctl(GtkWidget *widget, struct adrv9002_common *chann)
{
	char *gain_ctl;

	iio_widget_save_block_signals_by_data(&chann->gain_ctrl);

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
		iio_widget_update_block_signals_by_data(&chann->gain);
	}

	g_free(gain_ctl);
}

static void save_intf_gain(GtkWidget *widget, struct adrv9002_rx *rx)
{
	char *ensm = gtk_combo_box_text_get_active_text(
			GTK_COMBO_BOX_TEXT(rx->rx.ensm.widget));

	if (ensm && strcmp(ensm, "rf_enabled")) {
		dialog_box_message_error(widget, "Interface Gain Set Failed",
					 "ENSM must be rf_enabled to change the interface gain");
		iio_widget_update_block_signals_by_data(&rx->intf_gain);
	} else {
		iio_widget_save_block_signals_by_data(&rx->intf_gain);
	}

	g_free(ensm);
}

static void save_digital_gain_ctl(GtkWidget *widget, struct adrv9002_rx *rx)
{
	char *digital_gain;

	iio_widget_save_block_signals_by_data(&rx->digital_gain_ctl);
	digital_gain = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));

	if (digital_gain && strcmp(digital_gain, "spi")) {
		gtk_widget_set_sensitive(rx->intf_gain.widget, false);
	} else {
		gtk_widget_set_sensitive(rx->intf_gain.widget, true);
		iio_widget_update_block_signals_by_data(&rx->intf_gain);
	}

	g_free(digital_gain);
}

static void orx_control_track_cals(const struct adrv9002_common *chan, bool en)
{
	int i = 0;

	if (!chan->enabled)
		return;

	for (i = 0; i < chan->num_widgets; i++) {
		if (!strstr(chan->w[i].attr_name, "_tracking_en"))
			continue;
		gtk_widget_set_sensitive(chan->w[i].widget, en);
	}
}

static void orx_control_tx_widgets_visibility(const struct adrv9002_common *tx, bool en)
{
	int i = 0;

	/* Disable all tx controls that might affect tx ensm state if @en=true */
	gtk_widget_set_sensitive(tx->carrier.widget, en);
	gtk_widget_set_sensitive(tx->ensm.widget, en);
	gtk_widget_set_sensitive(tx->port_en.widget, en);
	gtk_widget_set_sensitive(tx->gain_ctrl.widget, en);

	for (i = 0; i < tx->num_widgets; i++) {
		if (strcmp(tx->w[i].attr_name, "en"))
			continue;
		gtk_widget_set_sensitive(tx->w[i].widget, en);
	}
}

static void orx_control_track_cal_visibility(const struct adrv9002_orx *orx, bool en)
{
	int i;
	int other = ~orx->idx & 0x1;
	const struct adrv9002_orx *orx_other = &orx->priv->orx_widgets[other];
	gboolean wired;

	/*
	 * The thing with tracking cals is that, due to the way the device driver API/Firmware
	 * is designed, we need to move all 4 ports to calibrated state to change a cal
	 * (even if we are only changing cals in one specific port). Hence, we need to make
	 * sure that if one of the ORxs is enabled, we block all the tracking calibrations
	 * controls in all enabled ports. The driver would not allow it anyways, so this way,
	 * we make it explicit to the user that this is not permitted.
	 */
	if (!en) {
		wired = false;
	} else {
		/*
		 * this check is needed for adrv9003 where ORX2 is never enabled and the
		 * widgets are never initialized for it.
		 */
		if (orx_other->enabled)
			wired = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(orx_other->orx_en.widget));
		else
			wired = true;
	}

	for (i = 0; i < orx->num_widgets; i++) {
		if (strcmp(orx->w[i].attr_name, "orx_quadrature_w_poly_tracking_en"))
			continue;
		gtk_widget_set_sensitive(orx->w[i].widget, wired);
		if (orx_other->enabled)
			gtk_widget_set_sensitive(orx_other->w[i].widget, wired);
		break;
	}

	/* control track calls on both RXs */
	orx_control_track_cals(&orx->priv->rx_widgets[other].rx, wired);
	orx_control_track_cals(&orx->priv->rx_widgets[orx->idx].rx, wired);
	/* control track calls on both TXs */
	orx_control_track_cals(&orx->priv->tx_widgets[other], wired);
	orx_control_track_cals(&orx->priv->tx_widgets[orx->idx], wired);
}

static void orx_control_rx_widgets_visibility(const struct adrv9002_common *rx, bool en)
{
	char rx_str[32];
	GtkWidget *rx_frame;

	/* Just disable all the RX frame as it does not make sense to control if @en=true*/
	sprintf(rx_str, "frame_rx%d", rx->idx + 1);
	rx_frame = GTK_WIDGET(gtk_builder_get_object(rx->priv->builder, rx_str));
	if (rx->enabled && rx_frame)
		gtk_widget_set_sensitive(rx_frame, en);
}

static void save_orx_powerdown(GtkWidget *widget, struct adrv9002_orx *orx)
{
	struct adrv9002_rx *rx = &orx->priv->rx_widgets[orx->idx];
	struct adrv9002_common *tx = &orx->priv->tx_widgets[orx->idx];
	GtkWidget *rx_ensm = rx->rx.ensm.widget;
	GtkWidget *tx_ensm = tx->ensm.widget;
	char *r_ensm = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(rx_ensm));
	char *t_ensm = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(tx_ensm));
	bool en = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

	/*
	 * The way ORx is supposed to work is to enable it only if RX is not in rf_enabled
	 * and TX __is__ in rf_enabled state. After that point we should not really touch any
	 * of the RX controls as it might trigger some state change that could break the Orx
	 * capture. For TX, we are also not supposed to do any state change on the port as some
	 * state transitions break ORx and we can't recover from it without toggling the ORx button.
	 */
	if (rx->rx.enabled && r_ensm && !strcmp(r_ensm, "rf_enabled") && !en) {
		dialog_box_message_error(widget, "ORX Enable failed",
					 "RX ENSM cannot be in rf_enabled in order to enable ORX");
		/* restore widget value */
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), true);
	} else if (t_ensm && strcmp(t_ensm, "rf_enabled") && !en) {
		dialog_box_message_error(widget, "ORX Enable failed",
					 "TX ENSM must be in rf_enabled in order to enable ORX");
		/* restore widget value */
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), true);
	} else {
		iio_widget_save_block_signals_by_data(&orx->orx_en);
		/* let's get the value again to make sure it is the most up to date */
		en = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		orx_control_rx_widgets_visibility(&rx->rx, en);
		orx_control_tx_widgets_visibility(tx, en);
		orx_control_track_cal_visibility(orx, en);
	}

	g_free(r_ensm);
	g_free(t_ensm);
}

static void save_ensm(GtkWidget *w, struct iio_widget *widget)
{
	widget->save(widget);
	/*
	 * If it is a transition to rf_enabled, it can take some time and so, we
	 * can still get the old value if we do not wait a bit...
	 */
	usleep(2000);
	iio_widget_update_block_signals_by_data(widget);
}

static void save_port_en(GtkWidget *widget, struct adrv9002_common *chann)
{
	char *port_en;

	iio_widget_save_block_signals_by_data(&chann->port_en);
	port_en = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(widget));

	if (port_en && strcmp(port_en, "spi")) {
		gtk_widget_set_sensitive(chann->ensm.widget, false);
	} else {
		gtk_widget_set_sensitive(chann->ensm.widget, true);
		iio_widget_update_block_signals_by_data(&chann->ensm);
	}

	g_free(port_en);
}

static void adrv9002_save_carrier_freq(GtkWidget *widget, struct adrv9002_common *chan)
{
	struct plugin_private *priv = chan->priv;
	int other = ~chan->idx & 0x1;
	/* we can use whatever attr as the iio channel is the same (naturally not for the LOs) */
	bool tx = iio_channel_is_output(chan->ensm.chn);

	chan->carrier.save(&chan->carrier);
	/*
	 * Carriers are only moved together for ports of the same type so just update the
	 * value of the @other channel. In cases like TDD, that won't be the case so we could
	 * actually skip updating the widget but doing it unconditionally it's just much
	 * simpler and allow us to drop all the code to keep the LO mappings.
	 */
	if (tx && other < priv->n_txs)
		iio_widget_update_block_signals_by_data(&priv->tx_widgets[other].carrier);
	else
		iio_widget_update_block_signals_by_data(&priv->rx_widgets[other].rx.carrier);

	iio_widget_update_block_signals_by_data(&chan->carrier);
}

static void adrv9002_show_help(GtkWidget *widget, void *unused)
{
	dialog_box_message_info(widget, "Initial Calibrations Help",
"<b>off:</b> Initial calibrations won't run automatically.\n"
"<b>auto:</b> Initial calibrations will run automatically for Carrier changes bigger or equal to 100MHz.\n\n"
"<b>To manually run the calibrations, press the \"Calibrate now\" button!</b>");
}

static void adrv9002_run_cals(GtkWidget *widget, struct plugin_private *priv)
{
	ssize_t ret;

	ret = iio_device_attr_write(priv->adrv9002, "initial_calibrations", "run");
	if (ret < 0)
		dialog_box_message_error(widget, "Initial Calibrations",
					 "Failed to re-run Initial Calibrations");
}

static double adrv9002_bbdc_loop_gain_convert(double val, bool updating)
{
	double loop_gain;

	if (updating)
		loop_gain = val / BBDC_LOOP_GAIN_RES;
	else
		loop_gain = round(val * BBDC_LOOP_GAIN_RES);

	return loop_gain;
}

static void handle_section_cb(GtkToggleToolButton *btn, GtkWidget *section)
{
	GtkWidget *toplevel;

	if (gtk_toggle_tool_button_get_active(btn)) {
		g_object_set(G_OBJECT(btn), "stock-id", "gtk-go-down", NULL);
		gtk_widget_show(section);
	} else {
		g_object_set(G_OBJECT(btn), "stock-id", "gtk-go-up", NULL);
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
		GTK_COMBO_BOX_TEXT(chann->gain_ctrl.widget));
	char *port_en = gtk_combo_box_text_get_active_text(
		GTK_COMBO_BOX_TEXT(chann->port_en.widget));

	if (gain_ctl && strcmp(gain_ctl, "spi"))
		iio_widget_update_block_signals_by_data(&chann->gain);

	if (port_en && strcmp(port_en, "spi"))
		iio_widget_update_block_signals_by_data(&chann->ensm);

	g_free(gain_ctl);
	g_free(port_en);
}

static void update_special_rx_widgets(struct adrv9002_rx *rx, const int n_widgets)
{
	int i;

	for (i = 0; i < n_widgets; i++) {
		char *digital_gain = gtk_combo_box_text_get_active_text(
			GTK_COMBO_BOX_TEXT(rx[i].digital_gain_ctl.widget));

		if (!rx[i].rx.enabled)
			goto nex_widget;

		update_label(&rx[i].rssi);
		update_label(&rx[i].decimated_power);
		update_special_widgets(&rx[i].rx);

		if (digital_gain && strstr(digital_gain, "automatic"))
			iio_widget_update_block_signals_by_data(&rx[i].intf_gain);
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

static void adrv9002_update_orx_widgets(struct plugin_private *priv, const int chann)
{
	struct adrv9002_orx *orx = &priv->orx_widgets[chann];
	int ret;
	long long dummy;
	char label[32];

	if (!orx->enabled) {
		/* make sure the widget is initialized */
		if (chann >= priv->n_txs)
			return;
		/*
		 * This will make sure that we restore TX/RX widgets sensitivity if we had ORx
		 * enabled before updating the profile
		 */
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(orx->orx_en.widget), true);
		return;
	}

	/* can we actually enable/disable orx?! */
	sprintf(label, "powerdown_en_label_orx%d", orx->idx + 1);
	ret = iio_channel_attr_read_longlong(orx->orx_en.chn, "orx_en", &dummy);
	/*
	 * Lets' hide as this is not really supposed to change at runtime. However, we need
	 * to do the check here as at the plugin initialization, the device might not have a
	 * profile supporting ORx and in that case, we cannot really know if orx_en is supported
	 * or not as the return val would be ENODEV...
	 */
	if (ret == -ENOTSUPP) {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(priv->builder, label)));
		gtk_widget_hide(orx->orx_en.widget);
	} else {
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(priv->builder, label)));
		gtk_widget_show(orx->orx_en.widget);
		iio_widget_update(&orx->orx_en);
	}

	/* generic widgets */
	iio_update_widgets_block_signals_by_data(orx->w, orx->num_widgets);
}

static void adrv9002_update_rx_widgets(struct plugin_private *priv, const int chann)
{
	struct adrv9002_rx *rx = &priv->rx_widgets[chann];

	/* rx */
	if (!rx->rx.enabled)
		return;

	iio_widget_update_block_signals_by_data(&rx->rx.gain_ctrl);
	iio_widget_update_block_signals_by_data(&rx->rx.gain);
	iio_widget_update_block_signals_by_data(&rx->rx.nco_freq);
	iio_widget_update_block_signals_by_data(&rx->rx.ensm);
	iio_widget_update_block_signals_by_data(&rx->rx.port_en);
	iio_widget_update_block_signals_by_data(&rx->digital_gain_ctl);
	iio_widget_update_block_signals_by_data(&rx->intf_gain);
	iio_widget_update_block_signals_by_data(&rx->rx.carrier);
	/* generic widgets */
	iio_update_widgets_block_signals_by_data(rx->rx.w, rx->rx.num_widgets);
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

	iio_widget_update_block_signals_by_data(&tx->gain_ctrl);
	iio_widget_update_block_signals_by_data(&tx->gain);
	iio_widget_update_block_signals_by_data(&tx->nco_freq);
	iio_widget_update_block_signals_by_data(&tx->carrier);
	iio_widget_update_block_signals_by_data(&tx->ensm);
	iio_widget_update_block_signals_by_data(&tx->port_en);
	/* generic widgets */
	iio_update_widgets_block_signals_by_data(tx->w, tx->num_widgets);
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

static void adrv9002_profile_read(struct plugin_private *priv)
{
	char profile[512];
	ssize_t ret;
	GtkLabel *label = GTK_LABEL(gtk_builder_get_object(priv->builder, "profile_config_read"));

	ret = iio_device_attr_read(priv->adrv9002, "profile_config", profile, sizeof(profile));
	if (ret < 0)
		strcpy(profile, "error\n");

	gtk_label_set_text(label, profile);
}

static void adrv9002_check_orx_status(struct plugin_private *priv, struct adrv9002_orx *orx)
{
	int ret;
	double dummy;
	char gtk_str[32];

	sprintf(gtk_str, "frame_orx%d", orx->idx + 1);
	ret = iio_channel_attr_read_double(orx->w[0].chn, "orx_hardwaregain", &dummy);
	if (ret == -ENODEV) {
		orx->enabled = false;
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(priv->builder, gtk_str)));
	} else {
		orx->enabled = true;
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(priv->builder, gtk_str)));
	}
}

static void adrv9002_check_channel_status(struct plugin_private *priv,
					  struct adrv9002_common *chan,
					  const char *gtk_str)
{
	int ret;
	double dummy;

	/*
	 * We use this attr to check if the channel is enabled or not since it should only
	 * return error if the channel is disabled (assuming there's nothing seriously
	 * wrong with the device). We can also just use the iio channel from the first
	 * widget as the channel is the same for all widgets on this port...
	 */
	ret = iio_channel_attr_read_double(chan->w[0].chn, "rf_bandwidth", &dummy);
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
	struct adrv9002_common *c;

	if (tx) {
		c = &priv->tx_widgets[channel];
		sprintf(gtk_str, "nco_freq_tx%d", channel + 1);
		sprintf(label_str, "nco_label_tx%d", channel + 1);
	} else {
		c = &priv->rx_widgets[channel].rx;
		sprintf(gtk_str, "nco_freq_rx%d", channel + 1);
		sprintf(label_str, "nco_label_rx%d", channel + 1);
	}

	/* nothing to do if the port is already disabled */
	if (!c->enabled)
		return;

	ret = iio_channel_attr_read_longlong(c->nco_freq.chn, "nco_frequency", &dummy);
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

static void adrv9002_update_rx_intf_gain_attr_available(struct adrv9002_rx *rx)
{
	struct iio_widget *w = &rx->intf_gain;
	gchar **saved_list, **available;
	char text[512];
	int ret;

	ret = iio_channel_attr_read(w->chn, w->attr_name_avail, text,
				    sizeof(text));
	if (ret < 0)
		return;

	/*
	 * We need to block the signals to avoid multiple calls to save_intf_gain() triggered
	 * by removing the elements (number of calls depends on the active value/index). Note
	 * there are other widgets sharing @rx as signal data but we are safe in still blocking
	 * all the signals for it at this stage. We should only get here when the 'reload_settings'
	 * button is pressed or on profile load.
	 */
	g_signal_handlers_block_matched(G_OBJECT(w->widget), G_SIGNAL_MATCH_DATA, 0, 0,
					NULL, NULL, rx);

	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(w->widget));

	g_signal_handlers_unblock_matched(G_OBJECT(w->widget), G_SIGNAL_MATCH_DATA, 0, 0,
					  NULL, NULL, rx);

	available = saved_list = g_strsplit(text, " ", 0);

	/* our available is static so we can just set it here once */
	for (; *available; available++) {
		if (*available[0] == '\0')
			continue;
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(w->widget),
					       *available);
	}

	g_strfreev(saved_list);
}

static void update_all(struct plugin_private *priv)
{
	int i;
	char gtk_str[32];

	for(i = 0; i < ADRV9002_NUM_CHANNELS; i++) {
		sprintf(gtk_str, "frame_rx%d", i + 1);
		adrv9002_check_channel_status(priv, &priv->rx_widgets[i].rx, gtk_str);
		adrv9002_check_nco_freq_support(priv, i, false);
		/* intf gain available value might change on profile load */
		adrv9002_update_rx_intf_gain_attr_available(&priv->rx_widgets[i]);
		adrv9002_update_rx_widgets(priv, i);

		if (i >= priv->n_txs)
			continue;

		adrv9002_check_orx_status(priv, &priv->orx_widgets[i]);
		adrv9002_update_orx_widgets(priv, i);
		sprintf(gtk_str, "frame_tx%d", i + 1);
		adrv9002_check_channel_status(priv, &priv->tx_widgets[i], gtk_str);
		adrv9002_check_nco_freq_support(priv, i, true);
		adrv9002_update_tx_widgets(priv, i);
	}

	adrv9002_profile_read(priv);
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
	dialog_box_message_error(GTK_WIDGET(chooser), "Stream Loading Failed",
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
	/* update widgets*/
	update_all(priv);
	/* re-arm the timer */
	priv->refresh_timeout = g_timeout_add(1000, (GSourceFunc)update_display,
					      priv);
	return;
err:
	g_free(file_name);
	dialog_box_message_error(GTK_WIDGET(chooser), "Profile Configuration Failed",
				 "Failed to load profile using the selected file!");

	if (priv->last_profile[0])
		gtk_file_chooser_set_filename(chooser, priv->last_profile);
	else
		gtk_file_chooser_set_filename(chooser, "(None)");
}

static void adrv9002_combo_box_init(struct iio_widget *combo, const char *w_str,
				    const char *attr, const char *attr_avail,
				    struct plugin_private *priv, struct iio_channel *chann)
{
	iio_combo_box_init_no_avail_flush_from_builder(combo, priv->adrv9002, chann, attr,
						       attr_avail, priv->builder, w_str, NULL);
}

static int adrv9002_tx_widgets_init(struct plugin_private *priv, const int chann)
{
	struct iio_channel *channel, *tx_lo;
	char chann_str[32];
	char widget_str[256];
	const char *lo_attr = chann ? "TX2_LO_frequency" : "TX1_LO_frequency";
	uint16_t *n_w = &priv->tx_widgets[chann].num_widgets;

	if (chann >= priv->n_txs)
		return 0;

	sprintf(chann_str, "voltage%d", chann);
	channel = iio_device_find_channel(priv->adrv9002, chann_str, true);
	if (!channel)
		return -ENODEV;

	/* LO goes from 0 to 3, the first 2 for RX and the other for TX */
	sprintf(chann_str, "altvoltage%d", chann + 2);
	tx_lo = iio_device_find_channel(priv->adrv9002, chann_str, true);
	if (!tx_lo)
		return -ENODEV;

	priv->tx_widgets[chann].priv = priv;
	priv->tx_widgets[chann].idx = chann;
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

	sprintf(widget_str, "hardware_gain_tx%d", chann + 1);
	iio_spin_button_init_from_builder(&priv->tx_widgets[chann].gain,
					  priv->adrv9002, channel,
					  "hardwaregain",
					  priv->builder, widget_str, NULL);

	sprintf(widget_str, "lo_freq_tx%d", chann + 1);
	iio_spin_button_int_init_from_builder(&priv->tx_widgets[chann].carrier,
					      priv->adrv9002, tx_lo, lo_attr,
					      priv->builder, widget_str, &mhz_scale);

	sprintf(widget_str, "sampling_rate_tx%d", chann + 1);
	adrv9002_gtk_label_init(priv, &priv->tx_widgets[chann].sampling_rate, channel,
				"sampling_frequency", widget_str, 1000000);

	sprintf(widget_str, "bandwidth_tx%d", chann + 1);
	adrv9002_gtk_label_init(priv, &priv->tx_widgets[chann].rf_bandwidth, channel, "rf_bandwidth",
				widget_str, 1000000);

	sprintf(widget_str, "frame_tx%d", chann + 1);
	adrv9002_check_channel_status(priv, &priv->tx_widgets[chann], widget_str);
	adrv9002_check_nco_freq_support(priv, chann, true);

	return 0;
}

static int adrv9002_rx_widgets_init(struct plugin_private *priv, const int chann)
{
	struct iio_channel *channel, *rx_lo;
	char chann_str[32];
	char widget_str[256];
	GtkAdjustment *bbdc_loop_gain_adjust;
	const char *bbdc_adjust = chann ? "adjustment_bbdc_loop_gain_rx1" :
						"adjustment_bbdc_loop_gain_rx2";
	const char *lo_attr = chann ? "RX2_LO_frequency" : "RX1_LO_frequency";
	uint16_t *n_w = &priv->rx_widgets[chann].rx.num_widgets;
	uint16_t *n_w_orx = &priv->orx_widgets[chann].num_widgets;

	sprintf(chann_str, "voltage%d", chann);
	channel = iio_device_find_channel(priv->adrv9002, chann_str, false);
	if (!channel)
		return -ENODEV;

	sprintf(chann_str, "altvoltage%d", chann);
	rx_lo = iio_device_find_channel(priv->adrv9002, chann_str, true);
	if (!rx_lo)
		return -ENODEV;

	priv->rx_widgets[chann].rx.priv = priv;
	priv->rx_widgets[chann].rx.idx = chann;
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

	sprintf(widget_str, "bbdc_en_rx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->rx_widgets[chann].rx.w[(*n_w)++],
					    priv->adrv9002, channel,
					    "bbdc_rejection_en", priv->builder, widget_str, false);

	sprintf(widget_str, "bbdc_loopgain_rx%d", chann + 1);
	iio_spin_button_int_init_from_builder(&priv->rx_widgets[chann].rx.w[(*n_w)++],
					      priv->adrv9002, channel, "bbdc_loop_gain_raw",
					      priv->builder, widget_str, NULL);

	/* bbdc loop gains has very low res. let's update here the adjustment */
	bbdc_loop_gain_adjust = GTK_ADJUSTMENT(gtk_builder_get_object(priv->builder, bbdc_adjust));
	gtk_adjustment_configure(bbdc_loop_gain_adjust, 0, bbdc_adjust_min, bbdc_adjust_max,
				 bbdc_adjust_min, 0, 0);
	iio_spin_button_set_convert_function(&priv->rx_widgets[chann].rx.w[*n_w - 1],
					     adrv9002_bbdc_loop_gain_convert);

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

	sprintf(widget_str, "hardware_gain_rx%d", chann + 1);
	iio_spin_button_init_from_builder(&priv->rx_widgets[chann].rx.gain,
					  priv->adrv9002, channel,
					  "hardwaregain",
					  priv->builder, widget_str, NULL);

	sprintf(widget_str, "lo_freq_rx%d", chann + 1);
	iio_spin_button_int_init_from_builder(&priv->rx_widgets[chann].rx.carrier,
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
	adrv9002_check_channel_status(priv, &priv->rx_widgets[chann].rx, widget_str);
	adrv9002_check_nco_freq_support(priv, chann, false);

	/*
	 * ORx widgets. Let's init them here as the IIO channel is the same as RX. ORx2 does
	 * not exist for adrv9003.
	 */
	if (chann >= priv->n_txs)
		return 0;

	priv->orx_widgets[chann].idx = chann;
	priv->orx_widgets[chann].priv = priv;
	sprintf(widget_str, "hardware_gain_orx%d", chann + 1);
	iio_spin_button_init_from_builder(&priv->orx_widgets[chann].w[(*n_w_orx)++],
					  priv->adrv9002, channel,
					  "orx_hardwaregain",
					  priv->builder, widget_str, NULL);

	sprintf(widget_str, "quadrature_poly_tracking_en_orx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->orx_widgets[chann].w[(*n_w_orx)++],
					    priv->adrv9002, channel,
					    "orx_quadrature_w_poly_tracking_en", priv->builder,
					    widget_str, false);

	sprintf(widget_str, "powerdown_en_orx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->orx_widgets[chann].orx_en,
					    priv->adrv9002, channel,
					    "orx_en", priv->builder, widget_str, true);

	sprintf(widget_str, "bbdc_en_orx%d", chann + 1);
	iio_toggle_button_init_from_builder(&priv->orx_widgets[chann].w[(*n_w_orx)++],
					    priv->adrv9002, channel,
					    "orx_bbdc_rejection_en", priv->builder, widget_str, false);

	adrv9002_check_orx_status(priv, &priv->orx_widgets[chann]);

	return 0;
}

static void connect_special_signal_widgets(struct plugin_private *priv, const int chann)
{
	/* rx gain handling */
	iio_make_widget_update_signal_based(&priv->rx_widgets[chann].rx.gain_ctrl,
					    G_CALLBACK(save_gain_ctl), &priv->rx_widgets[chann].rx);
	iio_make_widget_update_signal_based(&priv->rx_widgets[chann].rx.gain,
					    G_CALLBACK(iio_widget_save_block_signals_by_data_cb),
					    &priv->rx_widgets[chann].rx.gain);
	/* nco freq */
	iio_make_widget_update_signal_based(&priv->rx_widgets[chann].rx.nco_freq,
					    G_CALLBACK(iio_widget_save_block_signals_by_data_cb),
					    &priv->rx_widgets[chann].rx.nco_freq);
	/* ensm mode and port en */
	iio_make_widget_update_signal_based(&priv->rx_widgets[chann].rx.ensm,
					    G_CALLBACK(save_ensm), &priv->rx_widgets[chann].rx.ensm);
	iio_make_widget_update_signal_based(&priv->rx_widgets[chann].rx.port_en,
					    G_CALLBACK(save_port_en), &priv->rx_widgets[chann].rx);
	/* digital gain control */
	iio_make_widget_update_signal_based(&priv->rx_widgets[chann].intf_gain,
					    G_CALLBACK(save_intf_gain), &priv->rx_widgets[chann]);
	iio_make_widget_update_signal_based(&priv->rx_widgets[chann].digital_gain_ctl,
					    G_CALLBACK(save_digital_gain_ctl),
					    &priv->rx_widgets[chann]);
	/* carrier frequency */
	iio_make_widget_update_signal_based(&priv->rx_widgets[chann].rx.carrier,
					    G_CALLBACK(adrv9002_save_carrier_freq),
					    &priv->rx_widgets[chann].rx);
	if (chann >= priv->n_txs)
		return;

	/* tx atten handling */
	iio_make_widget_update_signal_based(&priv->tx_widgets[chann].gain_ctrl,
					    G_CALLBACK(save_gain_ctl), &priv->tx_widgets[chann]);
	/* nco freq */
	iio_make_widget_update_signal_based(&priv->tx_widgets[chann].nco_freq,
					    G_CALLBACK(iio_widget_save_block_signals_by_data_cb),
					    &priv->tx_widgets[chann].nco_freq);
	iio_make_widget_update_signal_based(&priv->tx_widgets[chann].gain,
					    G_CALLBACK(iio_widget_save_block_signals_by_data_cb),
					    &priv->tx_widgets[chann].gain);
	/* ensm mode and port en */
	iio_make_widget_update_signal_based(&priv->tx_widgets[chann].ensm,
					    G_CALLBACK(save_ensm), &priv->tx_widgets[chann].ensm);
	iio_make_widget_update_signal_based(&priv->tx_widgets[chann].port_en,
					    G_CALLBACK(save_port_en), &priv->tx_widgets[chann]);
	/* carrier frequency */
	iio_make_widget_update_signal_based(&priv->tx_widgets[chann].carrier,
					    G_CALLBACK(adrv9002_save_carrier_freq),
					    &priv->tx_widgets[chann]);
	/* orx enable */
	iio_make_widget_update_signal_based(&priv->orx_widgets[chann].orx_en,
					    G_CALLBACK(save_orx_powerdown), &priv->orx_widgets[chann]);
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

static void adrv9002_api_version_report(struct plugin_private *priv)
{
	GtkWidget *api_frame = GTK_WIDGET(gtk_builder_get_object(priv->builder, "frame_api"));
	GtkLabel *gapi = GTK_LABEL(gtk_builder_get_object(priv->builder, "api_version"));
	char api_version[16];

	if (iio_device_debug_attr_read(priv->adrv9002, "api_version", api_version,
				       sizeof(api_version)) < 0) {
		gtk_widget_hide(api_frame);
		return;
	}

	gtk_label_set_label(gapi, api_version);
}

static void adrv9002_initial_calibrations_init(struct plugin_private *priv)
{
	GObject *run = gtk_builder_get_object(priv->builder, "initial_calibrations_run");
	GObject *help = gtk_builder_get_object(priv->builder, "initial_calibrations_help");
	int n_w = priv->num_widgets;

	if (iio_attr_not_found(priv->adrv9002, NULL, "initial_calibrations")) {
		GObject *init_frame = gtk_builder_get_object(priv->builder, "frame_calibrations");

		gtk_widget_hide(GTK_WIDGET(init_frame));
		return;
	}

	adrv9002_combo_box_init(&priv->device_w[priv->num_widgets++], "initial_calibrations",
				"initial_calibrations", "initial_calibrations_available", priv, NULL);

	/*
	 * This will remove the "run" option from the list. Let's provide a dedicated button for
	 * it since the driver never reads back "run" from the attribute and that would make for
	 * a poor user experience. With a dedicated button, we can also tell if some error occurred
	 * or not...
	 */
	gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(priv->device_w[n_w].widget), 2);

	g_signal_connect(run, "clicked", G_CALLBACK(adrv9002_run_cals), priv);
	g_signal_connect(help, "clicked", G_CALLBACK(adrv9002_show_help), NULL);
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
	const char *name;

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

	/* lets check what device we have in hands */
	name = iio_device_get_name(priv->adrv9002);
	if (!strcmp(name, "adrv9003-phy")) {
		priv->n_txs = 1;
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(priv->builder, "frame_tx2")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(priv->builder, "frame_orx2")));
	} else {
		priv->n_txs = ARRAY_SIZE(priv->tx_widgets);
	}

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

	adrv9002_profile_read(priv);

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

	adrv9002_initial_calibrations_init(priv);

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
		adrv9002_update_orx_widgets(priv, i);
		adrv9002_update_tx_widgets(priv, i);
		iio_make_widgets_update_signal_based(priv->rx_widgets[i].rx.w,
						     priv->rx_widgets[i].rx.num_widgets,
						     G_CALLBACK(iio_widget_save_block_signals_by_data_cb));

		if (i >= priv->n_txs)
			continue;

		iio_make_widgets_update_signal_based(priv->orx_widgets[i].w,
						     priv->orx_widgets[i].num_widgets,
						     G_CALLBACK(iio_widget_save_block_signals_by_data_cb));
		iio_make_widgets_update_signal_based(priv->tx_widgets[i].w,
						     priv->tx_widgets[i].num_widgets,
						     G_CALLBACK(iio_widget_save_block_signals_by_data_cb));
	}

	/* device widgets */
	iio_update_widgets(priv->device_w, priv->num_widgets);
	iio_make_widgets_update_signal_based(priv->device_w, priv->num_widgets,
					     G_CALLBACK(iio_widget_save_block_signals_by_data_cb));

	/* update dac */
	for (i = 0; i < priv->n_dacs; i++)
		dac_data_manager_update_iio_widgets(priv->dac_manager[i].dac_tx_manager);

	adrv9002_api_version_report(priv);

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

	g_source_remove(priv->refresh_timeout);

	for (i = 0; i < priv->n_dacs; i++) {
		dac_data_manager_free(priv->dac_manager[i].dac_tx_manager);
	}

	osc_destroy_context(priv->ctx);
	osc_plugin_context_free_resources(&priv->plugin_ctx);
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

GArray* get_data_for_possible_plugin_instances(void)
{
	const char *dev = PHY_DEVICE;
	struct iio_context *osc_ctx = get_context_from_osc();
	GArray *devices = get_iio_devices_starting_with(osc_ctx, dev);

	if (!devices->len)
		/* then, let's try adrv9003 */
		dev = "adrv9003-phy";

	g_array_free(devices, FALSE);

	return get_data_for_possible_plugin_instances_helper(dev, THIS_DRIVER);
}
