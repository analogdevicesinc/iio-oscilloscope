/**
 * Copyright (C) 2016 Analog Devices, Inc.
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
#include "../libini2.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "../fru.h"
#include "../iio_utils.h"
#include "block_diagram.h"
#include "dac_data_manager.h"
//#include "fir_filter.h"
//#include "scpi.h"

#define THIS_DRIVER "AD9371"
#define PHY_DEVICE "ad9371-phy"
#define DDS_DEVICE "axi-ad9371-tx-hpc"
#define CAP_DEVICE "axi-ad9371-rx-hpc"
#define CAP_DEVICE_2 "axi-ad9371-rx-obs-hpc"
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
static bool has_dpd;

static const gdouble mhz_scale = 1000000.0;
static const gdouble inv_scale = -1.0;
static const gdouble scale100 = 100.0;

static const char *freq_name;

static struct iio_widget widgets[200];
static struct iio_widget *glb_widgets, *tx_widgets, *rx_widgets, *obsrx_widgets, *fpga_widgets;
static unsigned int rx1_gain, rx2_gain, obs_gain, tx1_clgc_desired_gain, tx2_clgc_desired_gain;
static unsigned int num_glb, num_tx, num_rx, num_obsrx, num_fpga;
static unsigned int rx_lo, tx_lo, sn_lo;
static unsigned int rx_sample_freq, tx_sample_freq;
static unsigned int tx1_dpd, tx2_dpd, tx1_clgc, tx2_clgc, tx1_vswr, tx2_vswr;;
static double updn_freq_span;
static double updn_freq_mix_sign;
static char last_profile[PATH_MAX];

static struct iio_context *ctx;
static struct iio_device *dev, *dds, *cap, *udc_rx, *udc_tx, *cap_obs;

enum {
	SECTION_GLOBAL,
	SECTION_TX,
	SECTION_RX,
	SECTION_OBS,
	SECTION_FPGA,
	SECTION_NUM,
};

static GtkToggleToolButton *section_toggle[SECTION_NUM];
static GtkWidget *section_setting[SECTION_NUM];

/* Widgets for Global Settings */
static GtkWidget *ensm_mode;
static GtkWidget *ensm_mode_available;

static GtkWidget *profile_config;
static GtkWidget *up_down_converter;

/* Widgets for Receive Settings */
static GtkWidget *rx_gain_control_rx1;
static GtkWidget *rx_gain_control_modes_rx1;
static GtkWidget *rx_gain_control_rx2;
//static GtkWidget *rx_gain_control_modes_rx2;
static GtkWidget *rx1_rssi;
static GtkWidget *rx2_rssi;
static GtkWidget *label_rf_bandwidth_rx;
static GtkWidget *label_sampling_freq_rx;

/* Widgets for interpolation/decimation */
static GtkWidget *fpga_tx_frequency_available;
static GtkWidget *fpga_rx_frequency_available;

/* Widgets for Observation Receive Settings */
static GtkWidget *obs_gain_control;
static GtkWidget *obs_gain_control_modes;
static GtkWidget *obs_port_select;
static GtkWidget *obs_rssi;
static GtkWidget *label_rf_bandwidth_obs;
static GtkWidget *label_sampling_freq_obs;

static GtkWidget *label_rf_bandwidth_tx;
static GtkWidget *label_sampling_freq_tx;

static GtkWidget *rx_phase_rotation[2];

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *ad9371_panel;
static gboolean plugin_detached;

static GtkWidget *tx1_dpd_track_count;
static GtkWidget *tx1_dpd_model_error;
static GtkWidget *tx1_dpd_external_path_delay;
static GtkWidget *tx1_dpd_status;

static GtkWidget *tx2_dpd_track_count;
static GtkWidget *tx2_dpd_model_error;
static GtkWidget *tx2_dpd_external_path_delay;
static GtkWidget *tx2_dpd_status;

static GtkWidget *tx1_clgc_track_count;
static GtkWidget *tx1_clgc_status;
static GtkWidget *tx1_clgc_current_gain;
static GtkWidget *tx1_clgc_orx_gain;
static GtkWidget *tx1_clgc_tx_gain;
static GtkWidget *tx1_clgc_tx_rms;

static GtkWidget *tx2_clgc_track_count;
static GtkWidget *tx2_clgc_status;
static GtkWidget *tx2_clgc_current_gain;
static GtkWidget *tx2_clgc_orx_gain;
static GtkWidget *tx2_clgc_tx_gain;
static GtkWidget *tx2_clgc_tx_rms;

static GtkWidget *tx1_vswr_track_count;
static GtkWidget *tx1_vswr_status;
static GtkWidget *tx1_vswr_forward_gain;
static GtkWidget *tx1_vswr_forward_gain_imag;
static GtkWidget *tx1_vswr_forward_gain_real;
static GtkWidget *tx1_vswr_forward_orx;
static GtkWidget *tx1_vswr_forward_tx;
static GtkWidget *tx1_vswr_reflected_gain;
static GtkWidget *tx1_vswr_reflected_gain_imag;
static GtkWidget *tx1_vswr_reflected_gain_real;
static GtkWidget *tx1_vswr_reflected_orx;
static GtkWidget *tx1_vswr_reflected_tx;

static GtkWidget *tx2_vswr_track_count;
static GtkWidget *tx2_vswr_status;
static GtkWidget *tx2_vswr_forward_gain;
static GtkWidget *tx2_vswr_forward_gain_imag;
static GtkWidget *tx2_vswr_forward_gain_real;
static GtkWidget *tx2_vswr_forward_orx;
static GtkWidget *tx2_vswr_forward_tx;
static GtkWidget *tx2_vswr_reflected_gain;
static GtkWidget *tx2_vswr_reflected_gain_imag;
static GtkWidget *tx2_vswr_reflected_gain_real;
static GtkWidget *tx2_vswr_reflected_orx;
static GtkWidget *tx2_vswr_reflected_tx;


const char *clgc_status_strings[] = {
	"No Error",
	"Error: TX is disabled",
	"Error: ORx is disabled",
	"Error: Loopback switch is closed",
	"Error: Data measurement aborted during capture",
	"Error: No initial calibration was done",
	"Error: Path delay not setup",
	"Error: No apply control is possible",
	"Error: ontrol value is out of range",
	"Error: CLGC feature is disabled",
	"Error: TX attenuation is capped",
	"Error: Gain measurement",
	"Error: No GPIO configured in single ORx configuration",
	"Error: Tx is not observable with any of the ORx Channels",
};

const char *dpd_status_strings[] = {
	"No Error",
	"Error: ORx disabled",
	"Error: Tx disabled",
	"Error: DPD initialization not run",
	"Error: Path delay not setup",
	"Error: ORx signal too low",
	"Error: ORx signal saturated",
	"Error: Tx signal too low",
	"Error: Tx signal saturated",
	"Error: Model error high",
	"Error: AM AM outliers",
	"Error: Invalid Tx profile",
	"Error: ORx QEC Disabled",
};

const char *vswr_status_strings[] = {
	"No Error",
	"Error: TX disabled",
	"Error: ORx disabled",
	"Error: Loopback switch is closed",
	"Error: No initial calibration was done",
	"Error: Path delay not setup",
	"Error: Data capture aborted",
	"Error: VSWR is disabled",
	"Error: Entering Cal",
	"Error: No GPIO configured in single ORx configuration",
	"Error: Tx is not observable with any of the ORx Channels",
};

static const char *ad9371_sr_attribs[] = {
	PHY_DEVICE".ensm_mode",
	PHY_DEVICE".calibrate_dpd_en",
	PHY_DEVICE".calibrate_clgc_en",
	PHY_DEVICE".calibrate_rx_qec_en",
	PHY_DEVICE".calibrate_tx_qec_en",
	PHY_DEVICE".calibrate_tx_lol_en",
	PHY_DEVICE".calibrate_tx_lol_ext_en",
	PHY_DEVICE".calibrate_vswr_en",
	PHY_DEVICE".in_voltage0_gain_control_mode",
	PHY_DEVICE".in_voltage0_hardwaregain",
	PHY_DEVICE".in_voltage0_quadrature_tracking_en",
	PHY_DEVICE".in_voltage0_temp_comp_gain",
	PHY_DEVICE".in_voltage1_gain_control_mode",
	PHY_DEVICE".in_voltage1_hardwaregain",
	PHY_DEVICE".in_voltage1_temp_comp_gain",
	PHY_DEVICE".in_voltage1_quadrature_tracking_en",
	PHY_DEVICE".in_voltage2_hardwaregain",
	PHY_DEVICE".in_voltage2_rf_port_select",
	PHY_DEVICE".in_voltage2_temp_comp_gain",
	PHY_DEVICE".in_voltage_rf_port_select_available",
	PHY_DEVICE".out_altvoltage0_RX_LO_frequency",
	PHY_DEVICE".out_altvoltage1_TX_LO_frequency",
	PHY_DEVICE".out_altvoltage2_RX_SN_LO_frequency",
	PHY_DEVICE".out_voltage0_hardwaregain",
	PHY_DEVICE".out_voltage0_lo_leakage_tracking_en",
	PHY_DEVICE".out_voltage0_quadrature_tracking_en",
	PHY_DEVICE".out_voltage1_hardwaregain",
	PHY_DEVICE".out_voltage1_lo_leakage_tracking_en",
	PHY_DEVICE".out_voltage1_quadrature_tracking_en",

	DDS_DEVICE".out_altvoltage0_TX1_I_F1_frequency",
	DDS_DEVICE".out_altvoltage0_TX1_I_F1_phase",
	DDS_DEVICE".out_altvoltage0_TX1_I_F1_raw",
	DDS_DEVICE".out_altvoltage0_TX1_I_F1_scale",
	DDS_DEVICE".out_altvoltage1_TX1_I_F2_frequency",
	DDS_DEVICE".out_altvoltage1_TX1_I_F2_phase",
	DDS_DEVICE".out_altvoltage1_TX1_I_F2_raw",
	DDS_DEVICE".out_altvoltage1_TX1_I_F2_scale",
	DDS_DEVICE".out_altvoltage2_TX1_Q_F1_frequency",
	DDS_DEVICE".out_altvoltage2_TX1_Q_F1_phase",
	DDS_DEVICE".out_altvoltage2_TX1_Q_F1_raw",
	DDS_DEVICE".out_altvoltage2_TX1_Q_F1_scale",
	DDS_DEVICE".out_altvoltage3_TX1_Q_F2_frequency",
	DDS_DEVICE".out_altvoltage3_TX1_Q_F2_phase",
	DDS_DEVICE".out_altvoltage3_TX1_Q_F2_raw",
	DDS_DEVICE".out_altvoltage3_TX1_Q_F2_scale",
	DDS_DEVICE".out_altvoltage4_TX2_I_F1_frequency",
	DDS_DEVICE".out_altvoltage4_TX2_I_F1_phase",
	DDS_DEVICE".out_altvoltage4_TX2_I_F1_raw",
	DDS_DEVICE".out_altvoltage4_TX2_I_F1_scale",
	DDS_DEVICE".out_altvoltage5_TX2_I_F2_frequency",
	DDS_DEVICE".out_altvoltage5_TX2_I_F2_phase",
	DDS_DEVICE".out_altvoltage5_TX2_I_F2_raw",
	DDS_DEVICE".out_altvoltage5_TX2_I_F2_scale",
	DDS_DEVICE".out_altvoltage6_TX2_Q_F1_frequency",
	DDS_DEVICE".out_altvoltage6_TX2_Q_F1_phase",
	DDS_DEVICE".out_altvoltage6_TX2_Q_F1_raw",
	DDS_DEVICE".out_altvoltage6_TX2_Q_F1_scale",
	DDS_DEVICE".out_altvoltage7_TX2_Q_F2_frequency",
	DDS_DEVICE".out_altvoltage7_TX2_Q_F2_phase",
	DDS_DEVICE".out_altvoltage7_TX2_Q_F2_raw",
	DDS_DEVICE".out_altvoltage7_TX2_Q_F2_scale",
};

static const char * ad9371_driver_attribs[] = {
	"load_myk_profile_file",
	"dds_mode_tx1",
	"dds_mode_tx2",
	"global_settings_show",
	"tx_show",
	"rx_show",
	"fpga_show",
	"tx_channel_0",
	"tx_channel_1",
	"tx_channel_2",
	"tx_channel_3",
	"dac_buf_filename",
};

static void profile_update(void);

static void fixup_label(GtkLabel *label, const char *search, const char *replace)
{
	const char *text = gtk_label_get_label(label);
	char *line, *new;

	if (text == NULL)
		return;

	line = g_strdup(text);

	new = g_strstr_len(line, strlen(line), search);

	new[0] = replace[0];

	gtk_label_set_markup(label, line);
	g_free(line);
}

static void update_lable_from(GtkWidget *label, const char *channel,
			      const char *attribute, bool output, const char *unit, int scale)
{
	char buf[80];
	long long val = 0;

	int ret = iio_channel_attr_read_longlong(
			iio_device_find_channel(dev, channel, output),
			attribute, &val);

	if (scale == 1)
		snprintf(buf, sizeof(buf), "%lld %s", val, unit);
	else if (scale > 0 && scale <= 10)
		snprintf(buf, sizeof(buf), "%.1f %s", (float)val / scale, unit);
	else if (scale > 10)
		snprintf(buf, sizeof(buf), "%.2f %s", (float)val / scale, unit);
	else if (scale > 100)
		snprintf(buf, sizeof(buf), "%.3f %s", (float)val / scale, unit);

	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(label), buf);
	else
		gtk_label_set_text(GTK_LABEL(label), "<error>");

}

static void update_lable_from_prms(GtkWidget *label, const char *channel,
			      const char *attribute, bool output, const char *unit, int scale)
{
	char buf[80];
	long long val = 0;

	int ret = iio_channel_attr_read_longlong(
		iio_device_find_channel(dev, channel, output),
						 attribute, &val);

	snprintf(buf, sizeof(buf), "%.2f %s", (float)val / scale + 21, unit);

	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(label), buf);
	else
		gtk_label_set_text(GTK_LABEL(label), "<error>");

}

static void update_dpd_status_from(GtkWidget *label, const char *channel,
			      const char *attribute)
{
	long long val = 0;

	int ret = iio_channel_attr_read_longlong(
		iio_device_find_channel(dev, channel, true),
						 attribute, &val);

	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(label), dpd_status_strings[val]);
	else
		gtk_label_set_text(GTK_LABEL(label), "<error>");

}

static void update_vswr_status_from(GtkWidget *label, const char *channel,
				   const char *attribute)
{
	long long val = 0;

	int ret = iio_channel_attr_read_longlong(
		iio_device_find_channel(dev, channel, true),
						 attribute, &val);

	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(label), vswr_status_strings[val]);
	else
		gtk_label_set_text(GTK_LABEL(label), "<error>");

}

static void update_clgc_status_from(GtkWidget *label, const char *channel,
				    const char *attribute)
{
	long long val = 0;

	int ret = iio_channel_attr_read_longlong(
		iio_device_find_channel(dev, channel, true),
						 attribute, &val);

	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(label), clgc_status_strings[val]);
	else
		gtk_label_set_text(GTK_LABEL(label), "<error>");

}

static void profile_update_labels(void)
{
	update_lable_from(label_rf_bandwidth_rx, "voltage0", "rf_bandwidth", false, "MHz", 1000000);
	update_lable_from(label_rf_bandwidth_obs, "voltage2", "rf_bandwidth", false, "MHz", 1000000);
	update_lable_from(label_rf_bandwidth_tx, "voltage0", "rf_bandwidth", true, "MHz", 1000000);

	update_lable_from(label_sampling_freq_rx, "voltage0", "sampling_frequency", false, "MSPS", 1000000);
	update_lable_from(label_sampling_freq_obs, "voltage2", "sampling_frequency", false, "MSPS", 1000000);
	update_lable_from(label_sampling_freq_tx, "voltage0", "sampling_frequency", true, "MSPS", 1000000);
}

static void dpd_update_labels(void)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tx_widgets[tx1_dpd].widget))) {
		update_lable_from(tx1_dpd_track_count, "voltage0", "dpd_track_count", true, "", 1);
		update_lable_from(tx1_dpd_model_error, "voltage0", "dpd_model_error", true, "%", 10);
		update_lable_from(tx1_dpd_external_path_delay, "voltage0", "dpd_external_path_delay", true, "", 16);
		update_dpd_status_from(tx1_dpd_status, "voltage0", "dpd_status");
	}

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tx_widgets[tx2_dpd].widget))) {
		update_lable_from(tx2_dpd_track_count, "voltage1", "dpd_track_count", true, "", 1);
		update_lable_from(tx2_dpd_model_error, "voltage1", "dpd_model_error", true, "%", 10);
		update_lable_from(tx2_dpd_external_path_delay, "voltage1", "dpd_external_path_delay", true, "", 16);
		update_dpd_status_from(tx2_dpd_status, "voltage1", "dpd_status");
	}
}

static void clgc_update_labels(void)
{

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tx_widgets[tx1_clgc].widget))) {
		iio_widget_update(&tx_widgets[tx1_clgc_desired_gain]);
		update_clgc_status_from(tx1_clgc_status, "voltage0", "clgc_status");
		update_lable_from(tx1_clgc_track_count, "voltage0", "clgc_track_count", true, "", 1);
		update_lable_from(tx1_clgc_current_gain, "voltage0", "clgc_current_gain", true, "dB", 100);
		update_lable_from(tx1_clgc_orx_gain, "voltage0", "clgc_orx_rms", true, "dBFS", 100);
		update_lable_from(tx1_clgc_tx_gain, "voltage0", "clgc_tx_gain", true, "dB", 20);
		update_lable_from(tx1_clgc_tx_rms, "voltage0", "clgc_tx_rms", true, "dBFS", 100);
	}

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tx_widgets[tx2_clgc].widget))) {
		iio_widget_update(&tx_widgets[tx2_clgc_desired_gain]);
		update_clgc_status_from(tx2_clgc_status, "voltage1", "clgc_status");
		update_lable_from(tx2_clgc_track_count, "voltage1", "clgc_track_count", true, "", 1);
		update_lable_from(tx2_clgc_current_gain, "voltage1", "clgc_current_gain", true, "dB", 100);
		update_lable_from(tx2_clgc_orx_gain, "voltage1", "clgc_orx_rms", true, "dBFS", 100);
		update_lable_from(tx2_clgc_tx_gain, "voltage1", "clgc_tx_gain", true, "dB", 20);
		update_lable_from(tx2_clgc_tx_rms, "voltage1", "clgc_tx_rms", true, "dBFS", 100);
	}
}

static void vswr_update_labels(void)
{

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tx_widgets[tx1_vswr].widget))) {
		update_vswr_status_from(tx1_vswr_status, "voltage0", "vswr_status");
		update_lable_from(tx1_vswr_track_count, "voltage0", "vswr_track_count", true, "", 1);
		update_lable_from(tx1_vswr_forward_gain, "voltage0", "vswr_forward_gain", true, "dB", 100);
		update_lable_from(tx1_vswr_forward_gain_imag, "voltage0", "vswr_forward_gain_imag", true, "dB", 100);
		update_lable_from(tx1_vswr_forward_gain_real, "voltage0", "vswr_forward_gain_real", true, "dB", 100);
		update_lable_from_prms(tx1_vswr_forward_orx, "voltage0", "vswr_forward_orx", true, "dBFS", 100);
		update_lable_from_prms(tx1_vswr_forward_tx, "voltage0", "vswr_forward_tx", true, "dBFS", 100);
		update_lable_from(tx1_vswr_reflected_gain, "voltage0", "vswr_reflected_gain", true, "dB", 100);
		update_lable_from(tx1_vswr_reflected_gain_imag, "voltage0", "vswr_reflected_gain_imag", true, "dB", 100);
		update_lable_from(tx1_vswr_reflected_gain_real, "voltage0", "vswr_reflected_gain_real", true, "dB", 100);
		update_lable_from_prms(tx1_vswr_reflected_orx, "voltage0", "vswr_reflected_orx", true, "dBFS", 100);
		update_lable_from_prms(tx1_vswr_reflected_tx, "voltage0", "vswr_reflected_tx", true, "dBFS", 100);
	}

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tx_widgets[tx2_vswr].widget))) {
		update_vswr_status_from(tx2_vswr_status, "voltage1", "vswr_status");
		update_lable_from(tx2_vswr_track_count, "voltage1", "vswr_track_count", true, "", 1);
		update_lable_from(tx2_vswr_forward_gain, "voltage1", "vswr_forward_gain", true, "dB", 100);
		update_lable_from(tx2_vswr_forward_gain_imag, "voltage1", "vswr_forward_gain_imag", true, "dB", 100);
		update_lable_from(tx2_vswr_forward_gain_real, "voltage1", "vswr_forward_gain_real", true, "dB", 100);
		update_lable_from_prms(tx2_vswr_forward_orx, "voltage1", "vswr_forward_orx", true, "dBFS", 100);
		update_lable_from_prms(tx2_vswr_forward_tx, "voltage1", "vswr_forward_tx", true, "dBFS", 100);
		update_lable_from(tx2_vswr_reflected_gain, "voltage1", "vswr_reflected_gain", true, "dB", 100);
		update_lable_from(tx2_vswr_reflected_gain_imag, "voltage1", "vswr_reflected_gain_imag", true, "dB", 100);
		update_lable_from(tx2_vswr_reflected_gain_real, "voltage1", "vswr_reflected_gain_real", true, "dB", 100);
		update_lable_from_prms(tx2_vswr_reflected_orx, "voltage1", "vswr_reflected_orx", true, "dBFS", 100);
		update_lable_from_prms(tx2_vswr_reflected_tx, "voltage1", "vswr_reflected_tx", true, "dBFS", 100);
	}
}

int load_myk_profile(const char *file_name,
		struct iio_device *dev1, struct iio_device *dev2,
		GtkWidget *panel, GtkFileChooser *chooser,
		char *last_profile)
{

	int ret = -ENOMEM;
	gchar *ptr, *path = NULL;
	FILE *f;

	if (!strncmp(file_name, "@FILTERS@/", sizeof("@FILTERS@/") - 1))
		path = g_build_filename(OSC_FILTER_FILE_PATH,
				file_name + sizeof("@FILTERS@/") - 1, NULL);
	else
		path = g_strdup(file_name);
	if (!path)
		goto err_set_filename;

	for (ptr = path; *ptr; ptr++)
		if (*ptr == '/')
			*ptr = G_DIR_SEPARATOR_S[0];

	f = fopen(path, "r");
	if (f) {
		char *buf;
		ssize_t len;
		int ret2;

		fseek(f, 0, SEEK_END);
		len = ftell(f);
		buf = malloc(len);
		fseek(f, 0, SEEK_SET);
		len = fread(buf, 1, len, f);
		fclose(f);

		iio_context_set_timeout(ctx, 30000);

		ret = iio_device_attr_write_raw(dev1,
				"profile_config", buf, len);
		if (dev2) {
			ret2 = iio_device_attr_write_raw(dev2,
					"profile_config", buf, len);
			ret = (ret > ret2) ? ret2 : ret;
		}

		iio_context_set_timeout(ctx, 3000);
		free(buf);
	}

	if (ret < 0) {
		fprintf(stderr, "Profile config failed: %s\n", path);
		GtkWidget *toplevel = gtk_widget_get_toplevel(panel);
		if (!gtk_widget_is_toplevel(toplevel))
			toplevel = NULL;

		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(toplevel),
						GTK_DIALOG_MODAL,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_CLOSE,
						"\nFailed to load profile using the selected file.");
		gtk_window_set_title(GTK_WINDOW(dialog), "Profile Configuration Failed");
		if (gtk_dialog_run(GTK_DIALOG(dialog)))
			gtk_widget_destroy(dialog);

	} else {
		if (last_profile)
			strncpy(last_profile, path, PATH_MAX);

	}

	profile_update();

	printf("Profile loaded: %s (ret = %i)\n", path, ret);
	if (ret >= 0)
		gtk_file_chooser_set_filename(chooser, path);
	g_free(path);

err_set_filename:
	if (ret < 0) {
		if (last_profile && last_profile[0])
			gtk_file_chooser_set_filename(chooser, last_profile);
		else
			gtk_file_chooser_set_filename(chooser, "(None)");
	}
	return ret;
}

static void glb_settings_update_labels(void)
{
	char buf[1024];
	ssize_t ret;

	ret = iio_device_attr_read(dev, "ensm_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(ensm_mode), buf);
	else
		gtk_label_set_text(GTK_LABEL(ensm_mode), "<error>");

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

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev, "voltage2", false),
			"gain_control_mode", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(obs_gain_control), buf);
	else
		gtk_label_set_text(GTK_LABEL(obs_gain_control), "<error>");

	profile_update_labels();

	iio_widget_update(&rx_widgets[rx1_gain]);
	if (is_2rx_2tx)
		iio_widget_update(&rx_widgets[rx2_gain]);

	iio_widget_update(&obsrx_widgets[obs_gain]);

}

static void rx_freq_info_update(void)
{
	double lo_freq;

	if (cap) {
		rx_update_device_sampling_freq(CAP_DEVICE,
						USE_INTERN_SAMPLING_FREQ);
		lo_freq = mhz_scale * gtk_spin_button_get_value(
			GTK_SPIN_BUTTON(rx_widgets[rx_lo].widget));

		rx_update_channel_lo_freq(CAP_DEVICE, "all", lo_freq);

	}

	if (cap_obs) {
		gchar *source;

		rx_update_device_sampling_freq(CAP_DEVICE_2,
						USE_INTERN_SAMPLING_FREQ);

		source = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(obs_port_select));

		if (source && strstr(source, "TX")) {
			lo_freq = mhz_scale * gtk_spin_button_get_value(
				GTK_SPIN_BUTTON(tx_widgets[tx_lo].widget));
		} else {
			lo_freq = mhz_scale * gtk_spin_button_get_value(
				GTK_SPIN_BUTTON(obsrx_widgets[sn_lo].widget));
		}
		rx_update_channel_lo_freq(CAP_DEVICE_2, "all", lo_freq);
		g_free(source);
	}
}

static void sample_frequency_changed_cb(void *data)
{
	glb_settings_update_labels();
	rx_freq_info_update();
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

	if (is_2rx_2tx) {
		rssi_update_label(rx2_rssi, "voltage1", false);
	}

	rssi_update_label(obs_rssi, "voltage2", false);
}

static gboolean update_display(gpointer foo)
{
	if (this_page == gtk_notebook_get_current_page(nbook) || plugin_detached) {
		gchar *gain_mode;

		rssi_update_labels();
		gain_mode = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(rx_gain_control_modes_rx1));
		if (gain_mode && strcmp(gain_mode, "manual")) {
			iio_widget_update(&rx_widgets[rx1_gain]);
			if (is_2rx_2tx)
				iio_widget_update(&rx_widgets[rx2_gain]);
		}
		g_free(gain_mode);

		if (has_dpd) {
			dpd_update_labels();
			clgc_update_labels();
			vswr_update_labels();
		}
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

static int split_target_lo_freq(double target_freq, double *ext_pll, double *ad9371_lo,
		double span, const double center_freq)
{
	double small_freq, large_freq;

	large_freq = get_span_multiple_from(target_freq, span);
	small_freq = target_freq - large_freq;

	*ad9371_lo = center_freq - small_freq * updn_freq_mix_sign;
	*ext_pll = center_freq + large_freq * updn_freq_mix_sign;

	return 0;
}

#define UPDN_RX 1
#define UPDN_TX 2

static void updn_converter_lo_freq_changed_cb(GtkSpinButton *button, int data)
{
	struct iio_channel *ad9371_ch, *updn_ch;
	double target_freq, ad9371_lo, updn_pll, center_freq;
	int ret;

	if (!has_udc_driver)
		return;

	if (data == UPDN_RX) {
		ad9371_ch = iio_device_find_channel(dev, "altvoltage0", true);
		updn_ch = iio_device_find_channel(udc_rx, "altvoltage0", true);
		center_freq = RX_CENTER_FREQ;
	} else if (data == UPDN_TX) {
		ad9371_ch = iio_device_find_channel(dev, "altvoltage1", true);
		updn_ch = iio_device_find_channel(udc_tx, "altvoltage0", true);
		center_freq = TX_CENTER_FREQ;
	} else {
		return;
	}

	target_freq = gtk_spin_button_get_value(button);
	split_target_lo_freq(target_freq, &updn_pll, &ad9371_lo, updn_freq_span, center_freq);
	ret = iio_channel_attr_write_longlong(ad9371_ch, freq_name, (long long)MHZ_TO_HZ(ad9371_lo));
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

	out[0] = iio_device_find_channel(cap, "voltage0_i", false);
	out[1] = iio_device_find_channel(cap, "voltage0_q", false);

	if (is_2rx_2tx) {
		out[2] = iio_device_find_channel(cap, "voltage1_i", false);
		out[3] = iio_device_find_channel(cap, "voltage1_q", false);
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

static void update_widgets(void)
{
	iio_update_widgets_of_device(widgets, num_glb + num_tx + num_rx + num_obsrx, dev);
	if (dds)
		iio_update_widgets_of_device(widgets, num_glb + num_tx + num_rx + num_obsrx, dds);
	dac_data_manager_update_iio_widgets(dac_tx_manager);

	iio_update_widgets(fpga_widgets, num_fpga);
}

static void profile_update(void)
{
	if (plugin_osc_running_state() == true) {
		plugin_osc_stop_capture();
		plugin_osc_start_capture();
	}

	glb_settings_update_labels();
	update_widgets();
	rx_freq_info_update();
}

static void reload_button_clicked(GtkButton *btn, gpointer data)
{
	update_widgets();
	profile_update();
	rx_freq_info_update();
	glb_settings_update_labels();
	rssi_update_labels();
	rx_phase_rotation_update();
}

static void hide_section_cb(GtkToggleToolButton *btn, GtkWidget *section)
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
			gtk_window_resize (GTK_WINDOW(toplevel), 1, 1);
	}
}

static void profile_config_file_set_cb (GtkFileChooser *chooser, gpointer data)
{
	char *file_name = gtk_file_chooser_get_filename(chooser);

	load_myk_profile(file_name, dev, NULL, ad9371_panel, chooser,
			last_profile);
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

static void int_dec_freq_update(void)
{
	struct iio_channel *ch;
	double freq;
	gchar *text;

	ch = iio_device_find_channel(cap, "voltage0_i", false);
	iio_channel_attr_read_double(ch, "sampling_frequency", &freq);

	text = g_strdup_printf ("%f", freq / mhz_scale);
	gtk_label_set_text(GTK_LABEL(label_sampling_freq_rx), text);
	g_free(text);

	ch = iio_device_find_channel(dds, "voltage0", true);
	iio_channel_attr_read_double(ch, "sampling_frequency", &freq);

	text = g_strdup_printf ("%f", freq / mhz_scale);
	gtk_label_set_text(GTK_LABEL(label_sampling_freq_tx), g_strdup_printf ("%f", freq / mhz_scale));
	g_free(text);
}

static void int_dec_update_cb(GtkComboBox *cmb, gpointer data)
{
	int_dec_freq_update();
	rx_freq_info_update();
}

static double get_gui_tx_sampling_freq(void)
{
	return gtk_spin_button_get_value(GTK_SPIN_BUTTON(tx_widgets[tx_sample_freq].widget));
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
	struct iio_channel *out0, *out1;
	gdouble val, phase;

	if (!cap)
		return;

	val = gtk_spin_button_get_value(spinbutton);

	phase = val * 2 * M_PI / 360.0;

	if (offset == 2) {
		out0 = iio_device_find_channel(cap, "voltage1_i", false);
		out1 = iio_device_find_channel(cap, "voltage1_q", false);
	} else {
		out0 = iio_device_find_channel(cap, "voltage0_i", false);
		out1 = iio_device_find_channel(cap, "voltage0_q", false);
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
 * Return 1 if the channel combination is valid
 * Return 0 if the combination is not valid
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
		else if (GTK_IS_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "clicked");
		else
			printf("unhandled widget type, attribute: %s (%u)\n", widgets[i].attr_name, i);

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

static int ad9371_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	int ret = 0;

	if (MATCH_ATTRIB("load_myk_profile_file")) {
		if (value[0]) {
			load_myk_profile(value, dev, NULL, ad9371_panel,
					GTK_FILE_CHOOSER(profile_config),
					last_profile);
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
	} else if (MATCH_ATTRIB("obs_show")) {
		gtk_toggle_tool_button_set_active(
				section_toggle[SECTION_OBS], !!atoi(value));
		hide_section_cb(section_toggle[SECTION_OBS],
				section_setting[SECTION_OBS]);
	} else if (MATCH_ATTRIB("fpga_show")) {
		gtk_toggle_tool_button_set_active(
				section_toggle[SECTION_FPGA], !!atoi(value));
		hide_section_cb(section_toggle[SECTION_FPGA],
				section_setting[SECTION_FPGA]);
	} else if (!strncmp(attrib, "tx_channel_", sizeof("tx_channel_") - 1)) {
		int tx = atoi(attrib + sizeof("tx_channel_") - 1);
		dac_data_manager_set_tx_channel_state(
				dac_tx_manager, tx, !!atoi(value));
	} else if (MATCH_ATTRIB("dac_buf_filename")) {
		dac_data_manager_set_buffer_chooser_filename(
				dac_tx_manager, value);
	} else if (MATCH_ATTRIB("SYNC_RELOAD")) {
		if (can_update_widgets)
			reload_button_clicked(NULL, NULL);
	} else {
		return -EINVAL;
	}

	return ret;
}

static int ad9371_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			ad9371_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	struct iio_channel *ch;
	char *value;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ad9371_driver_attribs); i++) {
		char *value = read_token_from_ini(ini_fn, THIS_DRIVER,
				ad9371_driver_attribs[i]);
		if (value) {
			ad9371_handle_driver(NULL,
					ad9371_driver_attribs[i], value);
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

	update_from_ini(ini_fn, THIS_DRIVER, dev, ad9371_sr_attribs,
			ARRAY_SIZE(ad9371_sr_attribs));
	if (dds)
		update_from_ini(ini_fn, THIS_DRIVER, dds, ad9371_sr_attribs,
				ARRAY_SIZE(ad9371_sr_attribs));

	if (can_update_widgets)
		reload_button_clicked(NULL, NULL);
}

static GtkWidget * ad9371_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *dds_container;
	struct iio_channel *ch0, *ch1, *ch2;

	can_update_widgets = false;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	dev = iio_context_find_device(ctx, PHY_DEVICE);
	if (!dev) {
		fprintf(stderr, "Failed to find phy device %s\n", PHY_DEVICE);
		goto destroy_ctx;
	}

	dds = iio_context_find_device(ctx, DDS_DEVICE);
	if (!dds) {
		fprintf(stderr, "Failed to find dds device %s\n", DDS_DEVICE);
		goto destroy_ctx;
	}

	cap = iio_context_find_device(ctx, CAP_DEVICE);
	if (!cap) {
		fprintf(stderr, "Failed to find cap device %s\n", CAP_DEVICE);
		goto destroy_ctx;
	}

	cap_obs = iio_context_find_device(ctx, CAP_DEVICE_2);
	if (!cap_obs) {
		fprintf(stderr, "Failed to find cap obs device %s\n", CAP_DEVICE_2);
		goto destroy_ctx;
	}

	udc_rx = iio_context_find_device(ctx, UDC_RX_DEVICE);
	udc_tx = iio_context_find_device(ctx, UDC_TX_DEVICE);

	has_udc_driver = (udc_rx && udc_tx);

	ch0 = iio_device_find_channel(dev, "voltage0", false); /* RX1 */
	ch1 = iio_device_find_channel(dev, "voltage1", false); /* RX2 */
	ch2 = iio_device_find_channel(dev, "voltage2", false); /* OBS-RX */

	dac_tx_manager = dac_data_manager_new(dds, NULL, ctx);

	dac_data_manager_set_buffer_size_alignment(dac_tx_manager, 16);

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

	if (osc_load_glade_file(builder, "ad9371") < 0)
		goto free_dac_manager;

	is_2rx_2tx = ch1 && iio_channel_find_attr(ch1, "hardwaregain");

	ad9371_panel = GTK_WIDGET(gtk_builder_get_object(builder, "ad9371_panel"));

	/* Global settings */

	profile_config = GTK_WIDGET(gtk_builder_get_object(builder, "profile_config"));

	ensm_mode = GTK_WIDGET(gtk_builder_get_object(builder, "ensm_mode"));
	ensm_mode_available = GTK_WIDGET(gtk_builder_get_object(builder, "ensm_mode_available"));
	up_down_converter = GTK_WIDGET(gtk_builder_get_object(builder, "checkbox_up_down_converter"));
	section_toggle[SECTION_GLOBAL] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "global_settings_toggle"));
	section_setting[SECTION_GLOBAL] = GTK_WIDGET(gtk_builder_get_object(builder, "global_settings"));
	section_toggle[SECTION_TX] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "tx_toggle"));
	section_setting[SECTION_TX] = GTK_WIDGET(gtk_builder_get_object(builder, "tx_settings"));
	section_toggle[SECTION_RX] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "rx_toggle"));
	section_setting[SECTION_RX] = GTK_WIDGET(gtk_builder_get_object(builder, "rx_settings"));
	section_toggle[SECTION_OBS] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "obs_toggle"));
	section_setting[SECTION_OBS] = GTK_WIDGET(gtk_builder_get_object(builder, "obs_settings"));
	section_toggle[SECTION_FPGA] = GTK_TOGGLE_TOOL_BUTTON(gtk_builder_get_object(builder, "fpga_toggle"));
	section_setting[SECTION_FPGA] = GTK_WIDGET(gtk_builder_get_object(builder, "fpga_settings"));

	/* Receive Chain */

	rx_gain_control_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx1"));
	rx_gain_control_rx2 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx2"));
	rx_gain_control_modes_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx1"));
//	rx_gain_control_modes_rx2 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx2"));
	rx1_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx1"));
	rx2_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx2"));
	fpga_rx_frequency_available = GTK_WIDGET(gtk_builder_get_object(builder, "fpga_rx_frequency_available"));

	/* Observation Receive Chain */

	obs_port_select = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_obs"));
	obs_gain_control = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_obs"));
	obs_gain_control_modes = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_obs"));
	obs_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_obs"));

	/* Transmit Chain */

	fpga_tx_frequency_available = GTK_WIDGET(gtk_builder_get_object(builder, "fpga_tx_frequency_available"));
	dds_container = GTK_WIDGET(gtk_builder_get_object(builder, "dds_transmit_block"));
	if (dac_tx_manager)
		gtk_container_add(GTK_CONTAINER(dds_container),
			dac_data_manager_get_gui_container(dac_tx_manager));
	gtk_widget_show_all(dds_container);

	rx_phase_rotation[0] = GTK_WIDGET(gtk_builder_get_object(builder, "rx1_phase_rotation"));
	rx_phase_rotation[1] = GTK_WIDGET(gtk_builder_get_object(builder, "rx2_phase_rotation"));

	gtk_combo_box_set_active(GTK_COMBO_BOX(ensm_mode_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes_rx1), 0);
//	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes_rx2), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(fpga_rx_frequency_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(fpga_tx_frequency_available), 0);

	GtkWidget *sfreq = GTK_WIDGET(gtk_builder_get_object(builder, "sampling_freq_tx"));
	GtkAdjustment *sfreq_adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(sfreq));


	gtk_adjustment_set_upper(sfreq_adj, 307.20);

	/* Bind the IIO device files to the GUI widgets */

	glb_widgets = widgets;

	/* Global settings */
	iio_combo_box_init(&glb_widgets[num_glb++],
		dev, NULL, "ensm_mode", "ensm_mode_available",
		ensm_mode_available, NULL);

	iio_toggle_button_init_from_builder(&glb_widgets[num_glb++],
					    dev, NULL, "calibrate_dpd_en", builder,
				     "calibrate_dpd_en", 0);

	iio_toggle_button_init_from_builder(&glb_widgets[num_glb++],
					    dev, NULL, "calibrate_clgc_en", builder,
				     "calibrate_clgc_en", 0);

	iio_toggle_button_init_from_builder(&glb_widgets[num_glb++],
					    dev, NULL, "calibrate_vswr_en", builder,
				     "calibrate_vswr_en", 0);

	iio_toggle_button_init_from_builder(&glb_widgets[num_glb++],
					    dev, NULL, "calibrate_rx_qec_en", builder,
				     "calibrate_rx_qec_en", 0);

	iio_toggle_button_init_from_builder(&glb_widgets[num_glb++],
					    dev, NULL, "calibrate_tx_qec_en", builder,
				     "calibrate_tx_qec_en", 0);

	iio_toggle_button_init_from_builder(&glb_widgets[num_glb++],
					    dev, NULL, "calibrate_tx_lol_en", builder,
				     "calibrate_tx_lol_en", 0);

	iio_toggle_button_init_from_builder(&glb_widgets[num_glb++],
					    dev, NULL, "calibrate_tx_lol_ext_en", builder,
				     "calibrate_tx_lol_ext_en", 0);

	iio_button_init_from_builder(&glb_widgets[num_glb++],
					    dev, NULL, "calibrate", builder,
				     "calibrate");

	rx_widgets = &glb_widgets[num_glb];

	/* Receive Chain */

	iio_combo_box_init(&rx_widgets[num_rx++],
		dev, ch0, "gain_control_mode",
		"gain_control_mode_available",
		rx_gain_control_modes_rx1, NULL);

	iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "temp_comp_gain", builder,
		"temp_comp_gain_rx1", NULL);

	iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch1, "temp_comp_gain", builder,
		"temp_comp_gain_rx2", NULL);

	rx1_gain = num_rx;
	iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "hardwaregain", builder,
		"hardware_gain_rx1", NULL);

	if (is_2rx_2tx) {
		rx2_gain = num_rx;
		iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
			dev, ch1, "hardwaregain", builder,
			"hardware_gain_rx2", NULL);
	}
	rx_sample_freq = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "sampling_frequency", builder,
		"sampling_freq_rx", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "quadrature_tracking_en", builder,
		"rx1_quadrature_tracking_en", 0);

	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch1, "quadrature_tracking_en", builder,
		"rx2_quadrature_tracking_en", 0);

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

// 	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
// 		dev, ch1, "external", builder,
// 		"rx_lo_external", 0);

// 	iio_spin_button_init_from_builder(&rx_widgets[num_rx],
// 		dev, ch1, "calibphase",
// 		builder, "rx1_phase_rotation", NULL);
// 	iio_spin_button_add_progress(&rx_widgets[num_rx++]);

	/* Observation Receiver Chain */

	obsrx_widgets = &rx_widgets[num_rx];

	iio_combo_box_init(&obsrx_widgets[num_obsrx++],
		dev, ch2, "gain_control_mode",
		"gain_control_mode_available",
		obs_gain_control_modes, NULL);

	iio_combo_box_init(&obsrx_widgets[num_obsrx++],
		dev, ch2, "rf_port_select",
		"rf_port_select_available",
		obs_port_select, NULL);

	iio_spin_button_init_from_builder(&obsrx_widgets[num_obsrx++],
		dev, ch2, "temp_comp_gain", builder,
		"temp_comp_gain_obs", NULL);

	obs_gain = num_obsrx;
	iio_spin_button_init_from_builder(&obsrx_widgets[num_obsrx++],
		dev, ch2, "hardwaregain", builder,
		"hardware_gain_obs", NULL);

	iio_toggle_button_init_from_builder(&obsrx_widgets[num_obsrx++],
		dev, ch2, "quadrature_tracking_en", builder,
		"obs_quadrature_tracking_en", 0);

	sn_lo = num_obsrx;
	ch2 = iio_device_find_channel(dev, "altvoltage2", true);
	if (iio_channel_find_attr(ch2, "frequency"))
		freq_name = "frequency";
	else
		freq_name = "RX_SN_LO_frequency";
	iio_spin_button_s64_init_from_builder(&obsrx_widgets[num_obsrx++],
		dev, ch2, freq_name, builder,
		"sn_lo_freq", &mhz_scale);
	iio_spin_button_add_progress(&obsrx_widgets[num_obsrx - 1]);

// 	iio_toggle_button_init_from_builder(&obsrx_widgets[num_obsrx++],
// 		dev, ch2, "external", builder,
// 		"obs_lo_external", 0);

	/* Transmit Chain */

	tx_widgets = &obsrx_widgets[num_obsrx];

	ch0 = iio_device_find_channel(dev, "voltage0", true);
	if (is_2rx_2tx)
		ch1 = iio_device_find_channel(dev, "voltage1", true);

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

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		dev, ch0, "quadrature_tracking_en", builder,
		"tx1_quadrature_tracking_en", 0);

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		dev, ch0, "lo_leakage_tracking_en", builder,
		"tx1_lo_leakage_tracking_en", 0);

	if (is_2rx_2tx) {
		iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, ch1, "quadrature_tracking_en", builder,
			"tx2_quadrature_tracking_en", 0);

		iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, ch1, "lo_leakage_tracking_en", builder,
			"tx2_lo_leakage_tracking_en", 0);
	}

	has_dpd = !!iio_channel_find_attr(ch0, "dpd_tracking_en");

	if (has_dpd) {
		/* DPD */
		tx1_dpd = num_tx;
		iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
						dev, ch0, "dpd_tracking_en", builder,
					"out_voltage0_dpd_tracking_en", 0);

		tx2_dpd = num_tx;
		iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
						dev, ch1, "dpd_tracking_en", builder,
					"out_voltage1_dpd_tracking_en", 0);

		iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
						dev, ch0, "dpd_actuator_en", builder,
					"out_voltage0_dpd_actuator_en", 0);

		iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
						dev, ch1, "dpd_actuator_en", builder,
					"out_voltage1_dpd_actuator_en", 0);

		iio_button_init_from_builder(&tx_widgets[num_tx++],
						dev, ch0, "dpd_reset_en", builder,
					"out_voltage0_dpd_reset_en");

		iio_button_init_from_builder(&tx_widgets[num_tx++],
						dev, ch1, "dpd_reset_en", builder,
					"out_voltage1_dpd_reset_en");

		tx1_dpd_track_count = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_dpd_track_count"));
		tx1_dpd_model_error = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_dpd_model_error"));
		tx1_dpd_external_path_delay = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_dpd_external_path_delay"));
		tx1_dpd_status = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_dpd_status"));

		tx2_dpd_track_count = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_dpd_track_count"));
		tx2_dpd_model_error = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_dpd_model_error"));
		tx2_dpd_external_path_delay = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_dpd_external_path_delay"));
		tx2_dpd_status = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_dpd_status"));


		/* CLGC */

		tx1_clgc = num_tx;
		iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
						dev, ch0, "clgc_tracking_en", builder,
					"out_voltage0_clgc_tracking_en", 0);
		tx2_clgc = num_tx;
		iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
						dev, ch1, "clgc_tracking_en", builder,
					"out_voltage1_clgc_tracking_en", 0);

		tx1_clgc_desired_gain = num_tx;
		iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
						dev, ch0, "clgc_desired_gain", builder,
					"out_voltage0_clgc_desired_gain", &scale100);

		tx2_clgc_desired_gain = num_tx;
		iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
						dev, ch1, "clgc_desired_gain", builder,
				    "out_voltage1_clgc_desired_gain", &scale100);

		tx1_clgc_track_count = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_clgc_track_count"));
		tx1_clgc_status = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_clgc_status"));
		tx1_clgc_current_gain = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_clgc_current_gain"));
		tx1_clgc_orx_gain = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_clgc_orx_rms"));
		tx1_clgc_tx_gain = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_clgc_tx_gain"));
		tx1_clgc_tx_rms = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_clgc_tx_rms"));

		tx2_clgc_track_count = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_clgc_track_count"));
		tx2_clgc_status = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_clgc_status"));
		tx2_clgc_current_gain = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_clgc_current_gain"));
		tx2_clgc_orx_gain = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_clgc_orx_rms"));
		tx2_clgc_tx_gain = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_clgc_tx_gain"));
		tx2_clgc_tx_rms = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_clgc_tx_rms"));

		/* VSWR */
		tx1_vswr = num_tx;
		iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
						dev, ch0, "vswr_tracking_en", builder,
					"out_voltage0_vswr_tracking_en", 0);

		tx2_vswr = num_tx;
		iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
						dev, ch1, "vswr_tracking_en", builder,
					"out_voltage1_vswr_tracking_en", 0);

		tx1_vswr_track_count = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_track_count"));
		tx1_vswr_status = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_status"));
		tx1_vswr_forward_gain = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_forward_gain"));
		tx1_vswr_forward_gain_imag = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_forward_gain_imag"));
		tx1_vswr_forward_gain_real = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_forward_gain_real"));
		tx1_vswr_forward_orx = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_forward_orx"));
		tx1_vswr_forward_tx = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_forward_tx"));
		tx1_vswr_reflected_gain = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_reflected_gain"));
		tx1_vswr_reflected_gain_imag = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_reflected_gain_imag"));
		tx1_vswr_reflected_gain_real = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_reflected_gain_real"));
		tx1_vswr_reflected_orx = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_reflected_orx"));
		tx1_vswr_reflected_tx = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage0_vswr_reflected_tx"));

		tx2_vswr_track_count = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_track_count"));
		tx2_vswr_status = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_status"));
		tx2_vswr_forward_gain = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_forward_gain"));
		tx2_vswr_forward_gain_imag = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_forward_gain_imag"));
		tx2_vswr_forward_gain_real = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_forward_gain_real"));
		tx2_vswr_forward_orx = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_forward_orx"));
		tx2_vswr_forward_tx = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_forward_tx"));
		tx2_vswr_reflected_gain = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_reflected_gain"));
		tx2_vswr_reflected_gain_imag = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_reflected_gain_imag"));
		tx2_vswr_reflected_gain_real = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_reflected_gain_real"));
		tx2_vswr_reflected_orx = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_reflected_orx"));
		tx2_vswr_reflected_tx = GTK_WIDGET(gtk_builder_get_object(builder, "out_voltage1_vswr_reflected_tx"));

		fixup_label(GTK_LABEL(gtk_builder_get_object(builder, "label_global_settings")), "1", "5");
		fixup_label(GTK_LABEL(gtk_builder_get_object(builder, "label_receive_chain")), "1", "5");
		fixup_label(GTK_LABEL(gtk_builder_get_object(builder, "label_transmit_chain")), "1", "5");
		fixup_label(GTK_LABEL(gtk_builder_get_object(builder, "label_obs_chain")), "1", "5");
	} else {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_dpd")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_clgc")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_vswr")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "calibrate_dpd_en")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "calibrate_clgc_en")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "calibrate_vswr_en")));
	}

	tx_lo = num_tx;
	ch1 = iio_device_find_channel(dev, "altvoltage1", true);

	if (iio_channel_find_attr(ch1, "frequency"))
		freq_name = "frequency";
	else
		freq_name = "TX_LO_frequency";
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
		dev, ch1, freq_name, builder, "tx_lo_freq", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

// 	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
// 		dev, ch1, "external", builder,
// 		"tx_lo_external", 0);

	/* FPGA widgets */
	fpga_widgets = &tx_widgets[num_tx];

	ch0 = iio_device_find_channel(dds, "voltage0", true);
	if (iio_channel_find_attr(ch0, "sampling_frequency_available")) {
		iio_combo_box_init(&fpga_widgets[num_fpga++],
				dds, ch0, "sampling_frequency",
			"sampling_frequency_available",
			fpga_tx_frequency_available, NULL);
	} else {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder,
				"transmit_frame_dma_buf")));
	}

	ch0 = iio_device_find_channel(cap, "voltage0_i", false);
	if (iio_channel_find_attr(ch0, "sampling_frequency_available")) {
		iio_combo_box_init(&fpga_widgets[num_fpga++],
				cap, ch0, "sampling_frequency",
			"sampling_frequency_available",
			fpga_rx_frequency_available, NULL);
	} else {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder,
				"receive_frame_dma_buf")));
	}

	if (ini_fn)
		load_profile(NULL, ini_fn);

	label_rf_bandwidth_tx = GTK_WIDGET(gtk_builder_get_object(builder, "label_rf_bandwidth_tx"));
	label_sampling_freq_tx = GTK_WIDGET(gtk_builder_get_object(builder, "label_sampling_freq_tx"));
	label_rf_bandwidth_obs = GTK_WIDGET(gtk_builder_get_object(builder, "label_rf_bandwidth_obs"));
	label_sampling_freq_obs = GTK_WIDGET(gtk_builder_get_object(builder, "label_sampling_freq_obs"));
	label_rf_bandwidth_rx = GTK_WIDGET(gtk_builder_get_object(builder, "label_rf_bandwidth_rx"));
	label_sampling_freq_rx = GTK_WIDGET(gtk_builder_get_object(builder, "label_sampling_freq_rx"));

	/* Update all widgets with current values */
	printf("Updating widgets...\n");
	update_widgets();
	rx_freq_info_update();
	printf("Updating FIR filter...\n");
	profile_update();
	glb_settings_update_labels();
	rssi_update_labels();
	dac_data_manager_freq_widgets_range_update(dac_tx_manager,
		get_gui_tx_sampling_freq() / 2.0);
	dac_data_manager_update_iio_widgets(dac_tx_manager);

	/* Connect signals */

	g_builder_connect_signal(builder, "rx1_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)0);

	g_builder_connect_signal(builder, "rx2_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)2);

	g_builder_connect_signal(builder, "sampling_freq_tx", "value-changed",
			G_CALLBACK(tx_sample_rate_changed), NULL);

	g_builder_connect_signal(builder, "ad9371_settings_reload", "clicked",
		G_CALLBACK(reload_button_clicked), NULL);

	g_builder_connect_signal(builder, "profile_config", "file-set",
		G_CALLBACK(profile_config_file_set_cb), NULL);

	g_signal_connect_after(section_toggle[SECTION_GLOBAL], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_GLOBAL]);

	g_signal_connect_after(section_toggle[SECTION_TX], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_TX]);

	g_signal_connect_after(section_toggle[SECTION_RX], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_RX]);

	g_signal_connect_after(section_toggle[SECTION_OBS], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_OBS]);

	g_signal_connect_after(section_toggle[SECTION_FPGA], "clicked",
		G_CALLBACK(hide_section_cb), section_setting[SECTION_FPGA]);

	g_signal_connect_after(ensm_mode_available, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);

	g_signal_connect_after(rx_gain_control_modes_rx1, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);

	g_signal_connect_after(obs_gain_control_modes, "changed",
		G_CALLBACK(glb_settings_update_labels), NULL);

	g_signal_connect(up_down_converter, "toggled",
		G_CALLBACK(up_down_converter_toggled_cb), NULL);

	g_signal_connect_after(fpga_rx_frequency_available, "changed",
		G_CALLBACK(int_dec_update_cb), label_sampling_freq_rx);

	g_signal_connect_after(fpga_tx_frequency_available, "changed",
		G_CALLBACK(int_dec_update_cb), label_sampling_freq_tx);

	make_widget_update_signal_based(glb_widgets, num_glb);
	make_widget_update_signal_based(rx_widgets, num_rx);
	make_widget_update_signal_based(obsrx_widgets, num_obsrx);
	make_widget_update_signal_based(tx_widgets, num_tx);
	make_widget_update_signal_based(fpga_widgets, num_fpga);

	iio_spin_button_set_on_complete_function(&rx_widgets[rx_sample_freq],
		sample_frequency_changed_cb, NULL);
	iio_spin_button_set_on_complete_function(&tx_widgets[tx_sample_freq],
		sample_frequency_changed_cb, NULL);
	iio_spin_button_set_on_complete_function(&rx_widgets[rx_lo],
		sample_frequency_changed_cb, NULL);
	iio_spin_button_set_on_complete_function(&tx_widgets[tx_lo],
		sample_frequency_changed_cb, NULL);
	iio_spin_button_set_on_complete_function(&obsrx_widgets[sn_lo],
		sample_frequency_changed_cb, NULL);

	add_ch_setup_check_fct(CAP_DEVICE, channel_combination_check);

	block_diagram_init(builder, 2, "AD9371.svg", "ADRV9371-N_PCBZ.jpg");

	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(profile_config), OSC_FILTER_FILE_PATH);
	dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager, OSC_WAVEFORM_FILE_PATH);

	if (!is_2rx_2tx) {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_rx2")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_fpga_rx2")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "table_hw_gain_tx2")));
	}

	gtk_widget_set_visible(up_down_converter, has_udc_driver);

	if (!dac_tx_manager)
		gtk_widget_hide(gtk_widget_get_parent(section_setting[SECTION_FPGA]));

	g_timeout_add(1000, (GSourceFunc) update_display, ctx);
	can_update_widgets = true;

	return ad9371_panel;

free_dac_manager:
	dac_data_manager_free(dac_tx_manager);
	dac_tx_manager = NULL;
destroy_ctx:
	osc_destroy_context(ctx);
	return NULL;

}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void ad9371_get_preferred_size(const struct osc_plugin *plugin, int *width, int *height)
{
	if (width)
		*width = 1100;
	if (height)
		*height = 800;
}

static void save_widgets_to_ini(FILE *f)
{
	fprintf(f, "load_myk_profile_file = %s\n"
			"dds_mode_tx1 = %i\n"
			"dds_mode_tx2 = %i\n"
			"dac_buf_filename = %s\n"
			"tx_channel_0 = %i\n"
			"tx_channel_1 = %i\n"
			"tx_channel_2 = %i\n"
			"tx_channel_3 = %i\n"
			"global_settings_show = %i\n"
			"tx_show = %i\n"
			"rx_show = %i\n"
			"obs_show = %i\n"
			"fpga_show = %i\n",
			last_profile,
			dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE, 1),
			dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE, 2),
			dac_data_manager_get_buffer_chooser_filename(dac_tx_manager),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 0),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 1),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 2),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 3),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_GLOBAL]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_TX]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_RX]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_OBS]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_FPGA]));
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		save_to_ini(f, THIS_DRIVER, dev, ad9371_sr_attribs,
				ARRAY_SIZE(ad9371_sr_attribs));
		if (dds)
			save_to_ini(f, NULL, dds, ad9371_sr_attribs,
					ARRAY_SIZE(ad9371_sr_attribs));
		save_widgets_to_ini(f);
		fclose(f);
	}
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	g_source_remove_by_user_data(ctx);

	if (ini_fn)
		save_profile(NULL, ini_fn);

	dac_data_manager_free(dac_tx_manager);
	dac_tx_manager = NULL;
	osc_destroy_context(ctx);
}

struct osc_plugin plugin;

static bool ad9371_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	if (!iio_context_find_device(osc_ctx, PHY_DEVICE))
		return false;

	/* Check if ad9371+x is used */
	return !iio_context_find_device(osc_ctx, "ad9371-phy-B");
}

GSList* get_dac_dev_names(const struct osc_plugin *plugin) {
	GSList *list = NULL;

	list = g_slist_append (list, (gpointer) DDS_DEVICE);

	return list;
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = ad9371_identify,
	.init = ad9371_init,
	.handle_item = ad9371_handle,
	.handle_external_request = handle_external_request,
	.update_active_page = update_active_page,
	.get_preferred_size = ad9371_get_preferred_size,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
	.get_dac_dev_names = get_dac_dev_names,
};
