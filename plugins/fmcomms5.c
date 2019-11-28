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

#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "../libini2.h"
#include "../iio_utils.h"
#include "./block_diagram.h"
#include "dac_data_manager.h"
#include "fir_filter.h"
#ifndef _WIN32
#include "scpi.h"
#endif

#define THIS_DRIVER "FMComms5"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

#define HANNING_ENBW 1.50
#define REFCLK_RATE 40000000

#define PHY_DEVICE1 "ad9361-phy"
#define DDS_DEVICE1 "cf-ad9361-dds-core-lpc" /* can be hpc as well */
#define CAP_DEVICE1 "cf-ad9361-lpc"
#define PHY_DEVICE2 "ad9361-phy-B"
#define DDS_DEVICE2 "cf-ad9361-dds-core-B"
#define CAP_DEVICE2 "cf-ad9361-B"
#define CAP_DEVICE1_ALT "cf-ad9361-A"

#define BOLD_TEXT(txt) "<b>"txt"</b>"

extern bool dma_valid_selection(const char *device, unsigned mask, unsigned channel_count);

static struct dac_data_manager *dac_tx_manager;

static bool can_update_widgets;
static bool tx_rssi_available;

static const gdouble mhz_scale = 1000000.0;
static const gdouble inv_scale = -1.0;

static struct iio_widget glb_widgets[50];
static struct iio_widget tx_widgets[50];
static struct iio_widget rx_widgets[50];

static unsigned int num_glb, num_tx, num_rx;
static unsigned int dcxo_coarse_num, dcxo_fine_num;
static unsigned int rx_gains[5];
static unsigned int rx_lo[2], tx_lo[2];
static unsigned int rx_sample_freq, tx_sample_freq;
static char last_fir_filter[PATH_MAX];
static char *rx_fastlock_store_name, *rx_fastlock_recall_name;
static char *tx_fastlock_store_name, *tx_fastlock_recall_name;

static struct iio_context *ctx;
static struct iio_device *dev1, *dds1, *cap1;
static struct iio_device *dev2, *dds2, *cap2;

#define SECTION_GLOBAL 0
#define SECTION_TX 1
#define SECTION_RX 2
#define SECTION_FPGA 3
static GtkToggleToolButton *section_toggle[4];
static GtkWidget *section_setting[4];

/* Widgets for Global Settings */
static GtkWidget *ensm_mode;
static GtkWidget *ensm_mode_available;
static GtkWidget *calib_mode;
static GtkWidget *calib_mode_available;
static GtkWidget *trx_rate_governor;
static GtkWidget *trx_rate_governor_available;
static GtkWidget *filter_fir_config;

/* Widgets for Receive Settings */
static GtkWidget *rx_gain_control[5];
static GtkWidget *rx_gain_control_modes[5];
static GtkWidget *rf_port_select_rx;
static GtkWidget *rx_rssi[5];
static GtkWidget *tx_rssi[5];
static GtkWidget *rx_path_rates;
static GtkWidget *tx_path_rates;
static GtkWidget *fir_filter_en_tx;
static GtkWidget *enable_fir_filter_rx;
static GtkWidget *enable_fir_filter_rx_tx;
static GtkWidget *disable_all_fir_filters;
static GtkWidget *rf_port_select_tx;
static GtkWidget *rx_fastlock_profile[2];
static GtkWidget *tx_fastlock_profile[2];
static GtkWidget *rx_phase_rotation[5];

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *fmcomms5_panel;
static gboolean plugin_detached;

static const char *fmcomms5_sr_attribs[] = {
	PHY_DEVICE1".trx_rate_governor",
	PHY_DEVICE1".dcxo_tune_coarse",
	PHY_DEVICE1".dcxo_tune_fine",
	PHY_DEVICE1".ensm_mode",
	PHY_DEVICE1".in_voltage0_rf_port_select",
	PHY_DEVICE1".in_voltage0_gain_control_mode",
	PHY_DEVICE1".in_voltage0_hardwaregain",
	PHY_DEVICE1".in_voltage1_gain_control_mode",
	PHY_DEVICE1".in_voltage1_hardwaregain",
	PHY_DEVICE1".in_voltage_bb_dc_offset_tracking_en",
	PHY_DEVICE1".in_voltage_quadrature_tracking_en",
	PHY_DEVICE1".in_voltage_rf_dc_offset_tracking_en",
	PHY_DEVICE1".out_voltage0_rf_port_select",
	PHY_DEVICE1".out_altvoltage0_RX_LO_external",
	PHY_DEVICE1".out_altvoltage1_TX_LO_external",
	PHY_DEVICE1".out_altvoltage0_RX_LO_frequency",
	PHY_DEVICE1".out_altvoltage1_TX_LO_frequency",
	PHY_DEVICE1".out_voltage0_hardwaregain",
	PHY_DEVICE1".out_voltage1_hardwaregain",
	PHY_DEVICE1".out_voltage_sampling_frequency",
	PHY_DEVICE1".in_voltage_rf_bandwidth",
	PHY_DEVICE1".out_voltage_rf_bandwidth",
	PHY_DEVICE2".trx_rate_governor",
	PHY_DEVICE2".dcxo_tune_coarse",
	PHY_DEVICE2".dcxo_tune_fine",
	PHY_DEVICE2".ensm_mode",
	PHY_DEVICE2".in_voltage0_rf_port_select",
	PHY_DEVICE2".in_voltage0_gain_control_mode",
	PHY_DEVICE2".in_voltage0_hardwaregain",
	PHY_DEVICE2".in_voltage1_gain_control_mode",
	PHY_DEVICE2".in_voltage1_hardwaregain",
	PHY_DEVICE2".in_voltage_bb_dc_offset_tracking_en",
	PHY_DEVICE2".in_voltage_quadrature_tracking_en",
	PHY_DEVICE2".in_voltage_rf_dc_offset_tracking_en",
	PHY_DEVICE2".out_voltage0_rf_port_select",
	PHY_DEVICE2".out_altvoltage0_RX_LO_frequency",
	PHY_DEVICE2".out_altvoltage1_TX_LO_frequency",
	PHY_DEVICE2".out_altvoltage0_RX_LO_external",
	PHY_DEVICE2".out_altvoltage1_TX_LO_external",
	PHY_DEVICE2".out_voltage0_hardwaregain",
	PHY_DEVICE2".out_voltage1_hardwaregain",
	PHY_DEVICE2".out_voltage_sampling_frequency",
	PHY_DEVICE2".in_voltage_rf_bandwidth",
	PHY_DEVICE2".out_voltage_rf_bandwidth",
	"load_fir_filter_file",
	PHY_DEVICE1".in_voltage_filter_fir_en",
	PHY_DEVICE1".out_voltage_filter_fir_en",
	PHY_DEVICE1".in_out_voltage_filter_fir_en",
	PHY_DEVICE2".in_voltage_filter_fir_en",
	PHY_DEVICE2".out_voltage_filter_fir_en",
	PHY_DEVICE2".in_out_voltage_filter_fir_en",
	"global_settings_show",
	"tx_show",
	"rx_show",
	"fpga_show",
	"dds_mode_tx1",
	"dds_mode_tx2",
	"dds_mode_tx3",
	"dds_mode_tx4",
	"dac_buf_filename",
	"tx_channel_0",
	"tx_channel_1",
	"tx_channel_2",
	"tx_channel_3",
	"tx_channel_4",
	"tx_channel_5",
	"tx_channel_6",
	"tx_channel_7",
	DDS_DEVICE1".out_altvoltage0_TX1_I_F1_frequency",
	DDS_DEVICE1".out_altvoltage0_TX1_I_F1_phase",
	DDS_DEVICE1".out_altvoltage0_TX1_I_F1_raw",
	DDS_DEVICE1".out_altvoltage0_TX1_I_F1_scale",
	DDS_DEVICE1".out_altvoltage1_TX1_I_F2_frequency",
	DDS_DEVICE1".out_altvoltage1_TX1_I_F2_phase",
	DDS_DEVICE1".out_altvoltage1_TX1_I_F2_raw",
	DDS_DEVICE1".out_altvoltage1_TX1_I_F2_scale",
	DDS_DEVICE1".out_altvoltage2_TX1_Q_F1_frequency",
	DDS_DEVICE1".out_altvoltage2_TX1_Q_F1_phase",
	DDS_DEVICE1".out_altvoltage2_TX1_Q_F1_raw",
	DDS_DEVICE1".out_altvoltage2_TX1_Q_F1_scale",
	DDS_DEVICE1".out_altvoltage3_TX1_Q_F2_frequency",
	DDS_DEVICE1".out_altvoltage3_TX1_Q_F2_phase",
	DDS_DEVICE1".out_altvoltage3_TX1_Q_F2_raw",
	DDS_DEVICE1".out_altvoltage3_TX1_Q_F2_scale",
	DDS_DEVICE1".out_altvoltage4_TX2_I_F1_frequency",
	DDS_DEVICE1".out_altvoltage4_TX2_I_F1_phase",
	DDS_DEVICE1".out_altvoltage4_TX2_I_F1_raw",
	DDS_DEVICE1".out_altvoltage4_TX2_I_F1_scale",
	DDS_DEVICE1".out_altvoltage5_TX2_I_F2_frequency",
	DDS_DEVICE1".out_altvoltage5_TX2_I_F2_phase",
	DDS_DEVICE1".out_altvoltage5_TX2_I_F2_raw",
	DDS_DEVICE1".out_altvoltage5_TX2_I_F2_scale",
	DDS_DEVICE1".out_altvoltage6_TX2_Q_F1_frequency",
	DDS_DEVICE1".out_altvoltage6_TX2_Q_F1_phase",
	DDS_DEVICE1".out_altvoltage6_TX2_Q_F1_raw",
	DDS_DEVICE1".out_altvoltage6_TX2_Q_F1_scale",
	DDS_DEVICE1".out_altvoltage7_TX2_Q_F2_frequency",
	DDS_DEVICE1".out_altvoltage7_TX2_Q_F2_phase",
	DDS_DEVICE1".out_altvoltage7_TX2_Q_F2_raw",
	DDS_DEVICE1".out_altvoltage7_TX2_Q_F2_scale",
	DDS_DEVICE2".out_altvoltage0_TX1_I_F1_frequency",
	DDS_DEVICE2".out_altvoltage0_TX1_I_F1_phase",
	DDS_DEVICE2".out_altvoltage0_TX1_I_F1_raw",
	DDS_DEVICE2".out_altvoltage0_TX1_I_F1_scale",
	DDS_DEVICE2".out_altvoltage1_TX1_I_F2_frequency",
	DDS_DEVICE2".out_altvoltage1_TX1_I_F2_phase",
	DDS_DEVICE2".out_altvoltage1_TX1_I_F2_raw",
	DDS_DEVICE2".out_altvoltage1_TX1_I_F2_scale",
	DDS_DEVICE2".out_altvoltage2_TX1_Q_F1_frequency",
	DDS_DEVICE2".out_altvoltage2_TX1_Q_F1_phase",
	DDS_DEVICE2".out_altvoltage2_TX1_Q_F1_raw",
	DDS_DEVICE2".out_altvoltage2_TX1_Q_F1_scale",
	DDS_DEVICE2".out_altvoltage3_TX1_Q_F2_frequency",
	DDS_DEVICE2".out_altvoltage3_TX1_Q_F2_phase",
	DDS_DEVICE2".out_altvoltage3_TX1_Q_F2_raw",
	DDS_DEVICE2".out_altvoltage3_TX1_Q_F2_scale",
	DDS_DEVICE2".out_altvoltage4_TX2_I_F1_frequency",
	DDS_DEVICE2".out_altvoltage4_TX2_I_F1_phase",
	DDS_DEVICE2".out_altvoltage4_TX2_I_F1_raw",
	DDS_DEVICE2".out_altvoltage4_TX2_I_F1_scale",
	DDS_DEVICE2".out_altvoltage5_TX2_I_F2_frequency",
	DDS_DEVICE2".out_altvoltage5_TX2_I_F2_phase",
	DDS_DEVICE2".out_altvoltage5_TX2_I_F2_raw",
	DDS_DEVICE2".out_altvoltage5_TX2_I_F2_scale",
	DDS_DEVICE2".out_altvoltage6_TX2_Q_F1_frequency",
	DDS_DEVICE2".out_altvoltage6_TX2_Q_F1_phase",
	DDS_DEVICE2".out_altvoltage6_TX2_Q_F1_raw",
	DDS_DEVICE2".out_altvoltage6_TX2_Q_F1_scale",
	DDS_DEVICE2".out_altvoltage7_TX2_Q_F2_frequency",
	DDS_DEVICE2".out_altvoltage7_TX2_Q_F2_phase",
	DDS_DEVICE2".out_altvoltage7_TX2_Q_F2_raw",
	DDS_DEVICE2".out_altvoltage7_TX2_Q_F2_scale",
};

static const char * fmcomms5_driver_attribs[] = {
	"load_fir_filter_file",
	"dds_mode_tx1",
	"dds_mode_tx2",
	"dds_mode_tx3",
	"dds_mode_tx4",
	"global_settings_show",
	"tx_show",
	"rx_show",
	"fpga_show",
	"tx_channel_0",
	"tx_channel_1",
	"tx_channel_2",
	"tx_channel_3",
	"tx_channel_4",
	"tx_channel_5",
	"tx_channel_6",
	"tx_channel_7",
	"dac_buf_filename",
};

static void trigger_mcs_button(void)
{
	struct osc_plugin *plugin;
	GSList *node;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin && (!strncmp(plugin->name, "FMComms2/3/4/5 Advanced", 23))) {
			if (plugin->handle_external_request) {
				plugin->handle_external_request(NULL, "Trigger MCS");
			}
		}
	}
}

static void rx_freq_info_update(void)
{
	const char *dev_name;
	double lo_freqA;
	double lo_freqB;

	dev_name = iio_device_get_name(cap1) ?: iio_device_get_id(cap1);
	rx_update_device_sampling_freq(dev_name,
		USE_INTERN_SAMPLING_FREQ);
	lo_freqA = mhz_scale * gtk_spin_button_get_value(
			GTK_SPIN_BUTTON(rx_widgets[rx_lo[0]].widget));
	lo_freqB = mhz_scale * gtk_spin_button_get_value(
			GTK_SPIN_BUTTON(rx_widgets[rx_lo[1]].widget));

	rx_update_channel_lo_freq(dev_name, "voltage0",
		lo_freqA);
	rx_update_channel_lo_freq(dev_name, "voltage1",
		lo_freqA);
	rx_update_channel_lo_freq(dev_name, "voltage2",
		lo_freqA);
	rx_update_channel_lo_freq(dev_name, "voltage3",
		lo_freqA);

	rx_update_channel_lo_freq(dev_name, "voltage4",
		lo_freqB);
	rx_update_channel_lo_freq(dev_name, "voltage5",
		lo_freqB);
	rx_update_channel_lo_freq(dev_name, "voltage6",
		lo_freqB);
	rx_update_channel_lo_freq(dev_name, "voltage7",
		lo_freqB);
}

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_freq_info_update();
}

static void glb_settings_update_labels(void)
{
	float rates[6];
	char tmp[160], buf[1024];
	ssize_t ret;
	int i;

	ret = iio_device_attr_read(dev1, "ensm_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(ensm_mode), buf);
	else
		gtk_label_set_text(GTK_LABEL(ensm_mode), "<error>");

	ret = iio_device_attr_read(dev1, "calib_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(calib_mode), buf);
	else
		gtk_label_set_text(GTK_LABEL(calib_mode), "<error>");

	ret = iio_device_attr_read(dev1, "trx_rate_governor", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(trx_rate_governor), buf);
	else
		gtk_label_set_text(GTK_LABEL(trx_rate_governor), "<error>");

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev1, "voltage0", false),
			"gain_control_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(rx_gain_control[1]), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx_gain_control[1]), "<error>");

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev1, "voltage1", false),
			"gain_control_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(rx_gain_control[2]), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx_gain_control[2]), "<error>");

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev2, "voltage0", false),
			"gain_control_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(rx_gain_control[3]), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx_gain_control[3]), "<error>");

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev2, "voltage1", false),
			"gain_control_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(rx_gain_control[4]), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx_gain_control[4]), "<error>");

	ret = iio_device_attr_read(dev1, "rx_path_rates", buf, sizeof(buf));
	if (ret > 0) {
		sscanf(buf, "BBPLL:%f ADC:%f R2:%f R1:%f RF:%f RXSAMP:%f",
		        &rates[0], &rates[1], &rates[2], &rates[3], &rates[4],
			&rates[5]);
		sprintf(tmp, "BBPLL: %4.3f   ADC: %4.3f   R2: %4.3f   R1: %4.3f   RF: %4.3f   RXSAMP: %4.3f",
		        rates[0] / 1e6, rates[1] / 1e6, rates[2] / 1e6,
			rates[3] / 1e6, rates[4] / 1e6, rates[5] / 1e6);

		gtk_label_set_text(GTK_LABEL(rx_path_rates), tmp);
	} else {
		gtk_label_set_text(GTK_LABEL(rx_path_rates), "<error>");
	}

	ret = iio_device_attr_read(dev1, "tx_path_rates", buf, sizeof(buf));
	if (ret > 0) {
		sscanf(buf, "BBPLL:%f DAC:%f T2:%f T1:%f TF:%f TXSAMP:%f",
		        &rates[0], &rates[1], &rates[2], &rates[3], &rates[4],
			&rates[5]);
		sprintf(tmp, "BBPLL: %4.3f   DAC: %4.3f   T2: %4.3f   T1: %4.3f   TF: %4.3f   TXSAMP: %4.3f",
		        rates[0] / 1e6, rates[1] / 1e6, rates[2] / 1e6,
			rates[3] / 1e6, rates[4] / 1e6, rates[5] / 1e6);

		gtk_label_set_text(GTK_LABEL(tx_path_rates), tmp);
	} else {
		gtk_label_set_text(GTK_LABEL(tx_path_rates), "<error>");
	}

	for (i = 1; i <=4; i++)
		iio_widget_update(&rx_widgets[rx_gains[i]]);
}

static void rf_port_select_rx_changed_cb(GtkComboBoxText *cmb, gpointer data)
{
	gchar *port_name;
	bool tx_1st = false, tx_2nd = false;

	port_name = gtk_combo_box_text_get_active_text(cmb);
	if (!port_name)
		return;

	if (!strcmp(port_name, "TX_MONITOR1")) {
		tx_1st = true;
	} else if (!strcmp(port_name, "TX_MONITOR2")) {
		tx_2nd = true;
	} else if (!strcmp(port_name, "TX_MONITOR1_2")) {
		tx_1st = tx_2nd = true;
	}
	gtk_widget_set_visible(tx_rssi[1], tx_1st);
	gtk_widget_set_visible(tx_rssi[2], tx_2nd);
	gtk_widget_set_visible(tx_rssi[3], tx_1st);
	gtk_widget_set_visible(tx_rssi[4], tx_2nd);

	g_free(port_name);
}

static void sample_frequency_changed_cb(void *data)
{
	glb_settings_update_labels();
	rx_freq_info_update();
}

static gboolean delayed_mcs_trigger(void *data)
{
	trigger_mcs_button();

	return false;
}


static void rx_sample_frequency_changed_cb(void *data)
{
	g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 20, (GSourceFunc) delayed_mcs_trigger, NULL, NULL);
	sample_frequency_changed_cb(data);
}

static void tx_sample_frequency_changed_cb(void *data)
{
	sample_frequency_changed_cb(data);
}

static void rssi_update_label(GtkWidget *label, struct iio_device *dev,
		const char* chn, bool is_tx)
{
	char buf[1024];
	int ret;

	/* don't update if it is hidden (to quiet down SPI) */
	if (!gtk_widget_is_drawable(GTK_WIDGET(label)))
		return;

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev, chn, is_tx),
			"rssi", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(label), buf);
	else
		gtk_label_set_text(GTK_LABEL(label), "<error>");
}

static void rssi_update_labels(void)
{
	int i;
	char *channel_name;

	for (i = 1; i < 5; i++) {
		channel_name = g_strdup_printf("voltage%d", (i - 1) % 2);
		if (!channel_name) {
			fprintf(stderr, "Failed to alloc string in %s\n",
				__func__);
			return;
		}
		rssi_update_label(rx_rssi[i], (i < 3) ? dev1 : dev2,
					channel_name, false);
		if (tx_rssi_available)
			rssi_update_label(tx_rssi[i], (i < 3) ? dev1 : dev2,
						channel_name, true);
		g_free(channel_name);
	}
}

static gboolean update_display(gpointer foo)
{
	if (this_page == gtk_notebook_get_current_page(nbook) || plugin_detached) {
		const char *gain_mode;
		int i;

		rssi_update_labels();
		for (i = 1; i <= 4; i++) {
			gain_mode = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(rx_gain_control_modes[i]));
			if (gain_mode && strcmp(gain_mode, "manual"))
				iio_widget_update(&rx_widgets[rx_gains[i]]);
		}
	}

	return TRUE;
}

static void filter_fir_update(void)
{
	bool rx = false, tx = false, rxtx = false;
	struct iio_channel *chn;

	iio_device_attr_read_bool(dev1, "in_out_voltage_filter_fir_en", &rxtx);

	chn = iio_device_find_channel(dev1, "voltage0", false);
	if (chn)
		iio_channel_attr_read_bool(chn, "filter_fir_en", &rx);
	chn = iio_device_find_channel(dev1, "voltage0", true);
	if (chn)
		iio_channel_attr_read_bool(chn, "filter_fir_en", &tx);

	if (rxtx) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx_tx), rxtx);
	} else if (!rx && !tx) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (disable_all_fir_filters), true);
	} else {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx), rx);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (fir_filter_en_tx), tx);
	}
	glb_settings_update_labels();
}

static void filter_fir_enable(GtkToggleButton *button, gpointer data)
{
	bool rx, tx, rxtx, disable;

	if (!gtk_toggle_button_get_active(button))
		return;

	rx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx));
	tx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (fir_filter_en_tx));
	rxtx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx_tx));
	disable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (disable_all_fir_filters));

	if (rxtx || disable) {
		iio_device_attr_write_bool(dev1,
				"in_out_voltage_filter_fir_en", rxtx);
		iio_device_attr_write_bool(dev2,
				"in_out_voltage_filter_fir_en", rxtx);
	} else {
		struct iio_channel *chn;
		chn = iio_device_find_channel(dev1, "voltage0", true);
		if (chn)
			iio_channel_attr_write_bool(chn, "filter_fir_en", tx);
		chn = iio_device_find_channel(dev2, "voltage0", true);
		if (chn)
			iio_channel_attr_write_bool(chn, "filter_fir_en", tx);

		chn = iio_device_find_channel(dev1, "voltage0", false);
		if (chn)
			iio_channel_attr_write_bool(chn, "filter_fir_en", rx);
		chn = iio_device_find_channel(dev2, "voltage0", false);
		if (chn)
			iio_channel_attr_write_bool(chn, "filter_fir_en", rx);
	}

	filter_fir_update();
	trigger_mcs_button();

	if (plugin_osc_running_state() == true) {
		plugin_osc_stop_capture();
		plugin_osc_start_capture();
	}
}

static void rx_phase_rotation_update()
{
	struct iio_channel *out[8];
	gdouble val[4];
	int i;

	out[0] = iio_device_find_channel(cap1, "voltage0", false);
	out[1] = iio_device_find_channel(cap1, "voltage1", false);
	out[2] = iio_device_find_channel(cap1, "voltage2", false);
	out[3] = iio_device_find_channel(cap1, "voltage3", false);
	out[4] = iio_device_find_channel(cap2, "voltage0", false);
	out[5] = iio_device_find_channel(cap2, "voltage1", false);
	out[6] = iio_device_find_channel(cap2, "voltage2", false);
	out[7] = iio_device_find_channel(cap2, "voltage3", false);

	for (i = 0; i < 8; i += 2) {
		iio_channel_attr_read_double(out[i], "calibscale", &val[0]);
		iio_channel_attr_read_double(out[i], "calibphase", &val[1]);
		iio_channel_attr_read_double(out[i + 1], "calibscale", &val[2]);
		iio_channel_attr_read_double(out[i + 1], "calibphase", &val[3]);

		val[0] = acos(val[0]) * 360.0 / (2.0 * M_PI);
		val[1] = asin(-1.0 * val[1]) * 360.0 / (2.0 * M_PI);
		val[2] = acos(val[2]) * 360.0 / (2.0 * M_PI);
		val[3] = asin(val[3]) * 360.0 / (2.0 * M_PI);

		if (val[1] < 0.0)
			val[0] *= -1.0;
		if (val[3] < 0.0)
			val[2] *= -1.0;
		if (val[1] < -90.0)
			val[0] = (val[0] * -1.0) - 180.0;
		if (val[3] < -90.0)
			val[0] = (val[0] * -1.0) - 180.0;

		if (fabs(val[0]) > 90.0) {
			if (val[1] < 0.0)
				val[1] = (val[1] * -1.0) - 180.0;
			else
				val[1] = 180 - val[1];
		}
		if (fabs(val[2]) > 90.0) {
			if (val[3] < 0.0)
				val[3] = (val[3] * -1.0) - 180.0;
			else
				val[3] = 180 - val[3];
		}

		if (round(val[0]) != round(val[1]) &&
					round(val[0]) != round(val[2]) &&
					round(val[0]) != round(val[3])) {
			printf("error calculating phase rotations\n");
			val[0] = 0.0;
		} else
			val[0] = (val[0] + val[1] + val[2] + val[3]) / 4.0;

		gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_phase_rotation[1 + i/2]), val[0]);
	}
}

static void dcxo_widgets_update(void)
{
	char val[64];
	int ret;

	ret = iio_device_attr_read(dev1, "dcxo_tune_coarse", val, sizeof(val));
	if (ret > 0)
		gtk_widget_show(glb_widgets[dcxo_coarse_num].widget);
	ret = iio_device_attr_read(dev1, "dcxo_tune_fine", val, sizeof(val));
	if (ret > 0)
		gtk_widget_show(glb_widgets[dcxo_fine_num].widget);
}

static void reload_button_clicked(GtkButton *btn, gpointer data)
{
	iio_update_widgets(glb_widgets, num_glb);
	iio_update_widgets(tx_widgets, num_tx);
	iio_update_widgets(rx_widgets, num_rx);
	dac_data_manager_update_iio_widgets(dac_tx_manager);
	filter_fir_update();
	rx_freq_info_update();
	glb_settings_update_labels();
	rssi_update_labels();
	rx_phase_rotation_update();
	dcxo_widgets_update();
}

static void hide_section_cb(GtkToggleToolButton *btn, GtkWidget *section)
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
			gtk_window_resize (GTK_WINDOW(toplevel), 1, 1);
	}
}

static int write_int(struct iio_channel *chn, const char *attr, int val)
{
	return iio_channel_attr_write_longlong(chn, attr, (long long) val);
}

static void fastlock_clicked(GtkButton *btn, gpointer data)
{
	struct iio_device *dev;
	unsigned command = (uintptr_t) data;
	int d;
	int profile;

	if (command > 4) {
		dev = dev2;
		command -= 4;
		d = 1;
	} else {
		dev = dev1;
		d = 0;
	}
	switch (command) {
		case 1: /* RX Store */
			iio_widget_save(&rx_widgets[rx_lo[d]]);
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(rx_fastlock_profile[d]));
			write_int(iio_device_find_channel(dev, "altvoltage0", true),
					rx_fastlock_store_name, profile);
			break;
		case 2: /* TX Store */
			iio_widget_save(&tx_widgets[tx_lo[d]]);
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile[d]));
			write_int(iio_device_find_channel(dev, "altvoltage1", true),
					tx_fastlock_store_name, profile);
			break;
		case 3: /* RX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(rx_fastlock_profile[d]));
			write_int(iio_device_find_channel(dev, "altvoltage0", true),
					rx_fastlock_recall_name, profile);
			iio_widget_update(&rx_widgets[rx_lo[d]]);
			break;
		case 4: /* TX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile[d]));
			write_int(iio_device_find_channel(dev, "altvoltage1", true),
					tx_fastlock_recall_name, profile);
			iio_widget_update(&tx_widgets[tx_lo[d]]);
			break;
	}
}

static void filter_fir_config_file_set_cb (GtkFileChooser *chooser, gpointer data)
{
	char *file_name = gtk_file_chooser_get_filename(chooser);

	load_fir_filter(file_name, dev1, dev2, fmcomms5_panel, chooser,
			fir_filter_en_tx, enable_fir_filter_rx,
			enable_fir_filter_rx_tx, disable_all_fir_filters,
			last_fir_filter);
}

static void tx_sample_rate_changed(GtkSpinButton *spinbutton, gpointer user_data)
{
	gdouble rate;

	rate = gtk_spin_button_get_value(spinbutton) / 2.0;
	dac_data_manager_freq_widgets_range_update(dac_tx_manager, rate);
}

static void rx_phase_rotation_set(GtkSpinButton *spinbutton, gpointer user_data)
{
	uintptr_t offset = (uintptr_t) user_data;
	struct iio_device *dev;
	struct iio_channel *out0, *out1;
	gdouble val, phase;

	val = gtk_spin_button_get_value(spinbutton);

	phase = val * 2 * M_PI / 360.0;

	if (offset == 4 || offset == 6) {
		dev = cap2;
		offset -= 4;
	} else {
		dev = cap1;
	}

	if (offset == 2) {
		out0 = iio_device_find_channel(dev, "voltage2", false);
		out1 = iio_device_find_channel(dev, "voltage3", false);
	} else {
		out0 = iio_device_find_channel(dev, "voltage0", false);
		out1 = iio_device_find_channel(dev, "voltage1", false);
	}

	if (out1 && out0) {
		iio_channel_attr_write_double(out0, "calibscale", (double) cos(phase));
		iio_channel_attr_write_double(out0, "calibphase", (double) (-1 * sin(phase)));
		iio_channel_attr_write_double(out1, "calibscale", (double) cos(phase));
		iio_channel_attr_write_double(out1, "calibphase", (double) sin(phase));
	}
}

/* Check for a valid two channels combination (ch0->ch1, ch2->ch3, ...)
 *
 * struct iio_device *dev - the iio device that owns the channels
 * char* ch_name - output parameter: stores the names of to the
 *                 enabled channels, useful for reporting for which
 *                 channels the combination is valid or not.
 * Return 1 if the channel combination is valid and 0 otherwise.
 */
static int channel_combination_check(struct iio_device *dev, const char **ch_names)
{
	bool consecutive_ch = FALSE;
	unsigned int i, k;
	GArray *channels = get_iio_channels_naturally_sorted(dev);

	for (i = 0, k = 0; i < channels->len; ++i) {
		struct iio_channel *ch = g_array_index(channels, struct iio_channel *, i);
		struct extra_info *info = iio_channel_get_data(ch);

		if (info->may_be_enabled) {
			const char *name = iio_channel_get_name(ch) ?: iio_channel_get_id(ch);
			ch_names[k++] = name;

			if (i > 0) {
				struct extra_info *prev = iio_channel_get_data(g_array_index(channels, struct iio_channel *, i - 1));

				if (prev->may_be_enabled) {
					consecutive_ch = TRUE;
					break;
				}
			}
		}
	}
	g_array_free(channels, FALSE);

	if (!consecutive_ch)
		return 0;

	if (!(i & 0x1))
		return 0;

	return 1;
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

static int handle_external_request(struct osc_plugin *plugin, const char *request)
{
	int ret = 0;

	if (!strcmp(request, "Reload Settings")) {
		reload_button_clicked(NULL, 0);
		ret = 1;
	}

	return ret;
}

#ifndef _WIN32
static int dcxo_to_eeprom(void)
{
	const char *eeprom_path = find_eeprom(NULL);
	char cmd[256];
	FILE *fp = NULL, *cmdfp = NULL;
	const char *failure_msg = NULL;
	double current_freq, target_freq;
	int ret = 0;

	if (!eeprom_path) {
		failure_msg = "Can't find EEPROM file in the sysfs";
		goto cleanup;
	}

	if (!strcmp(iio_context_get_name(ctx), "network")) {
		target_freq = REFCLK_RATE;
	} else if (!strcmp(iio_context_get_name(ctx), "local")) {
		fp = fopen("/sys/kernel/debug/clk/ad9361_ext_refclk/clk_rate", "r");
		if (!fp || fscanf(fp, "%lf", &target_freq) != 1) {
			failure_msg = "Unable to read AD9361 reference clock rate from debugfs.";
			if (fp)
				fclose(fp);
			goto cleanup;
		}
		if (fp) {
			fclose(fp);
		}
	} else {
		failure_msg = "AD9361 Reference clock rate missing from debugfs.";
		goto cleanup;
	}

	if (scpi_connect_counter() != 0) {
		failure_msg = "Failed to connect to Programmable Counter device.";
		goto cleanup;
	}

	if (scpi_counter_get_freq(&current_freq, &target_freq) != 0) {
		failure_msg = "Error retrieving counter frequency. "
			"Make sure the counter has the correct input attached.";
		goto cleanup;
	}

	sprintf(cmd, "fru-dump -i \"%s\" -o \"%s\" -t %x 2>&1", eeprom_path,
			eeprom_path, (unsigned int)current_freq);
	cmdfp = popen(cmd, "r");

	if (!cmdfp || pclose(cmdfp) != 0) {
		failure_msg = "Error running fru-dump to write to EEPROM";
		fprintf(stderr, "Error running fru-dump: %s\n", cmd);
		goto cleanup;
	}

cleanup:
	if (failure_msg) {
		fprintf(stderr, "SCPI failed: %s\n", failure_msg);
		GtkWidget *toplevel = gtk_widget_get_toplevel(fmcomms5_panel);
		if (!gtk_widget_is_toplevel(toplevel))
			toplevel = NULL;
		GtkWidget *dcxo_to_eeprom_fail = gtk_message_dialog_new(
			GTK_WINDOW(toplevel),
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE,
			"%s", failure_msg);
		gtk_window_set_title(GTK_WINDOW(dcxo_to_eeprom_fail), "Save to EEPROM");
		if (gtk_dialog_run(GTK_DIALOG(dcxo_to_eeprom_fail)))
			gtk_widget_destroy(dcxo_to_eeprom_fail);
		ret = -1;
	}

	g_free((void *)eeprom_path);

	return ret;
}
#endif /* _WIN32 */

static int fmcomms5_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	int ret = 0;

	if (MATCH_ATTRIB("load_fir_filter_file")) {
		if (value[0]) {
			load_fir_filter(value, dev1, dev2, fmcomms5_panel,
					GTK_FILE_CHOOSER(filter_fir_config),
					fir_filter_en_tx, enable_fir_filter_rx,
					enable_fir_filter_rx_tx,
					disable_all_fir_filters,
					last_fir_filter);
		}
	} else if (MATCH_ATTRIB("global_settings_show")) {
		gtk_toggle_tool_button_set_active(
				section_toggle[SECTION_GLOBAL], !!atoi(value));
		hide_section_cb(section_toggle[SECTION_GLOBAL],
				section_setting[SECTION_GLOBAL]);
	} else if (MATCH_ATTRIB("tx_show")) {
		gtk_toggle_tool_button_set_active(
				section_toggle[SECTION_TX], !!atoi(value));
		hide_section_cb(section_toggle[SECTION_TX],
				section_setting[SECTION_TX]);
	} else if (MATCH_ATTRIB("rx_show")) {
		gtk_toggle_tool_button_set_active(
				section_toggle[SECTION_RX], !!atoi(value));
		hide_section_cb(section_toggle[SECTION_RX],
				section_setting[SECTION_RX]);
	} else if (MATCH_ATTRIB("fpga_show")) {
		gtk_toggle_tool_button_set_active(
				section_toggle[SECTION_FPGA], !!atoi(value));
		hide_section_cb(section_toggle[SECTION_FPGA],
				section_setting[SECTION_FPGA]);
	} else if (MATCH_ATTRIB("dac_buf_filename")) {
		if (dac_data_manager_get_dds_mode(dac_tx_manager,
					DDS_DEVICE1, 1) == DDS_BUFFER)
			dac_data_manager_set_buffer_chooser_filename(
					dac_tx_manager, value);
	} else if (!strncmp(attrib, "dds_mode_tx", sizeof("dds_mode_tx") - 1)) {
		int tx = atoi(attrib + sizeof("dds_mode_tx") - 1);
		dac_data_manager_set_dds_mode(dac_tx_manager,
				tx <= 2 ? DDS_DEVICE1 : DDS_DEVICE2,
				tx <= 2 ? tx : tx % 2, atoi(value));
	} else if (!strncmp(attrib, "tx_channel_", sizeof("tx_channel_") - 1)) {
		int tx = atoi(attrib + sizeof("tx_channel_") - 1);
		dac_data_manager_set_tx_channel_state(
				dac_tx_manager, tx, !!atoi(value));
	} else if (MATCH_ATTRIB("SYNC_RELOAD")) {
		if (can_update_widgets)
			reload_button_clicked(NULL, NULL);
#ifndef _WIN32
	} else if (MATCH_ATTRIB("dcxo_to_eeprom")) {
		if (scpi_connect_functions()) {
			fprintf(stderr, "SCPI: Saving current clock rate to EEPROM.\n");
			ret = dcxo_to_eeprom();
		} else {
			fprintf(stderr, "SCPI plugin not loaded, can't query frequency.\n");
			ret = -1;
		}
#endif /* _WIN32 */
	} else {
		return -EINVAL;
	}

	return ret;
}

static int fmcomms5_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			fmcomms5_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	struct iio_channel *ch;
	char *value;
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(fmcomms5_driver_attribs); i++) {
		char *value = read_token_from_ini(ini_fn, THIS_DRIVER,
				fmcomms5_driver_attribs[i]);
		if (value) {
			fmcomms5_handle_driver(NULL,
					fmcomms5_driver_attribs[i], value);
			free(value);
		}
	}

	/* The gain_control_mode iio attribute should be set prior to setting
	 * hardwaregain iio attribute. This is neccessary due to the fact that
	 * some control modes change the hardwaregain automatically. */
	ch = iio_device_find_channel(dev1, "voltage0", false);
	value = read_token_from_ini(ini_fn, THIS_DRIVER,
				PHY_DEVICE1".in_voltage0_gain_control_mode");
	if (ch && value) {
		iio_channel_attr_write(ch, "gain_control_mode", value);
		free(value);
	}

	ch = iio_device_find_channel(dev1, "voltage1", false);
	value = read_token_from_ini(ini_fn, THIS_DRIVER,
				PHY_DEVICE1".in_voltage1_gain_control_mode");
	if (ch && value) {
		iio_channel_attr_write(ch, "gain_control_mode", value);
		free(value);
	}

	ch = iio_device_find_channel(dev2, "voltage0", false);
	value = read_token_from_ini(ini_fn, THIS_DRIVER,
				PHY_DEVICE2".in_voltage0_gain_control_mode");
	if (ch && value) {
		iio_channel_attr_write(ch, "gain_control_mode", value);
		free(value);
	}

	ch = iio_device_find_channel(dev2, "voltage1", false);
	value = read_token_from_ini(ini_fn, THIS_DRIVER,
				PHY_DEVICE2".in_voltage1_gain_control_mode");
	if (ch && value) {
		iio_channel_attr_write(ch, "gain_control_mode", value);
		free(value);
	}

	update_from_ini(ini_fn, THIS_DRIVER, dev1, fmcomms5_sr_attribs,
			ARRAY_SIZE(fmcomms5_sr_attribs));
	update_from_ini(ini_fn, THIS_DRIVER, dds1, fmcomms5_sr_attribs,
			ARRAY_SIZE(fmcomms5_sr_attribs));
	update_from_ini(ini_fn, THIS_DRIVER, cap1, fmcomms5_sr_attribs,
			ARRAY_SIZE(fmcomms5_sr_attribs));
	update_from_ini(ini_fn, THIS_DRIVER, dev2, fmcomms5_sr_attribs,
			ARRAY_SIZE(fmcomms5_sr_attribs));
	update_from_ini(ini_fn, THIS_DRIVER, dds2, fmcomms5_sr_attribs,
			ARRAY_SIZE(fmcomms5_sr_attribs));
	update_from_ini(ini_fn, THIS_DRIVER, cap2, fmcomms5_sr_attribs,
			ARRAY_SIZE(fmcomms5_sr_attribs));

	if (can_update_widgets)
		reload_button_clicked(NULL, NULL);
}

static GtkWidget * fmcomms5_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *dds_container;
	GtkWidget *dev1_rx_frm, *dev2_rx_frm;
	GtkWidget *dev1_tx_frm, *dev2_tx_frm;
	const char *freq_name;
	int rx_sample_freq_pair, tx_sample_freq_pair;
	int i;

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	ctx = osc_create_context();
	dev1 = iio_context_find_device(ctx, PHY_DEVICE1);
	dds1 = iio_context_find_device(ctx, DDS_DEVICE1);
	cap1 = iio_context_find_device(ctx, CAP_DEVICE1);
	dev2 = iio_context_find_device(ctx, PHY_DEVICE2);
	dds2 = iio_context_find_device(ctx, DDS_DEVICE2);
	cap2 = iio_context_find_device(ctx, CAP_DEVICE2);
	if (!cap1)
		cap1 = iio_context_find_device(ctx, CAP_DEVICE1_ALT);

	dac_tx_manager = dac_data_manager_new(dds1, dds2, ctx);
	if (!dac_tx_manager) {
		printf("FMComms5: Failed to use DDS resources\n");
		return 0;
	}

	if (osc_load_glade_file(builder, "fmcomms5") < 0)
		return NULL;

	fmcomms5_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms5_panel"));

	/* Global settings */

	ensm_mode = GTK_WIDGET(gtk_builder_get_object(builder, "ensm_mode"));
	ensm_mode_available = GTK_WIDGET(gtk_builder_get_object(builder, "ensm_mode_available"));
	calib_mode = GTK_WIDGET(gtk_builder_get_object(builder, "calib_mode"));
	calib_mode_available = GTK_WIDGET(gtk_builder_get_object(builder, "calib_mode_available"));
	trx_rate_governor = GTK_WIDGET(gtk_builder_get_object(builder, "trx_rate_governor"));
	trx_rate_governor_available = GTK_WIDGET(gtk_builder_get_object(builder, "trx_rate_governor_available"));
	tx_path_rates = GTK_WIDGET(gtk_builder_get_object(builder, "label_tx_path"));
	rx_path_rates = GTK_WIDGET(gtk_builder_get_object(builder, "label_rx_path"));
	filter_fir_config = GTK_WIDGET(gtk_builder_get_object(builder, "filter_fir_config"));
	enable_fir_filter_rx = GTK_WIDGET(gtk_builder_get_object(builder, "enable_fir_filter_rx"));
	fir_filter_en_tx = GTK_WIDGET(gtk_builder_get_object(builder, "fir_filter_en_tx"));
	enable_fir_filter_rx_tx = GTK_WIDGET(gtk_builder_get_object(builder, "enable_fir_filter_tx_rx"));
	disable_all_fir_filters = GTK_WIDGET(gtk_builder_get_object(builder, "disable_all_fir_filters"));

	section_toggle[SECTION_GLOBAL] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "global_settings_toggle"));
	section_setting[SECTION_GLOBAL] = GTK_WIDGET(gtk_builder_get_object(builder, "global_settings"));
	section_toggle[SECTION_TX] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "tx_toggle"));
	section_setting[SECTION_TX] = GTK_WIDGET(gtk_builder_get_object(builder, "tx_settings"));
	section_toggle[SECTION_RX] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "rx_toggle"));
	section_setting[SECTION_RX] = GTK_WIDGET(gtk_builder_get_object(builder, "rx_settings"));
	section_toggle[SECTION_FPGA] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "fpga_toggle"));
	section_setting[SECTION_FPGA] = GTK_WIDGET(gtk_builder_get_object(builder, "fpga_settings"));

	/* Receive Chain */

	dev1_rx_frm = GTK_WIDGET(gtk_builder_get_object(builder, "device1_rx_frame"));
	dev2_rx_frm = GTK_WIDGET(gtk_builder_get_object(builder, "device2_rx_frame"));
	rf_port_select_rx = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_rx"));
	rx_gain_control[1] = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx1"));
	rx_gain_control[2] = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx2"));
	rx_gain_control[3] = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx3"));
	rx_gain_control[4] = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx4"));
	rx_gain_control_modes[1] = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx1"));
	rx_gain_control_modes[2] = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx2"));
	rx_gain_control_modes[3] = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx3"));
	rx_gain_control_modes[4] = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx4"));
	rx_rssi[1] = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx1"));
	rx_rssi[2] = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx2"));
	rx_rssi[3] = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx3"));
	rx_rssi[4] = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx4"));
	rx_fastlock_profile[0] = GTK_WIDGET(gtk_builder_get_object(builder, "rx_fastlock_profile1"));
	rx_fastlock_profile[1] = GTK_WIDGET(gtk_builder_get_object(builder, "rx_fastlock_profile2"));

	/* Transmit Chain */

	dev1_tx_frm = GTK_WIDGET(gtk_builder_get_object(builder, "device1_tx_frame"));
	dev2_tx_frm = GTK_WIDGET(gtk_builder_get_object(builder, "device2_tx_frame"));
	rf_port_select_tx = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_tx"));
	tx_rssi[1] = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_tx1"));
	tx_rssi[2] = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_tx2"));
	tx_rssi[3] = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_tx3"));
	tx_rssi[4] = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_tx4"));
	tx_fastlock_profile[0] = GTK_WIDGET(gtk_builder_get_object(builder, "tx_fastlock_profile1"));
	tx_fastlock_profile[1] = GTK_WIDGET(gtk_builder_get_object(builder, "tx_fastlock_profile2"));
	dds_container = GTK_WIDGET(gtk_builder_get_object(builder, "dds_transmit_block"));
	gtk_container_add(GTK_CONTAINER(dds_container), dac_data_manager_get_gui_container(dac_tx_manager));
	gtk_widget_show_all(dds_container);

	rx_phase_rotation[1] = GTK_WIDGET(gtk_builder_get_object(builder, "rx1_phase_rotation"));
	rx_phase_rotation[2] = GTK_WIDGET(gtk_builder_get_object(builder, "rx2_phase_rotation"));
	rx_phase_rotation[3] = GTK_WIDGET(gtk_builder_get_object(builder, "rx3_phase_rotation"));
	rx_phase_rotation[4] = GTK_WIDGET(gtk_builder_get_object(builder, "rx4_phase_rotation"));

	gtk_combo_box_set_active(GTK_COMBO_BOX(ensm_mode_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(trx_rate_governor_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes[1]), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes[2]), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes[3]), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes[4]), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rf_port_select_rx), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rf_port_select_tx), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_fastlock_profile[0]), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_fastlock_profile[1]), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(tx_fastlock_profile[0]), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(tx_fastlock_profile[1]), 0);

	gtk_label_set_markup(GTK_LABEL(gtk_frame_get_label_widget(GTK_FRAME(dev1_rx_frm))), BOLD_TEXT(PHY_DEVICE1));
	gtk_label_set_markup(GTK_LABEL(gtk_frame_get_label_widget(GTK_FRAME(dev2_rx_frm))), BOLD_TEXT(PHY_DEVICE2));
	gtk_label_set_markup(GTK_LABEL(gtk_frame_get_label_widget(GTK_FRAME(dev1_tx_frm))), BOLD_TEXT(PHY_DEVICE1));
	gtk_label_set_markup(GTK_LABEL(gtk_frame_get_label_widget(GTK_FRAME(dev2_tx_frm))), BOLD_TEXT(PHY_DEVICE2));

	/* Bind the IIO device files to the GUI widgets */

	/* Global settings */
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev1, NULL, "ensm_mode", "ensm_mode_available",
		ensm_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev1, NULL, "calib_mode", "calib_mode_available",
		calib_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev1, NULL, "trx_rate_governor", "trx_rate_governor_available",
		trx_rate_governor_available, NULL);

	dcxo_coarse_num = num_glb;
	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		dev1, NULL, "dcxo_tune_coarse", builder, "dcxo_coarse_tune",
		0);
	dcxo_fine_num = num_glb;
	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		dev1, NULL, "dcxo_tune_fine", builder, "dcxo_fine_tune",
		0);

	iio_combo_box_init(&glb_widgets[num_glb++],
		dev2, NULL, "ensm_mode", "ensm_mode_available",
		ensm_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev2, NULL, "calib_mode", "calib_mode_available",
		calib_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev2, NULL, "trx_rate_governor", "trx_rate_governor_available",
		trx_rate_governor_available, NULL);

	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		dev2, NULL, "dcxo_tune_coarse", builder, "dcxo_coarse_tune",
		0);
	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		dev2, NULL, "dcxo_tune_fine", builder, "dcxo_fine_tune",
		0);

	/* Receive Chain */
	struct iio_channel *d1_ch0 = iio_device_find_channel(dev1, "voltage0", false),
			   *d1_ch1 = iio_device_find_channel(dev1, "voltage1", false),
			   *d2_ch0 = iio_device_find_channel(dev2, "voltage0", false),
			   *d2_ch1 = iio_device_find_channel(dev2, "voltage1", false);

	iio_combo_box_init(&rx_widgets[num_rx++],
		dev1, d1_ch0, "gain_control_mode",
		"gain_control_mode_available",
		rx_gain_control_modes[1], NULL);
	iio_combo_box_init(&rx_widgets[num_rx++],
		dev1, d1_ch1, "gain_control_mode",
		"gain_control_mode_available",
		rx_gain_control_modes[2], NULL);
	iio_combo_box_init(&rx_widgets[num_rx++],
		dev2, d2_ch0, "gain_control_mode",
		"gain_control_mode_available",
		rx_gain_control_modes[3], NULL);
	iio_combo_box_init(&rx_widgets[num_rx++],
		dev2, d2_ch1, "gain_control_mode",
		"gain_control_mode_available",
		rx_gain_control_modes[4], NULL);

	iio_combo_box_init(&rx_widgets[num_rx++],
		dev1, d1_ch0, "rf_port_select",
		"rf_port_select_available",
		rf_port_select_rx, NULL);
	iio_combo_box_init(&rx_widgets[num_rx++],
		dev2, d2_ch0, "rf_port_select",
		"rf_port_select_available",
		rf_port_select_rx, NULL);

	rx_gains[1] = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev1, d1_ch0, "hardwaregain", builder,
		"hardware_gain_rx1", NULL);
	rx_gains[2] = num_rx;
		iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			dev1, d1_ch1, "hardwaregain", builder,
			"hardware_gain_rx2", NULL);
	rx_gains[3] = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev2, d2_ch0, "hardwaregain", builder,
		"hardware_gain_rx3", NULL);
	rx_gains[4] = num_rx;
		iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			dev2, d2_ch1, "hardwaregain", builder,
			"hardware_gain_rx4", NULL);

	rx_sample_freq = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev1, d1_ch0, "sampling_frequency", builder,
		"sampling_freq_rx", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);
	rx_sample_freq_pair = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev2, d2_ch0, "sampling_frequency", builder,
		"sampling_freq_rx", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev1, d1_ch0, "rf_bandwidth", builder, "rf_bandwidth_rx",
		&mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev2, d2_ch0, "rf_bandwidth", builder, "rf_bandwidth_rx",
		&mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);


	d1_ch1 = iio_device_find_channel(dev1, "altvoltage0", true);
	d2_ch1 = iio_device_find_channel(dev2, "altvoltage0", true);

	if (iio_channel_find_attr(d1_ch1, "frequency"))
		freq_name = "frequency";
	else
		freq_name = "RX_LO_frequency";
	rx_lo[0] = num_rx;
	iio_spin_button_s64_init_from_builder(&rx_widgets[num_rx++],
		dev1, d1_ch1, freq_name, builder,
		"rx_lo_freq1", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);
	rx_lo[1] = num_rx;
	iio_spin_button_s64_init_from_builder(&rx_widgets[num_rx++],
		dev2, d2_ch1, freq_name, builder,
		"rx_lo_freq2", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev1, d1_ch1, "external", builder, "rx_lo_external1", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev2, d2_ch1, "external", builder, "rx_lo_external2", 0);

	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev1, d1_ch0, "quadrature_tracking_en", builder,
		"quad", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev2, d2_ch0, "quadrature_tracking_en", builder,
		"quad", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev1, d1_ch0, "rf_dc_offset_tracking_en", builder,
		"rfdc", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev2, d2_ch0, "rf_dc_offset_tracking_en", builder,
		"rfdc", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev1, d1_ch0, "bb_dc_offset_tracking_en", builder,
		"bbdc", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev2, d2_ch0, "bb_dc_offset_tracking_en", builder,
		"bbdc", 0);

	iio_spin_button_init_from_builder(&rx_widgets[num_rx],
		dev1, d1_ch1, "calibphase",
		builder, "rx1_phase_rotation", NULL);
	iio_spin_button_add_progress(&rx_widgets[num_rx++]);
	iio_spin_button_init_from_builder(&rx_widgets[num_rx],
		dev2, d2_ch1, "calibphase",
		builder, "rx3_phase_rotation", NULL);
	iio_spin_button_add_progress(&rx_widgets[num_rx++]);

	d1_ch0 = iio_device_find_channel(dev1, "altvoltage0", true);

	if (iio_channel_find_attr(d1_ch0, "fastlock_store"))
		rx_fastlock_store_name = "fastlock_store";
	else
		rx_fastlock_store_name = "RX_LO_fastlock_store";
	if (iio_channel_find_attr(d1_ch0, "fastlock_recall"))
		rx_fastlock_recall_name = "fastlock_recall";
	else
		rx_fastlock_recall_name = "RX_LO_fastlock_recall";

	/* Transmit Chain */

	d1_ch0 = iio_device_find_channel(dev1, "voltage0", true);
	d1_ch1 = iio_device_find_channel(dev1, "voltage1", true);
	d2_ch0 = iio_device_find_channel(dev2, "voltage0", true);
	d2_ch1 = iio_device_find_channel(dev2, "voltage1", true);

	tx_rssi_available = d1_ch0 && iio_channel_find_attr(d1_ch0, "rssi");

	iio_combo_box_init(&tx_widgets[num_tx++],
		dev1, d1_ch0, "rf_port_select",
		"rf_port_select_available",
		rf_port_select_tx, NULL);
	iio_combo_box_init(&tx_widgets[num_tx++],
		dev2, d2_ch0, "rf_port_select",
		"rf_port_select_available",
		rf_port_select_tx, NULL);

	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
		dev1, d1_ch0, "hardwaregain", builder,
		"hardware_gain_tx1", &inv_scale);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
		dev1, d1_ch1, "hardwaregain", builder,
		"hardware_gain_tx2", &inv_scale);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
		dev2, d2_ch0, "hardwaregain", builder,
		"hardware_gain_tx3", &inv_scale);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
		dev2, d2_ch1, "hardwaregain", builder,
		"hardware_gain_tx4", &inv_scale);

	tx_sample_freq = num_tx;
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		dev1, d1_ch0, "sampling_frequency", builder,
		"sampling_freq_tx", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	tx_sample_freq_pair = num_tx;
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		dev2, d2_ch0, "sampling_frequency", builder,
		"sampling_freq_tx", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		dev1, d1_ch0, "rf_bandwidth", builder,
		"rf_bandwidth_tx", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		dev2, d2_ch0, "rf_bandwidth", builder,
		"rf_bandwidth_tx", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	d1_ch1 = iio_device_find_channel(dev1, "altvoltage1", true);
	d2_ch1 = iio_device_find_channel(dev2, "altvoltage1", true);

	if (iio_channel_find_attr(d1_ch1, "frequency"))
		freq_name = "frequency";
	else
		freq_name = "TX_LO_frequency";

	tx_lo[0] = num_tx;
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
		dev1, d1_ch1, freq_name, builder, "tx_lo_freq1", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	tx_lo[1] = num_tx;
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
		dev2, d2_ch1, freq_name, builder, "tx_lo_freq2", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		dev1, d1_ch1, "external", builder, "tx_lo_external1", 0);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		dev2, d2_ch1, "external", builder, "tx_lo_external2", 0);

	d1_ch1 = iio_device_find_channel(dev1, "altvoltage1", true);

	if (iio_channel_find_attr(d1_ch1, "fastlock_store"))
		tx_fastlock_store_name = "fastlock_store";
	else
		tx_fastlock_store_name = "TX_LO_fastlock_store";
	if (iio_channel_find_attr(d1_ch1, "fastlock_recall"))
		tx_fastlock_recall_name = "fastlock_recall";
	else
		tx_fastlock_recall_name = "TX_LO_fastlock_recall";

	/* Widgets bindings */
	g_builder_bind_property(builder, "rssi_tx1", "visible",
		"label_rssi_tx1", "sensitive", G_BINDING_DEFAULT);
	g_builder_bind_property(builder, "rssi_tx2", "visible",
		"label_rssi_tx2", "sensitive", G_BINDING_DEFAULT);
	g_builder_bind_property(builder, "rssi_tx3", "visible",
		"label_rssi_tx3", "sensitive", G_BINDING_DEFAULT);
	g_builder_bind_property(builder, "rssi_tx4", "visible",
		"label_rssi_tx4", "sensitive", G_BINDING_DEFAULT);

	g_builder_bind_property(builder, "rx_lo_external1", "active",
		"rx_fastlock_profile1", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "rx_lo_external1", "active",
		"rx_fastlock_label1", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "rx_lo_external1", "active",
		"rx_fastlock_actions1", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "rx_lo_external2", "active",
		"rx_fastlock_profile2", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "rx_lo_external2", "active",
		"rx_fastlock_label2", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "rx_lo_external2", "active",
		"rx_fastlock_actions2", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "tx_lo_external1", "active",
		"tx_fastlock_profile1", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "tx_lo_external1", "active",
		"tx_fastlock_label1", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "tx_lo_external1", "active",
		"tx_fastlock_actions1", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "tx_lo_external2", "active",
		"tx_fastlock_profile2", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "tx_lo_external2", "active",
		"tx_fastlock_label2", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "tx_lo_external2", "active",
		"tx_fastlock_actions2", "visible", G_BINDING_INVERT_BOOLEAN);

	if (ini_fn)
		load_profile(NULL, ini_fn);

	if (tx_rssi_available)
		g_signal_connect(rf_port_select_rx, "changed",
			G_CALLBACK(rf_port_select_rx_changed_cb), NULL);

	g_builder_connect_signal(builder, "rx1_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)0);
	g_builder_connect_signal(builder, "rx2_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)2);
	g_builder_connect_signal(builder, "rx3_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)4);
	g_builder_connect_signal(builder, "rx4_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)6);

	g_builder_connect_signal(builder, "sampling_freq_tx", "value-changed",
			G_CALLBACK(tx_sample_rate_changed), NULL);

	g_builder_connect_signal(builder, "fmcomms2_settings_reload", "clicked",
		G_CALLBACK(reload_button_clicked), NULL);

	g_builder_connect_signal(builder, "filter_fir_config", "file-set",
		G_CALLBACK(filter_fir_config_file_set_cb), NULL);

	g_builder_connect_signal(builder, "rx_fastlock_store1", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 1);
	g_builder_connect_signal(builder, "tx_fastlock_store1", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 2);
	g_builder_connect_signal(builder, "rx_fastlock_recall1", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 3);
	g_builder_connect_signal(builder, "tx_fastlock_recall1", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 4);
	g_builder_connect_signal(builder, "rx_fastlock_store2", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 5);
	g_builder_connect_signal(builder, "tx_fastlock_store2", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 6);
	g_builder_connect_signal(builder, "rx_fastlock_recall2", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 7);
	g_builder_connect_signal(builder, "tx_fastlock_recall2", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 8);

	g_signal_connect_after(section_toggle[SECTION_GLOBAL], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_GLOBAL]);

	g_signal_connect_after(section_toggle[SECTION_TX], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_TX]);

	g_signal_connect_after(section_toggle[SECTION_RX], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_RX]);

	g_signal_connect_after(section_toggle[SECTION_FPGA], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_FPGA]);

	g_signal_connect_after(ensm_mode_available, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);

	g_signal_connect_after(calib_mode_available, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);

	g_signal_connect_after(trx_rate_governor_available, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);

	for (i = 1; i <= 4; i++)
		g_signal_connect_after(rx_gain_control_modes[i], "changed",
			G_CALLBACK(glb_settings_update_labels), NULL);

	g_signal_connect_after(enable_fir_filter_rx, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);
	g_signal_connect_after(fir_filter_en_tx, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);
	g_signal_connect_after(enable_fir_filter_rx_tx, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);
	g_signal_connect_after(disable_all_fir_filters, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);

	make_widget_update_signal_based(glb_widgets, num_glb);
	make_widget_update_signal_based(rx_widgets, num_rx);
	make_widget_update_signal_based(tx_widgets, num_tx);

	iio_spin_button_set_on_complete_function(&rx_widgets[rx_sample_freq_pair],
		rx_sample_frequency_changed_cb, NULL);
	iio_spin_button_set_on_complete_function(&tx_widgets[tx_sample_freq_pair],
		tx_sample_frequency_changed_cb, NULL);
	for (i = 0; i < 2; i++) {
		iio_spin_button_set_on_complete_function(&rx_widgets[rx_lo[i]],
			sample_frequency_changed_cb, NULL);
		iio_spin_button_set_on_complete_function(&tx_widgets[tx_lo[i]],
			sample_frequency_changed_cb, NULL);
		}
	if (!tx_rssi_available) {
		for (i = 1; i < 5; i++)
			gtk_widget_hide(tx_rssi[i]);
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "label_rssi_tx1")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "label_rssi_tx2")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "label_rssi_tx3")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "label_rssi_tx4")));
	}

	iio_update_widgets(glb_widgets, num_glb);
	tx_update_values();
	rx_update_values();
	filter_fir_update();
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(disable_all_fir_filters), true);
	glb_settings_update_labels();
	rssi_update_labels();
	dac_data_manager_update_iio_widgets(dac_tx_manager);

	add_ch_setup_check_fct("cf-ad9361-lpc", channel_combination_check);

	struct iio_device *adc_dev;
	struct extra_dev_info *adc_info;

	adc_dev = iio_context_find_device(get_context_from_osc(), CAP_DEVICE1);
	if (!adc_dev)
		adc_dev = iio_context_find_device(get_context_from_osc(), CAP_DEVICE1_ALT);
	if (adc_dev) {
		adc_info = iio_device_get_data(adc_dev);
		if (adc_info)
			adc_info->plugin_fft_corr = 20 * log10(1/sqrt(HANNING_ENBW));
	}

	block_diagram_init(builder, 2, "AD9361.svg", "AD_FMCOMMS5_EBZ.jpg");

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(filter_fir_config), OSC_FILTER_FILE_PATH);

	dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager, OSC_WAVEFORM_FILE_PATH);
	dac_data_manager_set_buffer_size_alignment(dac_tx_manager, 16);

	g_timeout_add(1000, (GSourceFunc) update_display, ctx);
	can_update_widgets = true;

	return fmcomms5_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void fmcomms5_get_preferred_size(const struct osc_plugin *plugin, int *width, int *height)
{
	if (width)
		*width = 1100;
	if (height)
		*height = 800;
}

static void save_widgets_to_ini(FILE *f)
{
	fprintf(f, "load_fir_filter_file = %s\n"
			"dds_mode_tx1 = %i\n"
			"dds_mode_tx2 = %i\n"
			"dds_mode_tx3 = %i\n"
			"dds_mode_tx4 = %i\n"
			"dac_buf_filename = %s\n"
			"tx_channel_0 = %i\n"
			"tx_channel_1 = %i\n"
			"tx_channel_2 = %i\n"
			"tx_channel_3 = %i\n"
			"tx_channel_4 = %i\n"
			"tx_channel_5 = %i\n"
			"tx_channel_6 = %i\n"
			"tx_channel_7 = %i\n"
			"global_settings_show = %i\n"
			"tx_show = %i\n"
			"rx_show = %i\n"
			"fpga_show = %i\n",
			last_fir_filter,
			dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE1, 1),
			dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE1, 2),
			dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE2, 1),
			dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE2, 2),
			dac_data_manager_get_buffer_chooser_filename(dac_tx_manager),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 0),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 1),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 2),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 3),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 4),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 5),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 6),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 7),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_GLOBAL]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_TX]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_RX]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_FPGA]));
}


static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		save_to_ini(f, THIS_DRIVER, dev1, fmcomms5_sr_attribs,
				ARRAY_SIZE(fmcomms5_sr_attribs));
		save_to_ini(f, NULL, dds1, fmcomms5_sr_attribs,
				ARRAY_SIZE(fmcomms5_sr_attribs));
		save_to_ini(f, NULL, cap1, fmcomms5_sr_attribs,
				ARRAY_SIZE(fmcomms5_sr_attribs));
		save_to_ini(f, NULL, dev2, fmcomms5_sr_attribs,
				ARRAY_SIZE(fmcomms5_sr_attribs));
		save_to_ini(f, NULL, dds2, fmcomms5_sr_attribs,
				ARRAY_SIZE(fmcomms5_sr_attribs));
		save_to_ini(f, NULL, cap2, fmcomms5_sr_attribs,
				ARRAY_SIZE(fmcomms5_sr_attribs));
		save_widgets_to_ini(f);
		fclose(f);
	}
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	g_source_remove_by_user_data(ctx);

	if (ini_fn)
		save_profile(NULL, ini_fn);

	if (dac_tx_manager) {
		dac_data_manager_free(dac_tx_manager);
		dac_tx_manager = NULL;
	}

	osc_destroy_context(ctx);
}

struct osc_plugin plugin;

static bool fmcomms5_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	dev1 = iio_context_find_device(osc_ctx, PHY_DEVICE1);
	dds1 = iio_context_find_device(osc_ctx, DDS_DEVICE1);
	cap1 = iio_context_find_device(osc_ctx, CAP_DEVICE1);
	dev2 = iio_context_find_device(osc_ctx, PHY_DEVICE2);
	dds2 = iio_context_find_device(osc_ctx, DDS_DEVICE2);
	cap2 = iio_context_find_device(osc_ctx, CAP_DEVICE2);

	if (!cap1) {
		cap1 = iio_context_find_device(osc_ctx, CAP_DEVICE1_ALT);
	}

	return !!dev1 && !!dds1 && !!cap1 && !!dev2 && !!dds2 && !!cap2;
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = fmcomms5_identify,
	.init = fmcomms5_init,
	.handle_item = fmcomms5_handle,
	.handle_external_request = handle_external_request,
	.update_active_page = update_active_page,
	.get_preferred_size = fmcomms5_get_preferred_size,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
