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

#include <ad9361.h>
#include <iio.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../libini2.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "../fru.h"
#include "../iio_utils.h"
#include "block_diagram.h"
#include "dac_data_manager.h"
#include "fir_filter.h"
#include "scpi.h"

#define HANNING_ENBW 1.50

#define THIS_DRIVER "AD936X"
#define PHY_DEVICE "ad9361-phy"
#define DDS_DEVICE "cf-ad9361-dds-core-lpc"
#define CAP_DEVICE "cf-ad9361-lpc"
#define UDC_RX_DEVICE "adf4351-udc-rx-pmod"
#define UDC_TX_DEVICE "adf4351-udc-tx-pmod"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

#define MHZ_TO_HZ(x) ((x) * 1000000ul)

#define REFCLK_RATE 40000000

extern bool dma_valid_selection(const char *device, unsigned mask, unsigned channel_count);

static struct dac_data_manager *dac_tx_manager;

static bool is_2rx_2tx;
static bool has_udc_driver;
static bool can_update_widgets;
static bool tx_rssi_available;

static const gdouble mhz_scale = 1000000.0;
static const gdouble inv_scale = -1.0;

static const char *freq_name;

static volatile int auto_calibrate = 0;
static unsigned int dcxo_coarse_num, dcxo_fine_num;
struct tuning_param
{
	double frequency;
	int coarse;
	int fine;
};

static struct iio_widget widgets[100];
static struct iio_widget *glb_widgets, *tx_widgets, *rx_widgets, *fpga_widgets;
static unsigned int rx1_gain, rx2_gain;
static unsigned int num_glb, num_tx, num_rx, num_fpga;
static unsigned int rx_lo, tx_lo;
static unsigned int rx_sample_freq, tx_sample_freq;
static double updn_freq_span;
static double updn_freq_mix_sign;
static char last_fir_filter[PATH_MAX];
static char *rx_fastlock_store_name, *rx_fastlock_recall_name;
static char *tx_fastlock_store_name, *tx_fastlock_recall_name;

static struct iio_context *ctx;
static struct iio_device *dev, *dds, *cap, *udc_rx, *udc_tx;

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
static GtkWidget *up_down_converter;
static GtkWidget *dcxo_cal_progressbar;
static GtkWidget *dcxo_cal_type;
static GtkWidget *dcxo_cal;
static GtkWidget *enable_auto_filter;
static GtkWidget *dcxo_cal_tab;

/* Widgets for Receive Settings */
static GtkWidget *rx_gain_control_rx1;
static GtkWidget *rx_gain_control_modes_rx1;
static GtkWidget *rf_port_select_rx;
static GtkWidget *rx_gain_control_rx2;
static GtkWidget *rx_gain_control_modes_rx2;
static GtkWidget *rx1_rssi;
static GtkWidget *rx2_rssi;
static GtkWidget *tx1_rssi;
static GtkWidget *tx2_rssi;
static GtkWidget *rx_path_rates;
static GtkWidget *tx_path_rates;
static GtkWidget *fir_filter_en_tx;
static GtkWidget *enable_fir_filter_rx;
static GtkWidget *enable_fir_filter_rx_tx;
static GtkWidget *disable_all_fir_filters;
static GtkWidget *rf_port_select_tx;
static GtkWidget *rx_fastlock_profile;
static GtkWidget *tx_fastlock_profile;
static GtkWidget *rx_phase_rotation[2];

static GtkWidget *fpga_tx_frequency_available;
static GtkWidget *fpga_rx_frequency_available;

static GtkWidget *sampling_freq_rx_decim;
static GtkWidget *sampling_freq_tx_inter;

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *fmcomms2_panel;
static gboolean plugin_detached;

static const char *fmcomms2_sr_attribs[] = {
	PHY_DEVICE".trx_rate_governor",
	PHY_DEVICE".dcxo_tune_coarse",
	PHY_DEVICE".dcxo_tune_fine",
	PHY_DEVICE".xo_correction",
	PHY_DEVICE".ensm_mode",
	PHY_DEVICE".in_voltage0_rf_port_select",
	PHY_DEVICE".in_voltage0_gain_control_mode",
	PHY_DEVICE".in_voltage0_hardwaregain",
	PHY_DEVICE".in_voltage1_gain_control_mode",
	PHY_DEVICE".in_voltage1_hardwaregain",
	PHY_DEVICE".in_voltage_bb_dc_offset_tracking_en",
	PHY_DEVICE".in_voltage_quadrature_tracking_en",
	PHY_DEVICE".in_voltage_rf_dc_offset_tracking_en",
	PHY_DEVICE".out_voltage0_rf_port_select",
	PHY_DEVICE".out_altvoltage0_RX_LO_external",
	PHY_DEVICE".out_altvoltage1_TX_LO_external",
	PHY_DEVICE".out_altvoltage0_RX_LO_frequency",
	PHY_DEVICE".out_altvoltage1_TX_LO_frequency",
	PHY_DEVICE".out_voltage0_hardwaregain",
	PHY_DEVICE".out_voltage1_hardwaregain",
	PHY_DEVICE".out_voltage_sampling_frequency",
	PHY_DEVICE".in_voltage_rf_bandwidth",
	PHY_DEVICE".out_voltage_rf_bandwidth",
	PHY_DEVICE".in_voltage_filter_fir_en",
	PHY_DEVICE".out_voltage_filter_fir_en",
	PHY_DEVICE".in_out_voltage_filter_fir_en",

	DDS_DEVICE".out_altvoltage0_TX1_I_F1_frequency",
	DDS_DEVICE".out_altvoltage0_TX1_I_F1_phase",
	DDS_DEVICE".out_altvoltage0_TX1_I_F1_scale",
	DDS_DEVICE".out_altvoltage1_TX1_I_F2_frequency",
	DDS_DEVICE".out_altvoltage1_TX1_I_F2_phase",
	DDS_DEVICE".out_altvoltage1_TX1_I_F2_scale",
	DDS_DEVICE".out_altvoltage2_TX1_Q_F1_frequency",
	DDS_DEVICE".out_altvoltage2_TX1_Q_F1_phase",
	DDS_DEVICE".out_altvoltage2_TX1_Q_F1_scale",
	DDS_DEVICE".out_altvoltage3_TX1_Q_F2_frequency",
	DDS_DEVICE".out_altvoltage3_TX1_Q_F2_phase",
	DDS_DEVICE".out_altvoltage3_TX1_Q_F2_scale",
	DDS_DEVICE".out_altvoltage4_TX2_I_F1_frequency",
	DDS_DEVICE".out_altvoltage4_TX2_I_F1_phase",
	DDS_DEVICE".out_altvoltage4_TX2_I_F1_scale",
	DDS_DEVICE".out_altvoltage5_TX2_I_F2_frequency",
	DDS_DEVICE".out_altvoltage5_TX2_I_F2_phase",
	DDS_DEVICE".out_altvoltage5_TX2_I_F2_scale",
	DDS_DEVICE".out_altvoltage6_TX2_Q_F1_frequency",
	DDS_DEVICE".out_altvoltage6_TX2_Q_F1_phase",
	DDS_DEVICE".out_altvoltage6_TX2_Q_F1_scale",
	DDS_DEVICE".out_altvoltage7_TX2_Q_F2_frequency",
	DDS_DEVICE".out_altvoltage7_TX2_Q_F2_phase",
	DDS_DEVICE".out_altvoltage7_TX2_Q_F2_scale",

	UDC_RX_DEVICE".out_altvoltage0_frequency",
	UDC_TX_DEVICE".out_altvoltage0_frequency",
};

static const char * fmcomms2_driver_attribs[] = {
	"load_fir_filter_file",
	"dds_mode_tx1",
	"dds_mode_tx2",
	"global_settings_show",
	"tx_show",
	"rx_show",
	"fpga_show",
	"up_down_converter",
	"tx_channel_0",
	"tx_channel_1",
	"tx_channel_2",
	"tx_channel_3",
	"dac_buf_filename",
};

static void glb_settings_update_labels(void)
{
	float rates[6];
	char tmp[160], buf[1024];
	ssize_t ret;

	ret = iio_device_attr_read(dev, "ensm_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(ensm_mode), buf);
	else
		gtk_label_set_text(GTK_LABEL(ensm_mode), "<error>");

	ret = iio_device_attr_read(dev, "calib_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(calib_mode), buf);
	else
		gtk_label_set_text(GTK_LABEL(calib_mode), "<error>");

	ret = iio_device_attr_read(dev, "trx_rate_governor", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(trx_rate_governor), buf);
	else
		gtk_label_set_text(GTK_LABEL(trx_rate_governor), "<error>");

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev, "voltage0", false),
			"gain_control_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(rx_gain_control_rx1), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx_gain_control_rx1), "<error>");

	if (is_2rx_2tx) {
		ret = iio_channel_attr_read(
				iio_device_find_channel(dev, "voltage1", false),
				"gain_control_mode", buf, sizeof(buf));
		if (ret > 0)
			gtk_label_set_text(GTK_LABEL(rx_gain_control_rx2), buf);
		else
			gtk_label_set_text(GTK_LABEL(rx_gain_control_rx2), "<error>");
	}

	ret = iio_device_attr_read(dev, "rx_path_rates", buf, sizeof(buf));
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

	ret = iio_device_attr_read(dev, "tx_path_rates", buf, sizeof(buf));
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

	iio_widget_update(&rx_widgets[rx1_gain]);
	if (is_2rx_2tx)
		iio_widget_update(&rx_widgets[rx2_gain]);
}

static void rf_port_select_rx_changed_cb(GtkComboBoxText *cmb, gpointer data)
{
	gchar *port_name;
	bool tx1 = false, tx2 = false;

	port_name = gtk_combo_box_text_get_active_text(cmb);
	if (!port_name)
		return;

	if (!strcmp(port_name, "TX_MONITOR1")) {
		tx1 = true;
	} else if (!strcmp(port_name, "TX_MONITOR2")) {
		tx2 = true;
	} else if (!strcmp(port_name, "TX_MONITOR1_2")) {
		tx1 = tx2 = true;
	}
	gtk_widget_set_visible(tx1_rssi, tx1);
	gtk_widget_set_visible(tx2_rssi, tx2);

	g_free(port_name);
}

static void rx_freq_info_update(void)
{
	double lo_freq;

	if (cap)
		rx_update_device_sampling_freq(CAP_DEVICE,
						USE_INTERN_SAMPLING_FREQ);
	lo_freq = mhz_scale * gtk_spin_button_get_value(
			GTK_SPIN_BUTTON(rx_widgets[rx_lo].widget));
	if (cap)
		rx_update_channel_lo_freq(CAP_DEVICE, "all", lo_freq);
}

static void int_dec_freq_update(void)
{
	struct iio_channel *ch;
	double freq;

	ch = iio_device_find_channel(cap, "voltage0", false);
	iio_channel_attr_read_double(ch, "sampling_frequency", &freq);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(sampling_freq_rx_decim), freq / mhz_scale);

	ch = iio_device_find_channel(dds, "voltage0", true);
	iio_channel_attr_read_double(ch, "sampling_frequency", &freq);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(sampling_freq_tx_inter), freq / mhz_scale);
}

static void sample_frequency_changed_cb(void *data)
{
	glb_settings_update_labels();
	rx_freq_info_update();
	iio_update_widgets(fpga_widgets, num_fpga);
	int_dec_freq_update();
}

static double get_gui_tx_sampling_freq(void)
{
	return gtk_spin_button_get_value(GTK_SPIN_BUTTON(tx_widgets[tx_sample_freq].widget));
}

static void filter_fir_update(void); /* forwrad declaration */

static void tx_sample_frequency_changed_cb(void *data)
{
	double rate;
	bool auto_fir;

	/* Skip rx_sample_freq changed, since RX and TX rates are always the same */
	if (!data)
		return;

	auto_fir = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (enable_auto_filter));
	rate = get_gui_tx_sampling_freq();

	if (auto_fir) {
		ad9361_set_bb_rate (dev, (unsigned long) (rate * 1000000));
		gtk_widget_show(enable_fir_filter_rx_tx);
		gtk_widget_show(disable_all_fir_filters);
		filter_fir_update();
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(filter_fir_config), "(None)");
	} else {
		iio_spin_button_save(&tx_widgets[tx_sample_freq]);
	}
	/* We've set the sampling freq. Now read back the value and update the widget. */
	iio_widget_update(&tx_widgets[tx_sample_freq]);

	dac_data_manager_freq_widgets_range_update(dac_tx_manager, rate / 2.0);
	sample_frequency_changed_cb(NULL);
}

static void rssi_update_label(GtkWidget *label, const char *chn,  bool is_tx)
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
	rssi_update_label(rx1_rssi, "voltage0", false);
	if (tx_rssi_available)
		rssi_update_label(tx1_rssi, "voltage0", true);
	if (is_2rx_2tx) {
		rssi_update_label(rx2_rssi, "voltage1", false);
		if (tx_rssi_available)
			rssi_update_label(tx2_rssi, "voltage1", true);
	}
}

static gboolean update_display(gpointer foo)
{
	if (this_page == gtk_notebook_get_current_page(nbook) || plugin_detached) {
		const char *gain_mode;

		rssi_update_labels();
		gain_mode = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(rx_gain_control_modes_rx1));
		if (gain_mode && strcmp(gain_mode, "manual"))
			iio_widget_update(&rx_widgets[rx1_gain]);

		gain_mode = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(rx_gain_control_modes_rx2));
		if (is_2rx_2tx && gain_mode && strcmp(gain_mode, "manual"))
			iio_widget_update(&rx_widgets[rx2_gain]);
	}

	return TRUE;
}

const double RX_CENTER_FREQ = 340; /* MHz */
const double TX_CENTER_FREQ = 370; /* MHz */

static double get_span_multiple_from(double frequency, double span)
{
	double num = 0;

	if (span <= 0)
		return 0;

	while (num <= frequency)
		num += span;

	return (num - span);
}

static int split_target_lo_freq(double target_freq, double *ext_pll, double *ad9361_lo,
		double span, const double center_freq)
{
	double small_freq, large_freq;

	large_freq = get_span_multiple_from(target_freq, span);
	small_freq = target_freq - large_freq;

	*ad9361_lo = center_freq - small_freq * updn_freq_mix_sign;
	*ext_pll = center_freq + large_freq * updn_freq_mix_sign;

	return 0;
}

#define UPDN_RX 1
#define UPDN_TX 2

static void updn_converter_lo_freq_changed_cb(GtkSpinButton *button, int data)
{
	struct iio_channel *ad9361_ch, *updn_ch;
	double target_freq, ad9361_lo, updn_pll, center_freq;
	int ret;

	if (data == UPDN_RX) {
		ad9361_ch = iio_device_find_channel(dev, "altvoltage0", true);
		updn_ch = iio_device_find_channel(udc_rx, "altvoltage0", true);
		center_freq = RX_CENTER_FREQ;
	} else if (data == UPDN_TX) {
		ad9361_ch = iio_device_find_channel(dev, "altvoltage1", true);
		updn_ch = iio_device_find_channel(udc_tx, "altvoltage0", true);
		center_freq = TX_CENTER_FREQ;
	} else {
		return;
	}

	target_freq = gtk_spin_button_get_value(button);
	split_target_lo_freq(target_freq, &updn_pll, &ad9361_lo, updn_freq_span, center_freq);
	ret = iio_channel_attr_write_longlong(ad9361_ch, freq_name, (long long)MHZ_TO_HZ(ad9361_lo));
	if (ret < 0)
		fprintf(stderr,"Write to %s attribute of %s device: %s\n",
			freq_name, PHY_DEVICE, strerror(-ret));
	ret = iio_channel_attr_write_longlong(updn_ch, "frequency", (long long)MHZ_TO_HZ(updn_pll));
	if (ret < 0)
		fprintf(stderr,"Write to %s attribute of %s device: %s\n",
			"frequency", (UPDN_TX) ? UDC_TX_DEVICE : UDC_RX_DEVICE, strerror(-ret));
	rx_freq_info_update();
}

#define UPDN_LO_FREQ_MIN 1   /* MHz */
#define UPDN_LO_FREQ_MAX 120 /* MHz */

static void up_down_converter_toggled_cb(GtkToggleButton *button, gpointer data)
{
	static gint rx_updn_hid, tx_updn_hid;
	static gdouble lo_min, lo_max;
	static void (*rx_lo_update_value)(struct iio_widget *, const char *, size_t);
	static void (*tx_lo_update_value)(struct iio_widget *, const char *, size_t);

	if (gtk_toggle_button_get_active(button)) {
		iio_spin_button_progress_deactivate(&rx_widgets[rx_lo]);
		iio_spin_button_progress_deactivate(&tx_widgets[tx_lo]);
		rx_updn_hid = g_signal_connect(rx_widgets[rx_lo].widget, "value-changed",
			G_CALLBACK(updn_converter_lo_freq_changed_cb), (gpointer)UPDN_RX);
		tx_updn_hid = g_signal_connect(tx_widgets[tx_lo].widget, "value-changed",
			G_CALLBACK(updn_converter_lo_freq_changed_cb), (gpointer)UPDN_TX);
		gtk_spin_button_get_range(GTK_SPIN_BUTTON(rx_widgets[rx_lo].widget), &lo_min, &lo_max);
		gtk_spin_button_set_range(GTK_SPIN_BUTTON(rx_widgets[rx_lo].widget), UPDN_LO_FREQ_MIN, UPDN_LO_FREQ_MAX);
		gtk_spin_button_set_range(GTK_SPIN_BUTTON(tx_widgets[tx_lo].widget), UPDN_LO_FREQ_MIN, UPDN_LO_FREQ_MAX);
		rx_lo_update_value = rx_widgets[rx_lo].update_value;
		tx_lo_update_value = tx_widgets[tx_lo].update_value;
		rx_widgets[rx_lo].update_value = NULL;
		tx_widgets[tx_lo].update_value = NULL;
	} else {
		g_signal_handler_disconnect(rx_widgets[rx_lo].widget, rx_updn_hid);
		g_signal_handler_disconnect(tx_widgets[tx_lo].widget, tx_updn_hid);
		rx_widgets[rx_lo].update_value = rx_lo_update_value;
		tx_widgets[tx_lo].update_value = tx_lo_update_value;
		iio_spin_button_progress_activate(&rx_widgets[rx_lo]);
		iio_spin_button_progress_activate(&tx_widgets[tx_lo]);
		g_signal_emit_by_name(rx_widgets[rx_lo].widget,
			"value-changed", NULL);
		g_signal_emit_by_name(tx_widgets[tx_lo].widget,
			"value-changed", NULL);
		gtk_spin_button_set_range(GTK_SPIN_BUTTON(rx_widgets[rx_lo].widget), lo_min, lo_max);
		gtk_spin_button_set_range(GTK_SPIN_BUTTON(tx_widgets[tx_lo].widget), lo_min, lo_max);
	}
}

static void rx_phase_rotation_update()
{
	struct iio_channel *out[4];
	gdouble val[4];
	int i, d = 0;

	if (!cap)
		return;

	out[0] = iio_device_find_channel(cap, "voltage0", false);
	out[1] = iio_device_find_channel(cap, "voltage1", false);

	if (is_2rx_2tx) {
		out[2] = iio_device_find_channel(cap, "voltage2", false);
		out[3] = iio_device_find_channel(cap, "voltage3", false);
		d = 2;
	}

	for (i = 0; i <= d; i += 2) {
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

		gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_phase_rotation[i/2]), val[0]);
	}
}

static void dcxo_widgets_update(void)
{
	char val[64];
	int ret;

	ret = iio_device_attr_read(dev, "dcxo_tune_coarse", val, sizeof(val));

	if (ret < 0)
		gtk_widget_hide(dcxo_cal_tab);
	else
		gtk_widget_show(dcxo_cal_tab);
}

static void int_dec_update_cb(GtkComboBox *cmb, gpointer data)
{
	if (gtk_combo_box_get_active(cmb) > 0)
		gtk_widget_show(GTK_WIDGET(data));
	else
		gtk_widget_hide(GTK_WIDGET(data));

	int_dec_freq_update();
	rx_freq_info_update();
}

static void int_dec_spin_update_cb(GtkSpinButton *spin, gpointer data)
{
	struct iio_channel *chn = data;
	double fpga, freq, trx;

	freq = mhz_scale * gtk_spin_button_get_value(spin);
	iio_channel_attr_read_double(chn, "sampling_frequency", &fpga);
	trx = mhz_scale * gtk_spin_button_get_value(
		GTK_SPIN_BUTTON(tx_widgets[tx_sample_freq].widget));

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_widgets[tx_sample_freq].widget),
		 (trx/fpga * freq) / mhz_scale);

}

static void update_widgets(void)
{
	iio_update_widgets_of_device(widgets, num_glb + num_tx + num_rx, dev);
	if (dds)
		iio_update_widgets_of_device(fpga_widgets, num_fpga, dds);
	if (cap)
		iio_update_widgets_of_device(fpga_widgets, num_fpga, cap);
	dac_data_manager_update_iio_widgets(dac_tx_manager);
	dcxo_widgets_update();
}

static void filter_fir_update(void)
{
	bool rx = false, tx = false, rxtx;
	struct iio_channel *chn;
	int stat;

	ad9361_get_trx_fir_enable(dev, &stat);
	rxtx = !!stat;

	chn = iio_device_find_channel(dev, "voltage0", false);
	if (chn)
		iio_channel_attr_read_bool(chn, "filter_fir_en", &rx);
	chn = iio_device_find_channel(dev, "voltage0", true);
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
}

static void filter_fir_enable(GtkToggleButton *button, gpointer data)
{
	bool rx, tx, rxtx, disable;

	if (gtk_toggle_button_get_active(button))
		return;

	rx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx));
	tx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (fir_filter_en_tx));
	rxtx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx_tx));
	disable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (disable_all_fir_filters));

	if (rxtx || disable) {
		ad9361_set_trx_fir_enable(dev, rxtx);
	} else {
		struct iio_channel *chn;
		if (rx) {
			chn = iio_device_find_channel(dev, "voltage0", true);
			if (chn)
				iio_channel_attr_write_bool(chn, "filter_fir_en", tx);

			chn = iio_device_find_channel(dev, "voltage0", false);
			if (chn)
				iio_channel_attr_write_bool(chn, "filter_fir_en", rx);

		}

		if (tx) {
			chn = iio_device_find_channel(dev, "voltage0", false);
			if (chn)
				iio_channel_attr_write_bool(chn, "filter_fir_en", rx);

			chn = iio_device_find_channel(dev, "voltage0", true);
			if (chn)
				iio_channel_attr_write_bool(chn, "filter_fir_en", tx);

		}
	}

	if (plugin_osc_running_state() == true) {
		plugin_osc_stop_capture();
		plugin_osc_start_capture();
	}

	filter_fir_update();
	glb_settings_update_labels();
	update_widgets();
	rx_freq_info_update();
}

static void reload_button_clicked(GtkButton *btn, gpointer data)
{
	update_widgets();

	filter_fir_update();
	rx_freq_info_update();
	glb_settings_update_labels();
	rssi_update_labels();
	rx_phase_rotation_update();
}

#ifndef _WIN32
static int dcxo_cal_to_eeprom_clicked(GtkButton *btn, gpointer data)
{
	unsigned coarse, fine;
	char cmd[256];
	const char *eeprom_path = find_eeprom(NULL);
	FILE *fp = NULL;
	const char *failure_msg = NULL;
	int ret = 0;

	if (!eeprom_path) {
		failure_msg = "Can't find EEPROM file in the sysfs";
		goto cleanup;
	}

	coarse = gtk_spin_button_get_value(GTK_SPIN_BUTTON(glb_widgets[dcxo_coarse_num].widget));
	fine = gtk_spin_button_get_value(GTK_SPIN_BUTTON(glb_widgets[dcxo_fine_num].widget));
	sprintf(cmd, "fru-dump -i \"%s\" -o \"%s\" -t %.02x%.04x 2>&1", eeprom_path,
			eeprom_path, coarse, fine);
	fp = popen(cmd, "r");

	if (!fp || pclose(fp) != 0) {
		failure_msg = "Error running fru-dump to write to EEPROM";
		fprintf(stderr, "Error running fru-dump: %s\n", cmd);
		goto cleanup;
	}

cleanup:
	if (failure_msg) {
		GtkWidget *toplevel = gtk_widget_get_toplevel(fmcomms2_panel);
		if (!gtk_widget_is_toplevel(toplevel))
			toplevel = NULL;
		GtkWidget *dcxo_cal_eeprom_fail = gtk_message_dialog_new(
			GTK_WINDOW(toplevel),
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE,
			"%s", failure_msg);
		gtk_window_set_title(GTK_WINDOW(dcxo_cal_eeprom_fail), "Save to EEPROM");
		if (gtk_dialog_run(GTK_DIALOG(dcxo_cal_eeprom_fail)))
			gtk_widget_destroy(dcxo_cal_eeprom_fail);
		ret = -1;
	}

	g_free((void *)eeprom_path);

	return ret;
}

static int dcxo_cal_from_eeprom_clicked(GtkButton *btn, gpointer data)
{
	const char *eeprom_path = find_eeprom(NULL);
	unsigned char *raw_eeprom = NULL;
	struct FRU_DATA *fru = NULL;
	FILE *fp;
	size_t bytes;
	const char *failure_msg = NULL;
	char coarse_str[3], fine_str[5];
	int coarse, fine, ret = 0;

	if (!eeprom_path) {
		failure_msg = "Can't find EEPROM file in the sysfs";
		goto cleanup;
	}

	fp = fopen(eeprom_path, "rb");
	if (!fp) {
		failure_msg = "Can't open EEPROM file";
		goto cleanup;
	}

	raw_eeprom = g_malloc(FAB_SIZE_FRU_EEPROM);
	bytes = fread(raw_eeprom, 1, FAB_SIZE_FRU_EEPROM, fp);

	/* FRU format specifies a 256 byte file size. */
	if (ferror(fp) || bytes != FAB_SIZE_FRU_EEPROM) {
		failure_msg = "Failed to read EEPROM file";
		fclose(fp);
		goto cleanup;
	}
	fclose(fp);

	fru = parse_FRU(raw_eeprom);
	if (!fru) {
		failure_msg = "Failed to parse EEPROM";
		goto cleanup;
	}

	/* The tuning parameters are stored as a single, concatenated hex string,
	 * with the first two characters being the coarse value and the last four
	 * characters being the fine value.
	 *
	 * Note that there are two header bytes that must be skipped first.
	 */
	memcpy(coarse_str, &fru->Board_Area->custom[4][2], 2);
	coarse_str[2] = '\0';
	memcpy(fine_str, &fru->Board_Area->custom[4][4], 4);
	fine_str[4] = '\0';

	coarse = strtol(coarse_str, NULL, 16);
	fine = strtol(fine_str, NULL, 16);
	if (errno == ERANGE || errno == EINVAL) {
		failure_msg = "Failed parsing coarse and/or fine values from EEPROM";
		goto cleanup;
	}

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(
		glb_widgets[dcxo_coarse_num].widget), coarse);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(
		glb_widgets[dcxo_fine_num].widget), fine);

cleanup:
	if (failure_msg) {
		GtkWidget *toplevel = gtk_widget_get_toplevel(fmcomms2_panel);
		if (!gtk_widget_is_toplevel(toplevel))
			toplevel = NULL;
		GtkWidget *dcxo_cal_eeprom_fail = gtk_message_dialog_new(
			GTK_WINDOW(toplevel),
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE,
			"%s", failure_msg);
		gtk_window_set_title(GTK_WINDOW(dcxo_cal_eeprom_fail), "Load from EEPROM");
		if (gtk_dialog_run(GTK_DIALOG(dcxo_cal_eeprom_fail)))
			gtk_widget_destroy(dcxo_cal_eeprom_fail);
		ret = -1;
	}

	g_free((void *)eeprom_path);
	g_free(raw_eeprom);
	g_free(fru);

	return ret;
}

static int xo_freq_to_eeprom(void)
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
		GtkWidget *toplevel = gtk_widget_get_toplevel(fmcomms2_panel);
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

static int dcxo_cal_clicked(GtkButton *btn, gpointer data)
{
	double current_freq, target_freq = 0, diff = 0, orig_diff = 0, prev_diff = 0;
	int coarse = 0, fine = 4095, tune_step = 1, direction = 0, ret = 0;
	GQueue *tuning_elems = NULL;
	struct tuning_param *tuning_elem = NULL;
	bool fine_tune = false;
	char *failure_msg = NULL;

	FILE *fp;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn)))
		goto dcxo_cleanup;

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dcxo_cal_type))) {
		case 0: /* REFCLK */
			/* Force the correct clock output mode. */
			iio_device_debug_attr_write_longlong(dev, "adi,clk-output-mode-select", 1);
			iio_device_debug_attr_write_longlong(dev, "initialize", 1);

			if (!strcmp(iio_context_get_name(ctx), "network")) {
				target_freq = REFCLK_RATE;
			} else if (!strcmp(iio_context_get_name(ctx), "local")) {
				fp = fopen("/sys/kernel/debug/clk/ad9361_ext_refclk/clk_rate", "r");
				if (!fp || fscanf(fp, "%lf", &target_freq) != 1) {
					failure_msg = "Unable to read AD9361 reference clock rate from debugfs.";
					if (fp)
						fclose(fp);
					goto dcxo_cleanup;
				}
				if (fp)
					fclose(fp);
			} else {
				failure_msg = "AD9361 Reference clock rate missing from debugfs.";
				goto dcxo_cleanup;
			}
			break;
		case 1: /* RF Output */
			target_freq = mhz_scale * (
				gtk_spin_button_get_value(GTK_SPIN_BUTTON(tx_widgets[tx_lo].widget)) +
				gtk_spin_button_get_value(GTK_SPIN_BUTTON(
					dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_1, TONE_I), WIDGET_FREQUENCY))));
			break;
		case 2: /* RF Input */
			failure_msg = "RF Input is not supported yet for DCXO calibration.";
			goto dcxo_cleanup;
			break;
		default:
			failure_msg = "Unsupported calibration method selected.";
			goto dcxo_cleanup;
	}

	if (scpi_connect_counter() != 0) {
		failure_msg = "Failed to connect to Programmable Counter device.";
		goto dcxo_cleanup;
	}

	/* Alter toggle button text on start and disable user input for certain
	 * widgets during calibration.
	 */
	gtk_button_set_label(btn, "Stop calibration");
	gtk_widget_set_sensitive(dcxo_cal_type, FALSE);
	gtk_widget_set_sensitive(glb_widgets[dcxo_coarse_num].widget, FALSE);
	gtk_widget_set_sensitive(glb_widgets[dcxo_fine_num].widget, FALSE);

	tuning_elems = g_queue_new();
	target_freq = roundf(target_freq);

	while (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) {
		gtk_widget_show(dcxo_cal_progressbar);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(glb_widgets[dcxo_coarse_num].widget), coarse);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(glb_widgets[dcxo_fine_num].widget), fine);
		dcxo_widgets_update();
		while (gtk_events_pending())
			gtk_main_iteration();

		/* Querying frequency counters via SCPI too quickly leads to failures. */
		sleep(1);

		if (scpi_counter_get_freq(&current_freq, &target_freq) != 0) {
			failure_msg = "Error retrieving counter frequency. "
				"Make sure the counter has the correct input attached.";
			goto dcxo_cleanup;
		}

		/* Sometimes the frequency counter returns entirely wrong values that
		 * are orders of magnitude off. In those cases we trigger a new
		 * measurement request and hope the device returns the correct value
		 * this time.
		 */
		if (prev_diff != 0 && fabs(target_freq - current_freq) > 10 * fabs(prev_diff)) {
			fprintf(stderr, "Skipping likely erroneous response from SCPI device. "
				"Previous difference to target frequency was %lf Hz, possible bad value's "
				"difference is %lf Hz.\n", prev_diff, (target_freq - current_freq));
			continue;
		}

		prev_diff = diff;
		diff = target_freq - current_freq;

		/* Show progress towards the target frequency in relation to the
		 * original frequency measurement.
		 */
		if (orig_diff == 0) {
			orig_diff = fabs(diff);
			direction = (int)fabs(diff)/diff;
		} else {
			gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(
				dcxo_cal_progressbar), (orig_diff-fabs(diff))/orig_diff);
		}

		/* Store the past ten tuning value pairs and related frequencies. This
		 * is used to determine the final values that are the closest to the
		 * target frequency.
		 */
		if (g_queue_get_length(tuning_elems) >= 10)
			g_queue_pop_head(tuning_elems);
		tuning_elem = g_new(struct tuning_param, 1);
		tuning_elem->frequency = current_freq;
		tuning_elem->coarse = coarse;
		tuning_elem->fine = fine;
		g_queue_push_tail(tuning_elems, tuning_elem);

		if (fine_tune) {
			/* Stop once we go past our target frequency. */
			if (diff != 0) {
				if (direction < 0 && current_freq < target_freq)
					break;
				else if (direction > 0 && current_freq > target_freq)
					break;
			}

			tune_step = (int)roundf(-1 * (diff / 2));

			/* Force the next tuning step to be at least positive or negative 1. */
			if (tune_step == 0)
				tune_step = -1 * direction;

			fine += tune_step;
		} else {
			/* Do a binary search for the closest approaching coarse tune value
			 * in relation to the target frequency. When the difference to the
			 * target frequency is below another coarse tune step, switch to
			 * fine tuning.
			 */
			if (tune_step != 0) {
				if (prev_diff != 0)
					tune_step = (int)nearbyintf(-1 * ((tune_step * diff) / fabs(diff - prev_diff)) / 2);
				coarse += tune_step;
			} else {
				fine_tune = true;
			}
		}

		if (coarse < 0 || coarse > 63 || fine < 0 || fine > 8191) {
			failure_msg = "Outside of tuning bounds. Make sure you have the "
				"correct calibration method selected.\n";
			goto dcxo_cleanup;
		}
	}

	/* Determine the median tuning value from the list of acceptable values.
	 * Values are first removed from the beginning of the queue if they have a
	 * higher difference to the target frequency in comparison to the last
	 * added element.
	 */
	if (g_queue_get_length(tuning_elems) > 1) {
		tuning_elem = g_queue_peek_tail(tuning_elems);
		diff = fabs(tuning_elem->frequency - target_freq);

		while (g_queue_get_length(tuning_elems) > 1) {
			tuning_elem = g_queue_peek_head(tuning_elems);
			if (fabs(tuning_elem->frequency - target_freq) > diff)
				g_queue_pop_head(tuning_elems);
			else
				break;
		}

		/* Set final tuning values using the median value of the remaining
		 * range.
		 */
		tuning_elem = g_queue_peek_nth(
			tuning_elems, ceil(g_queue_get_length(tuning_elems)/2));
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(
			glb_widgets[dcxo_coarse_num].widget), tuning_elem->coarse);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(
			glb_widgets[dcxo_fine_num].widget), tuning_elem->fine);
	}

dcxo_cleanup:
	if (failure_msg) {
		GtkWidget *toplevel = gtk_widget_get_toplevel(fmcomms2_panel);
		if (!gtk_widget_is_toplevel(toplevel))
			toplevel = NULL;
		GtkWidget *dcxo_cal_dialog_done = gtk_message_dialog_new(
			GTK_WINDOW(toplevel),
			GTK_DIALOG_MODAL,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE,
			"%s", failure_msg);
		gtk_window_set_title(GTK_WINDOW(dcxo_cal_dialog_done), "DCXO calibration");
		if (gtk_dialog_run(GTK_DIALOG(dcxo_cal_dialog_done)))
			gtk_widget_destroy(dcxo_cal_dialog_done);
		ret = -1;
	}

	gtk_widget_hide(dcxo_cal_progressbar);

	/* reset calibration buttons */
	gtk_button_set_label(btn, "Calibrate DCXO");
	g_signal_handlers_block_by_func(btn, G_CALLBACK(dcxo_cal_clicked), NULL);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), FALSE);
	g_signal_handlers_unblock_by_func(btn, G_CALLBACK(dcxo_cal_clicked), NULL);

	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dcxo_cal_progressbar), 0);
	gtk_widget_set_sensitive(dcxo_cal_type, TRUE);
	gtk_widget_set_sensitive(glb_widgets[dcxo_coarse_num].widget, TRUE);
	gtk_widget_set_sensitive(glb_widgets[dcxo_fine_num].widget, TRUE);

	auto_calibrate = 1;

	if (tuning_elems)
		g_queue_free_full(tuning_elems, (GDestroyNotify)g_free);

	return ret;
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
	int profile;

	switch ((uintptr_t) data) {
		case 1: /* RX Store */
			iio_widget_save(&rx_widgets[rx_lo]);
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(rx_fastlock_profile));
			write_int(iio_device_find_channel(dev, "altvoltage0", true),
					rx_fastlock_store_name, profile);
			break;
		case 2: /* TX Store */
			iio_widget_save(&tx_widgets[tx_lo]);
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile));
			write_int(iio_device_find_channel(dev, "altvoltage1", true),
					tx_fastlock_store_name, profile);
			break;
		case 3: /* RX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(rx_fastlock_profile));
			write_int(iio_device_find_channel(dev, "altvoltage0", true),
					rx_fastlock_recall_name, profile);
			iio_widget_update(&rx_widgets[rx_lo]);
			break;
		case 4: /* TX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile));
			write_int(iio_device_find_channel(dev, "altvoltage1", true),
					tx_fastlock_recall_name, profile);
			iio_widget_update(&tx_widgets[tx_lo]);
			break;
	}
}

static void filter_fir_config_file_set_cb (GtkFileChooser *chooser, gpointer data)
{
	int ret;
	char *file_name = gtk_file_chooser_get_filename(chooser);

	ret = load_fir_filter(file_name, dev, NULL, fmcomms2_panel, chooser,
			fir_filter_en_tx, enable_fir_filter_rx,
			enable_fir_filter_rx_tx, disable_all_fir_filters,
			last_fir_filter);

	if (ret >= 0)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (enable_auto_filter), FALSE);
}

static int compare_gain(const char *a, const char *b) __attribute__((unused));
static int compare_gain(const char *a, const char *b)
{
	double val_a, val_b;
	sscanf(a, "%lf", &val_a);
	sscanf(b, "%lf", &val_b);

	if (val_a < val_b)
		return -1;
	else if(val_a > val_b)
		return 1;
	else
		return 0;
}

static void rx_phase_rotation_set(GtkSpinButton *spinbutton, gpointer user_data)
{
	uintptr_t offset = (uintptr_t) user_data;
	struct iio_channel *out0, *out1;
	gdouble val, phase;

	if (!cap)
		return;

	val = gtk_spin_button_get_value(spinbutton);

	phase = val * 2 * M_PI / 360.0;

	if (offset == 2) {
		out0 = iio_device_find_channel(cap, "voltage2", false);
		out1 = iio_device_find_channel(cap, "voltage3", false);
	} else {
		out0 = iio_device_find_channel(cap, "voltage0", false);
		out1 = iio_device_find_channel(cap, "voltage1", false);
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

static int handle_external_request (struct osc_plugin *plugin, const char *request)
{
	int ret = 0;

	if (!strcmp(request, "Reload Settings")) {
		reload_button_clicked(NULL, 0);
		ret = 1;
	}

	return ret;
}

static int fmcomms2_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	int ret = 0;

	if (MATCH_ATTRIB("load_fir_filter_file")) {
		if (value[0]) {
			load_fir_filter(value, dev, NULL, fmcomms2_panel,
					GTK_FILE_CHOOSER(filter_fir_config),
					fir_filter_en_tx, enable_fir_filter_rx,
					enable_fir_filter_rx_tx,
					disable_all_fir_filters,
					last_fir_filter);
		}
	} else if (MATCH_ATTRIB("dds_mode_tx1")) {
		dac_data_manager_set_dds_mode(dac_tx_manager,
				DDS_DEVICE, 1, atoi(value));
	} else if (MATCH_ATTRIB("dds_mode_tx2")) {
		dac_data_manager_set_dds_mode(dac_tx_manager,
				DDS_DEVICE, 2, atoi(value));
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
	} else if (MATCH_ATTRIB("up_down_converter")) {
		gtk_toggle_button_set_active(
				(GtkToggleButton *)up_down_converter, !!atoi(value));
	} else if (!strncmp(attrib, "tx_channel_", sizeof("tx_channel_") - 1)) {
		int tx = atoi(attrib + sizeof("tx_channel_") - 1);
		dac_data_manager_set_tx_channel_state(
				dac_tx_manager, tx, !!atoi(value));
	} else if (MATCH_ATTRIB("dac_buf_filename")) {
		dac_data_manager_set_buffer_chooser_filename(
				dac_tx_manager, value);
#ifndef _WIN32
	} else if (MATCH_ATTRIB("dcxo_calibrate")) {
		/* calibration button needs to be active for the function to run */
		g_signal_handlers_block_by_func(
			GTK_TOGGLE_BUTTON(dcxo_cal), G_CALLBACK(dcxo_cal_clicked), NULL);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dcxo_cal), TRUE);
		g_signal_handlers_unblock_by_func(
			GTK_TOGGLE_BUTTON(dcxo_cal), G_CALLBACK(dcxo_cal_clicked), NULL);
		ret = dcxo_cal_clicked(GTK_BUTTON(dcxo_cal), NULL);
		while (!auto_calibrate)
			gtk_main_iteration();
	} else if (MATCH_ATTRIB("dcxo_to_eeprom")) {
		ret = dcxo_cal_to_eeprom_clicked(NULL, NULL);
	} else if (MATCH_ATTRIB("xo_freq_to_eeprom")) {
		ret = xo_freq_to_eeprom();
#endif /* _WIN32 */
	} else if (MATCH_ATTRIB("SYNC_RELOAD")) {
		if (can_update_widgets)
			reload_button_clicked(NULL, NULL);
	} else {
		return -EINVAL;
	}

	return ret;
}

static int fmcomms2_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			fmcomms2_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	struct iio_channel *ch;
	char *value;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fmcomms2_driver_attribs); i++) {
		char *value = read_token_from_ini(ini_fn, THIS_DRIVER,
				fmcomms2_driver_attribs[i]);
		if (value) {
			fmcomms2_handle_driver(NULL,
					fmcomms2_driver_attribs[i], value);
			free(value);
		}
	}

	/* The gain_control_mode iio attribute should be set prior to setting
	 * hardwaregain iio attribute. This is neccessary due to the fact that
	 * some control modes change the hardwaregain automatically. */
	ch = iio_device_find_channel(dev, "voltage0", false);
	value = read_token_from_ini(ini_fn, THIS_DRIVER,
				PHY_DEVICE".in_voltage0_gain_control_mode");
	if (ch && value) {
		iio_channel_attr_write(ch, "gain_control_mode", value);
		free(value);
	}

	ch = iio_device_find_channel(dev, "voltage1", false);
	value = read_token_from_ini(ini_fn, THIS_DRIVER,
				PHY_DEVICE".in_voltage1_gain_control_mode");
	if (ch && value) {
		iio_channel_attr_write(ch, "gain_control_mode", value);
		free(value);
	}

	update_from_ini(ini_fn, THIS_DRIVER, dev, fmcomms2_sr_attribs,
			ARRAY_SIZE(fmcomms2_sr_attribs));
	if (dds)
		update_from_ini(ini_fn, THIS_DRIVER, dds, fmcomms2_sr_attribs,
				ARRAY_SIZE(fmcomms2_sr_attribs));
	if (udc_rx)
		update_from_ini(ini_fn, THIS_DRIVER, udc_rx, fmcomms2_sr_attribs,
				ARRAY_SIZE(fmcomms2_sr_attribs));
	if (udc_tx)
		update_from_ini(ini_fn, THIS_DRIVER, udc_tx, fmcomms2_sr_attribs,
				ARRAY_SIZE(fmcomms2_sr_attribs));

	if (can_update_widgets)
		reload_button_clicked(NULL, NULL);
}

static GtkWidget * fmcomms2_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *dds_container;
	struct iio_channel *ch0, *ch1;

	can_update_widgets = false;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	dev = iio_context_find_device(ctx, PHY_DEVICE);
	dds = iio_context_find_device(ctx, DDS_DEVICE);
	cap = iio_context_find_device(ctx, CAP_DEVICE);
	udc_rx = iio_context_find_device(ctx, UDC_RX_DEVICE);
	udc_tx = iio_context_find_device(ctx, UDC_TX_DEVICE);
	has_udc_driver = (udc_rx && udc_tx);

	ch0 = iio_device_find_channel(dev, "voltage0", false);
	ch1 = iio_device_find_channel(dev, "voltage1", false);

	dac_tx_manager = dac_data_manager_new(dds, NULL, ctx);

	const char *env_freq_span = getenv("OSC_UPDN_FREQ_SPAN");
	const char *env_freq_mix_sign = getenv("OSC_UPDN_FREQ_MIX_SIGN");

	if(!env_freq_span) {
		updn_freq_span = 2;
	} else {
		errno = 0;
		updn_freq_span = g_strtod(env_freq_span, NULL);
		if (errno)
			updn_freq_span = 2;
	}

	if(!env_freq_mix_sign) {
		updn_freq_mix_sign = 1;
	} else {
		if (!strncmp(env_freq_mix_sign, "-", 1))
			updn_freq_mix_sign = -1;
		else
			updn_freq_mix_sign = 1;
	}

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (osc_load_glade_file(builder, "fmcomms2") < 0)
		return NULL;

	is_2rx_2tx = ch1 && iio_channel_find_attr(ch1, "hardwaregain");

	fmcomms2_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms2_panel"));

	/* Hide DCXO calibration support if the scpi plugin isn't loaded. */
	if (!scpi_connect_functions())
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "dcxo_cal_grid")));

#ifndef _WIN32
	/* Disable EEPROM functionality if not running locally. */
	if (strcmp(iio_context_get_name(ctx), "local") != 0) {
		gtk_widget_set_sensitive(GTK_WIDGET(
			gtk_builder_get_object(builder, "dcxo_cal_to_eeprom")), FALSE);
		gtk_widget_set_sensitive(GTK_WIDGET(
			gtk_builder_get_object(builder, "dcxo_cal_from_eeprom")), FALSE);
	}

	/* Disable saving to EEPROM if not running as root. */
	if (getuid() != 0) {
		gtk_widget_set_sensitive(GTK_WIDGET(
			gtk_builder_get_object(builder, "dcxo_cal_to_eeprom")), FALSE);
	}
#endif

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
	up_down_converter = GTK_WIDGET(gtk_builder_get_object(builder, "checkbox_up_down_converter"));
	dcxo_cal_progressbar = GTK_WIDGET(gtk_builder_get_object(builder, "dcxo_cal_progressbar"));
	dcxo_cal_type = GTK_WIDGET(gtk_builder_get_object(builder, "dcxo_cal_type"));
	dcxo_cal = GTK_WIDGET(gtk_builder_get_object(builder, "dcxo_cal"));
	enable_auto_filter = GTK_WIDGET(gtk_builder_get_object(builder, "enable_auto_filter"));
	dcxo_cal_tab = GTK_WIDGET(gtk_builder_get_object(builder, "dcxo_tab"));

	section_toggle[SECTION_GLOBAL] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "global_settings_toggle"));
	section_setting[SECTION_GLOBAL] = GTK_WIDGET(gtk_builder_get_object(builder, "global_settings"));
	section_toggle[SECTION_TX] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "tx_toggle"));
	section_setting[SECTION_TX] = GTK_WIDGET(gtk_builder_get_object(builder, "tx_settings"));
	section_toggle[SECTION_RX] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "rx_toggle"));
	section_setting[SECTION_RX] = GTK_WIDGET(gtk_builder_get_object(builder, "rx_settings"));
	section_toggle[SECTION_FPGA] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "fpga_toggle"));
	section_setting[SECTION_FPGA] = GTK_WIDGET(gtk_builder_get_object(builder, "fpga_settings"));

	/* Receive Chain */

	rf_port_select_rx = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_rx"));
	rx_gain_control_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx1"));
	rx_gain_control_rx2 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx2"));
	rx_gain_control_modes_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx1"));
	rx_gain_control_modes_rx2 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx2"));
	rx1_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx1"));
	rx2_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx2"));
	rx_fastlock_profile = GTK_WIDGET(gtk_builder_get_object(builder, "rx_fastlock_profile"));
	fpga_rx_frequency_available = GTK_WIDGET(gtk_builder_get_object(builder, "fpga_rx_frequency_available"));
	sampling_freq_rx_decim = GTK_WIDGET(gtk_builder_get_object(builder, "sampling_freq_rx_decim"));

	/* Transmit Chain */

	rf_port_select_tx = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_tx"));
	tx_fastlock_profile = GTK_WIDGET(gtk_builder_get_object(builder, "tx_fastlock_profile"));
	tx1_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_tx1"));
	tx2_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_tx2"));
	fpga_tx_frequency_available = GTK_WIDGET(gtk_builder_get_object(builder, "fpga_tx_frequency_available"));
	sampling_freq_tx_inter = GTK_WIDGET(gtk_builder_get_object(builder, "sampling_freq_tx_inter"));

	dds_container = GTK_WIDGET(gtk_builder_get_object(builder, "dds_transmit_block"));

	if (dac_tx_manager)
		gtk_container_add(GTK_CONTAINER(dds_container),
			dac_data_manager_get_gui_container(dac_tx_manager));
	gtk_widget_show_all(dds_container);

	rx_phase_rotation[0] = GTK_WIDGET(gtk_builder_get_object(builder, "rx1_phase_rotation"));
	rx_phase_rotation[1] = GTK_WIDGET(gtk_builder_get_object(builder, "rx2_phase_rotation"));

	gtk_combo_box_set_active(GTK_COMBO_BOX(ensm_mode_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(trx_rate_governor_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes_rx1), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes_rx2), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rf_port_select_rx), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rf_port_select_tx), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_fastlock_profile), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(tx_fastlock_profile), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(dcxo_cal_type), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(fpga_rx_frequency_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(fpga_tx_frequency_available), 0);

	/* Bind the IIO device files to the GUI widgets */

	glb_widgets = widgets;

	/* Global settings */
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev, NULL, "ensm_mode", "ensm_mode_available",
		ensm_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev, NULL, "calib_mode", "calib_mode_available",
		calib_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev, NULL, "trx_rate_governor", "trx_rate_governor_available",
		trx_rate_governor_available, NULL);

	dcxo_coarse_num = num_glb;
	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		dev, NULL, "dcxo_tune_coarse", builder, "dcxo_coarse_tune",
		0);
	dcxo_fine_num = num_glb;
	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		dev, NULL, "dcxo_tune_fine", builder, "dcxo_fine_tune",
		0);

	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		dev, NULL, "xo_correction", builder, "xo_correction",
		0);

	rx_widgets = &glb_widgets[num_glb];

	/* Receive Chain */

	iio_combo_box_init(&rx_widgets[num_rx++],
		dev, ch0, "gain_control_mode",
		"gain_control_mode_available",
		rx_gain_control_modes_rx1, NULL);

	iio_combo_box_init(&rx_widgets[num_rx++],
		dev, ch0, "rf_port_select",
		"rf_port_select_available",
		rf_port_select_rx, NULL);

	if (is_2rx_2tx)
		iio_combo_box_init(&rx_widgets[num_rx++],
			dev, ch1, "gain_control_mode",
			"gain_control_mode_available",
			rx_gain_control_modes_rx2, NULL);
	rx1_gain = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "hardwaregain", builder,
		"hardware_gain_rx1", NULL);

	if (is_2rx_2tx) {
		rx2_gain = num_rx;
		iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			dev, ch1, "hardwaregain", builder,
			"hardware_gain_rx2", NULL);
	}
	rx_sample_freq = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "sampling_frequency", builder,
		"sampling_freq_rx", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "rf_bandwidth", builder, "rf_bandwidth_rx",
		&mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);
	rx_lo = num_rx;

	ch1 = iio_device_find_channel(dev, "altvoltage0", true);
	if (iio_channel_find_attr(ch1, "frequency"))
		freq_name = "frequency";
	else
		freq_name = "RX_LO_frequency";
	iio_spin_button_s64_init_from_builder(&rx_widgets[num_rx++],
		dev, ch1, freq_name, builder,
		"rx_lo_freq", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch1, "external", builder,
		"rx_lo_external", 0);

	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "quadrature_tracking_en", builder,
		"quad", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "rf_dc_offset_tracking_en", builder,
		"rfdc", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "bb_dc_offset_tracking_en", builder,
		"bbdc", 0);

	ch0 = iio_device_find_channel(dev, "altvoltage0", true);

	if (iio_channel_find_attr(ch0, "fastlock_store"))
		rx_fastlock_store_name = "fastlock_store";
	else
		rx_fastlock_store_name = "RX_LO_fastlock_store";
	if (iio_channel_find_attr(ch0, "fastlock_recall"))
		rx_fastlock_recall_name = "fastlock_recall";
	else
		rx_fastlock_recall_name = "RX_LO_fastlock_recall";

	/* Transmit Chain */

	tx_widgets = &rx_widgets[num_rx];

	ch0 = iio_device_find_channel(dev, "voltage0", true);
	if (is_2rx_2tx)
		ch1 = iio_device_find_channel(dev, "voltage1", true);

	tx_rssi_available = ch0 && iio_channel_find_attr(ch0, "rssi");
	if (is_2rx_2tx)
		tx_rssi_available = tx_rssi_available &&
				(ch1 && iio_channel_find_attr(ch1, "rssi"));

	iio_combo_box_init(&tx_widgets[num_tx++],
		dev, ch0, "rf_port_select",
		"rf_port_select_available",
		rf_port_select_tx, NULL);

	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
		dev, ch0, "hardwaregain", builder,
		"hardware_gain_tx1", &inv_scale);

	if (is_2rx_2tx)
		iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, ch1, "hardwaregain", builder,
			"hardware_gain_tx2", &inv_scale);
	tx_sample_freq = num_tx;
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		dev, ch0, "sampling_frequency", builder,
		"sampling_freq_tx", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		dev, ch0, "rf_bandwidth", builder,
		"rf_bandwidth_tx", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	tx_lo = num_tx;
	ch1 = iio_device_find_channel(dev, "altvoltage1", true);

	if (iio_channel_find_attr(ch1, "frequency"))
		freq_name = "frequency";
	else
		freq_name = "TX_LO_frequency";
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
		dev, ch1, freq_name, builder, "tx_lo_freq", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		dev, ch1, "external", builder,
		"tx_lo_external", 0);

	/* FPGA widgets */
	fpga_widgets = &tx_widgets[num_tx];

	ch0 = iio_device_find_channel(dds, "voltage0", true);
	if (iio_channel_find_attr(ch0, "sampling_frequency_available")) {
		iio_combo_box_init(&fpga_widgets[num_fpga++],
				dds, ch0, "sampling_frequency",
			"sampling_frequency_available",
			fpga_tx_frequency_available, NULL);

		g_signal_connect_after(sampling_freq_tx_inter, "value-changed",
				       G_CALLBACK(int_dec_spin_update_cb), ch0);


	} else {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder,
				"transmit_frame_dma_buf")));

		gtk_widget_hide(sampling_freq_tx_inter);
	}

	ch0 = iio_device_find_channel(cap, "voltage0", false);
	if (iio_channel_find_attr(ch0, "sampling_frequency_available")) {
		iio_combo_box_init(&fpga_widgets[num_fpga++],
				cap, ch0, "sampling_frequency",
			"sampling_frequency_available",
			fpga_rx_frequency_available, NULL);

		g_signal_connect_after(sampling_freq_rx_decim, "value-changed",
				       G_CALLBACK(int_dec_spin_update_cb), ch0);

	} else {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder,
				"receive_frame_dma_buf")));
		gtk_widget_hide(sampling_freq_rx_decim);

	}

	ch1 = iio_device_find_channel(dev, "altvoltage1", true);

	/* Widgets bindings */
	g_builder_bind_property(builder, "rssi_tx1", "visible",
		"label_rssi_tx1", "sensitive", G_BINDING_DEFAULT);
	g_builder_bind_property(builder, "rssi_tx2", "visible",
		"label_rssi_tx2", "sensitive", G_BINDING_DEFAULT);
	g_builder_bind_property(builder, "rx_lo_external", "active",
		"rx_fastlock_profile", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "rx_lo_external", "active",
		"rx_fastlock_label", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "rx_lo_external", "active",
		"rx_fastlock_actions", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "tx_lo_external", "active",
		"tx_fastlock_profile", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "tx_lo_external", "active",
		"tx_fastlock_label", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "tx_lo_external", "active",
		"tx_fastlock_actions", "visible", G_BINDING_INVERT_BOOLEAN);

	if (ini_fn)
		load_profile(NULL, ini_fn);

	/* Update all widgets with current values */
	printf("Updating widgets...\n");
	update_widgets();
	sample_frequency_changed_cb(NULL);
	printf("Updating FIR filter...\n");
	filter_fir_update();
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(disable_all_fir_filters), true);
	glb_settings_update_labels();
	rssi_update_labels();
	dac_data_manager_freq_widgets_range_update(dac_tx_manager,
		get_gui_tx_sampling_freq() / 2.0);
	dac_data_manager_update_iio_widgets(dac_tx_manager);

	/* Connect signals */

	if (iio_channel_find_attr(ch1, "fastlock_store"))
		tx_fastlock_store_name = "fastlock_store";
	else
		tx_fastlock_store_name = "TX_LO_fastlock_store";
	if (iio_channel_find_attr(ch1, "fastlock_recall"))
		tx_fastlock_recall_name = "fastlock_recall";
	else
		tx_fastlock_recall_name = "TX_LO_fastlock_recall";


	g_builder_connect_signal(builder, "rx1_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)0);

	g_builder_connect_signal(builder, "rx2_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)2);

	g_builder_connect_signal(builder, "fmcomms2_settings_reload", "clicked",
		G_CALLBACK(reload_button_clicked), NULL);

	g_builder_connect_signal(builder, "filter_fir_config", "file-set",
		G_CALLBACK(filter_fir_config_file_set_cb), NULL);

	g_builder_connect_signal(builder, "dcxo_cal", "clicked",
		G_CALLBACK(dcxo_cal_clicked), NULL);
#ifndef _WIN32
	g_builder_connect_signal(builder, "dcxo_cal_to_eeprom", "clicked",
		G_CALLBACK(dcxo_cal_to_eeprom_clicked), NULL);
	g_builder_connect_signal(builder, "dcxo_cal_from_eeprom", "clicked",
		G_CALLBACK(dcxo_cal_from_eeprom_clicked), NULL);
#endif

	g_builder_connect_signal(builder, "rx_fastlock_store", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 1);
	g_builder_connect_signal(builder, "tx_fastlock_store", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 2);
	g_builder_connect_signal(builder, "rx_fastlock_recall", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 3);
	g_builder_connect_signal(builder, "tx_fastlock_recall", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 4);

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

	g_signal_connect_after(rx_gain_control_modes_rx1, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);
	g_signal_connect_after(rx_gain_control_modes_rx2, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);

	g_signal_connect_after(fpga_rx_frequency_available, "changed",
			       G_CALLBACK(int_dec_update_cb), sampling_freq_rx_decim);

	g_signal_connect_after(fpga_tx_frequency_available, "changed",
			       G_CALLBACK(int_dec_update_cb), sampling_freq_tx_inter);


	if (tx_rssi_available)
		g_signal_connect(rf_port_select_rx, "changed",
			G_CALLBACK(rf_port_select_rx_changed_cb), NULL);

	g_signal_connect_after(enable_fir_filter_rx, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);
	g_signal_connect_after(fir_filter_en_tx, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);
	g_signal_connect_after(enable_fir_filter_rx_tx, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);
	g_signal_connect_after(disable_all_fir_filters, "toggled",
		G_CALLBACK(filter_fir_enable), NULL);

	g_signal_connect(up_down_converter, "toggled",
		G_CALLBACK(up_down_converter_toggled_cb), NULL);

	make_widget_update_signal_based(glb_widgets, num_glb);
	make_widget_update_signal_based(rx_widgets, num_rx);
	make_widget_update_signal_based(tx_widgets, num_tx);
	make_widget_update_signal_based(fpga_widgets, num_fpga);

	iio_spin_button_set_on_complete_function(&rx_widgets[rx_sample_freq],
		tx_sample_frequency_changed_cb, (void *) FALSE);
	iio_spin_button_set_on_complete_function(&tx_widgets[tx_sample_freq],
		tx_sample_frequency_changed_cb, (void *) TRUE);
	iio_spin_button_set_on_complete_function(&rx_widgets[rx_lo],
		sample_frequency_changed_cb, NULL);
	iio_spin_button_set_on_complete_function(&tx_widgets[tx_lo],
		sample_frequency_changed_cb, NULL);

	/* Things are saved in tx_sample_frequency_changed_cb() */
	iio_spin_button_skip_save_on_complete(&rx_widgets[rx_sample_freq], TRUE);
	iio_spin_button_skip_save_on_complete(&tx_widgets[tx_sample_freq], TRUE);

	add_ch_setup_check_fct("cf-ad9361-lpc", channel_combination_check);

	struct iio_device *adc_dev;
	struct extra_dev_info *adc_info;

	adc_dev = iio_context_find_device(get_context_from_osc(), CAP_DEVICE);
	if (adc_dev) {
		adc_info = iio_device_get_data(adc_dev);
		if (adc_info) /* TO DO: use osc preferences instead */
			adc_info->plugin_fft_corr = 20 * log10(1/sqrt(HANNING_ENBW));
	}

	block_diagram_init(builder, 2, "AD9361.svg", "AD_FMCOMM2S2_RevC.jpg");

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(filter_fir_config), OSC_FILTER_FILE_PATH);
	dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager, OSC_WAVEFORM_FILE_PATH);

	if (!is_2rx_2tx) {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_rx2")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_fpga_rx2")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "table_hw_gain_tx2")));
	}
	if (!tx_rssi_available) {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "rssi_tx1")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "rssi_tx2")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "label_rssi_tx1")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "label_rssi_tx2")));
	}
	gtk_widget_set_visible(up_down_converter, has_udc_driver);

	if (!dac_tx_manager)
		gtk_widget_hide(gtk_widget_get_parent(section_setting[SECTION_FPGA]));

	g_timeout_add(1000, (GSourceFunc) update_display, ctx);
	can_update_widgets = true;

	return fmcomms2_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void fmcomms2_get_preferred_size(const struct osc_plugin *plugin, int *width, int *height)
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
			"tx_channel_0 = %i\n"
			"tx_channel_1 = %i\n"
			"tx_channel_2 = %i\n"
			"tx_channel_3 = %i\n"
			"dac_buf_filename = %s\n"
			"up_down_converter = %i\n"
			"global_settings_show = %i\n"
			"tx_show = %i\n"
			"rx_show = %i\n"
			"fpga_show = %i\n",
			last_fir_filter,
			dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE, 1),
			dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE, 2),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 0),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 1),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 2),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 3),
			dac_data_manager_get_buffer_chooser_filename(dac_tx_manager),
			!!gtk_toggle_button_get_active((GtkToggleButton *)up_down_converter),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_GLOBAL]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_TX]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_RX]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_FPGA]));
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		save_to_ini(f, THIS_DRIVER, dev, fmcomms2_sr_attribs,
				ARRAY_SIZE(fmcomms2_sr_attribs));
		if (dds)
			save_to_ini(f, NULL, dds, fmcomms2_sr_attribs,
					ARRAY_SIZE(fmcomms2_sr_attribs));
		if (udc_rx)
			save_to_ini(f, NULL, udc_rx, fmcomms2_sr_attribs,
					ARRAY_SIZE(fmcomms2_sr_attribs));
		if (udc_tx)
			save_to_ini(f, NULL, udc_tx, fmcomms2_sr_attribs,
					ARRAY_SIZE(fmcomms2_sr_attribs));
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

static bool fmcomms2_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	if (!iio_context_find_device(osc_ctx, PHY_DEVICE))
		return false;

	/* Check if FMComms5 is used */
	return !iio_context_find_device(osc_ctx, "ad9361-phy-B");
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = fmcomms2_identify,
	.init = fmcomms2_init,
	.handle_item = fmcomms2_handle,
	.handle_external_request = handle_external_request,
	.update_active_page = update_active_page,
	.get_preferred_size = fmcomms2_get_preferred_size,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
