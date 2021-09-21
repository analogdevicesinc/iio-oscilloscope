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
#define NUM_MAX_ORX_WIDGETS 3
#define NUM_MAX_DDS	2
#define NUM_MAX_ADC	2

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define ADRV9002_LO_INV	255

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
	struct plugin_private *priv;
	struct adrv9002_combo_box gain_ctrl;
	struct iio_widget gain;
	struct iio_widget nco_freq;
	struct iio_widget carrier;
	struct adrv9002_combo_box ensm;
	struct adrv9002_combo_box port_en;
	struct adrv9002_gtklabel rf_bandwidth;
	struct adrv9002_gtklabel sampling_rate;
	/* these are generic widgets that don't need any special attention */
	struct iio_widget w[NUM_MAX_WIDGETS];
	uint16_t num_widgets;
	bool enabled;
	uint8_t lo;
	uint8_t idx;
};

struct adrv9002_rx {
	struct adrv9002_common rx;
	struct adrv9002_combo_box digital_gain_ctl;
	struct adrv9002_combo_box intf_gain;
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
	/* rx */
	struct adrv9002_rx rx_widgets[ADRV9002_NUM_CHANNELS];
	/* tx */
	struct adrv9002_common tx_widgets[ADRV9002_NUM_CHANNELS];
	/* orx */
	struct adrv9002_orx orx_widgets[ADRV9002_NUM_CHANNELS];
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
	gtk_widget_set_sensitive(tx->ensm.w.widget, en);
	gtk_widget_set_sensitive(tx->port_en.w.widget, en);
	gtk_widget_set_sensitive(tx->gain_ctrl.w.widget, en);

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
	if (!en)
		wired = false;
	else
		wired = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(orx_other->orx_en.widget));

	for (i = 0; i < orx->num_widgets; i++) {
		if (strcmp(orx->w[i].attr_name, "orx_quadrature_w_poly_tracking_en"))
			continue;
		gtk_widget_set_sensitive(orx->w[i].widget, wired);
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
	GtkWidget *rx_ensm = rx->rx.ensm.w.widget;
	GtkWidget *tx_ensm = tx->ensm.w.widget;
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
		dialog_box_message(widget, "ORX Enable failed",
				   "RX ENSM cannot be in rf_enabled in order to enable ORX");
		/* restore widget value */
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), true);
	} else if (t_ensm && strcmp(t_ensm, "rf_enabled") && !en) {
		dialog_box_message(widget, "ORX Enable failed",
				   "TX ENSM must be in rf_enabled in order to enable ORX");
		/* restore widget value */
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), true);
	} else {
		iio_widget_save(&orx->orx_en);
		/* let's get the value again to make sure it is the most up to date */
		en = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		orx_control_rx_widgets_visibility(&rx->rx, en);
		orx_control_tx_widgets_visibility(tx, en);
		orx_control_track_cal_visibility(orx, en);
	}

	g_free(r_ensm);
	g_free(t_ensm);
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

/*
 * helper struct to save the ensm of all ports that are on the same LO as the one
 * we are changing the carrier.
 */
struct carrier_helper {
	struct {
		struct adrv9002_combo_box *ensm;
		gchar *old;
	} s[3]; /* can't have more than 3 other ports on the same LO */
	int n_restore;
};

/* helper API to move all ports on the same LO to the calibrated state */
static int carrier_helper_move_to_calibrated(struct adrv9002_common *c, struct carrier_helper *helper)
{
	int ret;
	GtkComboBoxText *w;

	ret = iio_channel_attr_write(c->ensm.w.chn, c->ensm.w.attr_name, "calibrated");
	if (ret < 0)
		return ret;

	helper->s[helper->n_restore].ensm = &c->ensm;
	w = GTK_COMBO_BOX_TEXT(c->ensm.w.widget);
	helper->s[helper->n_restore++].old = gtk_combo_box_text_get_active_text(w);
	return 0;
}

/* helper API to move the other port of the same type to the same carrier */
static int carrier_helper_move_other_port(struct adrv9002_common *c, int other, bool tx)
{
	struct iio_widget *carrier;
	gdouble freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON(c->carrier.widget));
	int ret;

	if (tx) {
		/* nothing to do if the other TX is not on the same LO */
		if (c->lo != c->priv->tx_widgets[other].lo)
			return 0;
		carrier = &c->priv->tx_widgets[other].carrier;
	} else {
		/* nothing to do if the other RX is not on the same LO */
		if (c->lo != c->priv->rx_widgets[other].rx.lo)
			return 0;
		carrier = &c->priv->rx_widgets[other].rx.carrier;
	}

	freq *= mhz_scale;
	ret = iio_channel_attr_write_longlong(carrier->chn, carrier->attr_name, freq);
	if (ret)
		return ret;

	/* we do not want to get here when setting the carrier on the other port */
	iio_widget_update_block_signals_by_data(carrier);

	return 0;
}

/*
 * Handle changing the carrier frequency. Handling the carrier frequency is not really
 * straight because it looks like we have 4 independent controls when in reality they are not
 * as the device only has 2 LOs. It all the depends on the LO mappings present in the
 * current profile. First, the only way a carrier is actually applied (PLL re-tunes) is
 * if all ports on the same LO, are moved into the calibrated state before changing it. Not
 * doing so means that we may end up in an inconsistent state. For instance, consider the
 * following steps in a FDD profile where RX1=RX2=LO1 (assuming both ports start at 2.4GHz):
 *   1) Move RX1 carrier to 2.45GHz -> LO1 re-tunes
 *   2) Move RX1 back to 2.4GHz -> LO1 does not re-tune and our carrier is at 2.45GHz
 * With the above steps we are left with both ports, __apparently__, at 2.45GHz but in __reality__
 * our carrier is at 2.4GHz.
 *
 * Secondly, when both ports of the same type are at the same LO, which happens on FDD and TTD
 * (with diversity) profiles, we should move the carrier of these ports together, because it's
 * just not possible to have both ports enabled with different carriers (they are on the same LO!).
 * On TDD profiles, we never move TX/RX ports together even being on the same LO. The assumption
 * is that we might have time to re-tune between RX and TX frames. If we don't, we need to manually
 * set TX and RX carriers to the same value before starting operating...
 */
static void adrv9002_save_carrier_freq(GtkWidget *widget, struct adrv9002_common *chan)
{
	int c;
	struct plugin_private *priv = chan->priv;
	int ret;
	int other = ~chan->idx & 0x1;
	struct carrier_helper ensm_restore = {0};
	/* we can use whatever attr as the iio channel is the same (naturally not for the LOs) */
	bool tx = iio_channel_is_output(chan->ensm.w.chn);

	if (chan->lo == ADRV9002_LO_INV) {
		/* fallback to independent LOs */
		iio_widget_save(&chan->carrier);
		return;
	}

	for (c = 0; c < ADRV9002_NUM_CHANNELS; c++) {
		/* let's move all ports on the same LO to the calibrated state */
		if (chan == &priv->tx_widgets[c] || priv->tx_widgets[c].lo != chan->lo)
			goto rx;

		ret = carrier_helper_move_to_calibrated(&priv->tx_widgets[c], &ensm_restore);
		if (ret)
			goto ensm_restore;
rx:
		if (chan == &priv->rx_widgets[c].rx || priv->rx_widgets[c].rx.lo != chan->lo)
			continue;

		ret = carrier_helper_move_to_calibrated(&priv->rx_widgets[c].rx, &ensm_restore);
		if (ret)
			goto ensm_restore;
	}

	ret = carrier_helper_move_other_port(chan, other, tx);
	if (ret)
		goto ensm_restore;

	chan->carrier.save(&chan->carrier);
ensm_restore:
	/* restore the ensm_mode */
	for (c = 0; c < ensm_restore.n_restore; c++) {
		iio_channel_attr_write(ensm_restore.s[c].ensm->w.chn,
				       ensm_restore.s[c].ensm->w.attr_name,
				       ensm_restore.s[c].old);
		/* update the UI */
		combo_box_manual_update(ensm_restore.s[c].ensm);
		g_free(ensm_restore.s[c].old);
	}

	iio_widget_update_block_signals_by_data(&chan->carrier);
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

static void adrv9002_update_orx_widgets(struct plugin_private *priv, const int chann)
{
	struct adrv9002_orx *orx = &priv->orx_widgets[chann];
	int ret;
	long long dummy;
	char label[32];

	if (!orx->enabled) {
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
	iio_update_widgets(orx->w, orx->num_widgets);
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
	iio_widget_update_block_signals_by_data(&rx->rx.carrier);
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
	iio_widget_update_block_signals_by_data(&tx->carrier);
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

static void adrv9002_profile_read(struct plugin_private *priv)
{
	char profile[512];
	ssize_t ret;
	GtkLabel *label = GTK_LABEL(gtk_builder_get_object(priv->builder, "profile_config_read"));
	int c;

	ret = iio_device_attr_read(priv->adrv9002, "profile_config", profile, sizeof(profile));
	if (ret < 0)
		strcpy(profile, "error\n");

	gtk_label_set_text(label, profile);

	/* lets get the LO mappings used when setting carriers */
	for (c = 0; c < ADRV9002_NUM_CHANNELS; c++) {
		int ret = 0, lo = 0;
		char port[8], *p;

		/* init LO mapping to an invalid value */
		priv->rx_widgets[c].rx.lo = ADRV9002_LO_INV;
		priv->tx_widgets[c].lo = ADRV9002_LO_INV;

		if (!priv->rx_widgets[c].rx.enabled)
			goto tx;

		/* look for RX */
		sprintf(port, "RX%d", c + 1);
		p = strstr(profile, port);
		if (!p)
			continue;

		ret = sscanf(p, "RX%*d LO: L0%d", &lo);
		if (ret != 1)
			continue;

		priv->rx_widgets[c].rx.lo = lo;
tx:
		if (!priv->tx_widgets[c].enabled)
			continue;

		/* look for TX */
		sprintf(port, "TX%d", c + 1);
		p = strstr(profile, port);
		if (!p)
			continue;

		ret = sscanf(p, "TX%*d LO: L0%d", &lo);
		if (ret != 1)
			continue;

		priv->tx_widgets[c].lo = lo;
	}
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

static void update_all(struct plugin_private *priv)
{
	int i;
	char gtk_str[32];

	for(i = 0; i < ADRV9002_NUM_CHANNELS; i++) {
		sprintf(gtk_str, "frame_rx%d", i + 1);
		adrv9002_check_channel_status(priv, &priv->rx_widgets[i].rx, gtk_str);
		adrv9002_check_nco_freq_support(priv, i, false);
		adrv9002_update_rx_widgets(priv, i);
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

	/* ORx widgets. Let's init them here as the IIO channel is the same as RX */
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
	/* carrier frequency */
	iio_make_widget_update_signal_based(&priv->rx_widgets[chann].rx.carrier,
					    G_CALLBACK(adrv9002_save_carrier_freq),
					    &priv->rx_widgets[chann].rx);
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
	/* carrier frequency */
	iio_make_widget_update_signal_based(&priv->tx_widgets[chann].carrier,
					    G_CALLBACK(adrv9002_save_carrier_freq),
					    &priv->tx_widgets[chann]);
	/* orx enable */
	g_signal_connect(G_OBJECT(priv->orx_widgets[chann].orx_en.widget),
			 "toggled", G_CALLBACK(save_orx_powerdown),
			 &priv->orx_widgets[chann]);
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
		make_widget_update_signal_based(priv->rx_widgets[i].rx.w,
						priv->rx_widgets[i].rx.num_widgets);
		make_widget_update_signal_based(priv->orx_widgets[i].w,
						priv->orx_widgets[i].num_widgets);
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

GArray* get_data_for_possible_plugin_instances(void)
{
	return get_data_for_possible_plugin_instances_helper(PHY_DEVICE, THIS_DRIVER);
}
