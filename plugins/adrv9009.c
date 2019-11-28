/**
 * Copyright (C) 2018 Analog Devices, Inc.
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
//#include "block_diagram.h"
#include "dac_data_manager.h"
#include "../iio_utils.h"

#define HANNING_ENBW 1.50

#define THIS_DRIVER "ADRV9009"
#define PHY_DEVICE "adrv9009-phy"
#define DDS_DEVICE "axi-adrv9009-tx-hpc"
#define CAP_DEVICE "axi-adrv9009-rx-hpc"
#define CAP_DEVICE_2 "axi-adrv9009-rx-obs-hpc"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

enum plugin_section
{
	SECTION_GLOBAL,
	SECTION_TX,
	SECTION_RX,
	SECTION_OBS,
	SECTION_FPGA,
	SECTION_NUM,
};

/* This structure contains all information related to one adrv9009-phy device.
 * This plugin will dynamically create new sets of widgets for each additional
 * device that will find.
 */
struct plugin_subcomponent
{
	/* References to IIO structures */
	struct iio_device *iio_dev;
	struct iio_channel *ch0, *ch1, *ch2, *ch3, *alt_ch0, *alt_ch1, *out_ch0, *out_ch1;

	/* Associated GTK builder */
	GtkBuilder *builder;

	/* List of containers of widgets grouped for each section */
	GtkWidget *section_containers[SECTION_NUM];

	/* Widgets for Receive Settings */
	GtkWidget *rx_gain_control_rx1;
	GtkWidget *rx_gain_control_modes_rx1;
	GtkWidget *rx_gain_control_rx2;
	GtkWidget *rx1_rssi;
	GtkWidget *rx2_rssi;
	GtkWidget *label_rf_bandwidth_rx;
	GtkWidget *label_sampling_freq_rx;

	/* Widgets for Transmit Settings */
	GtkWidget *label_rf_bandwidth_tx;
	GtkWidget *label_sampling_freq_tx;

	/* Widgets for Observation Receive Settings */
	GtkWidget *obs_port_select;
	//GtkWidget *obs_rssi;
	GtkWidget *label_rf_bandwidth_obs;
	GtkWidget *label_sampling_freq_obs;

	/* Widgets for FPGA Settings */
	GtkWidget *rx_phase_rotation[2];

	/* IIO Widgets */
	struct iio_widget widgets[200];
	struct iio_widget *glb_widgets, *tx_widgets, *rx_widgets, *obsrx_widgets;
	unsigned int num_glb, num_tx, num_rx, num_obsrx;

	/* Useful indexes of IIO widgets from the list of iio widgets of this subcomponent */
	unsigned int rx1_gain, rx2_gain, obs_gain;
	unsigned int trx_lo, aux_lo;
	unsigned int rx_sample_freq, tx_sample_freq;

	/* Save/Restore attributes */
	char **sr_attribs;
	size_t sr_attribs_count;
};

extern bool dma_valid_selection(const char *device, unsigned mask, unsigned channel_count);

static struct plugin_subcomponent *subcomponents;
static bool plugin_single_device_mode = TRUE;
static guint phy_devs_count = 0;
static struct dac_data_manager *dac_tx_manager;

static bool can_update_widgets;

static const gdouble mhz_scale = 1000000.0;
static const gdouble inv_scale = -1.0;

static char last_profile[PATH_MAX];

static struct iio_context *ctx;
static struct iio_device *dds, *cap, *cap_obs;

static GtkToggleToolButton *section_toggle[SECTION_NUM];
static GtkWidget *section_setting[SECTION_NUM];

/* Widgets for Global Settings */
static GtkWidget *ensm_mode;
static GtkWidget *ensm_mode_available;
static GtkWidget *profile_config;
static struct iio_widget iio_ensm_mode_available;

/* Widgets for interpolation/decimation */
GtkWidget *fpga_rx_frequency_available;
GtkWidget *fpga_tx_frequency_available;
struct iio_widget fpga_widgets[2];
static unsigned int num_fpga = 0;

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *adrv9009_panel;
static gboolean plugin_detached;

static const char *adrv9009_sr_attribs[] = {
	".calibrate_fhm_en",
	".calibrate_rx_phase_correction_en",
	".calibrate_rx_qec_en",
	".calibrate_tx_lol_en",
	".calibrate_tx_lol_ext_en",
	".calibrate_tx_qec_en",
	".ensm_mode",
	".in_voltage0_gain_control_mode",
	".in_voltage0_gain_control_pin_mode_en",
	".in_voltage0_hardwaregain",
	".in_voltage0_hd2_tracking_en",
	".in_voltage0_powerdown",
	".in_voltage0_quadrature_tracking_en",
	".in_voltage1_gain_control_pin_mode_en",
	".in_voltage1_hardwaregain",
	".in_voltage1_hd2_tracking_en",
	".in_voltage1_powerdown",
	".in_voltage1_quadrature_tracking_en",
	".in_voltage2_hardwaregain",
	".in_voltage2_powerdown",
	".in_voltage2_quadrature_tracking_en",
	".in_voltage2_rf_port_select",
	".in_voltage2_rf_port_select_available",
	".in_voltage3_hardwaregain",
	".in_voltage3_powerdown",
	".in_voltage3_quadrature_tracking_en",
	".in_voltage3_rf_port_select",
	".out_altvoltage0_TRX_LO_frequency",
	".out_altvoltage0_TRX_LO_frequency_hopping_mode_enable",
	".out_altvoltage1_AUX_OBS_RX_LO_frequency",
	".out_voltage0_atten_control_pin_mode_en",
	".out_voltage0_hardwaregain",
	".out_voltage0_lo_leakage_tracking_en",
	".out_voltage0_pa_protection_en",
	".out_voltage0_powerdown",
	".out_voltage0_quadrature_tracking_en",
	".out_voltage1_atten_control_pin_mode_en",
	".out_voltage1_hardwaregain",
	".out_voltage1_lo_leakage_tracking_en",
	".out_voltage1_pa_protection_en",
	".out_voltage1_powerdown",
	".out_voltage1_quadrature_tracking_en",
	".out_voltage1_rf_bandwidth",
};

static char **dds_device_sr_attribs = NULL;
static unsigned dds_device_sr_attribs_count = 0;

static const char *adrv9009_driver_attribs[] = {
	"load_tal_profile_file",
	"ensm_mode",
	"global_settings_show",
	"tx_show",
	"rx_show",
	"fpga_show",
	"fpga_rx_frequency_available",
	"fpga_tx_frequency_available",
	"dac_buf_filename",
};

static void profile_update(void);

static void build_dds_sr_attribs_list(unsigned devices_count)
{
	static const unsigned DDS_CHANNEL_COUNT = 8;
	static const unsigned CHANNEL_ATTRIB_COUNT = 4; /*freq, phase, raw, scale*/
	unsigned total_dds_chn_count = devices_count * DDS_CHANNEL_COUNT;
	guint i;

	dds_device_sr_attribs_count = total_dds_chn_count * CHANNEL_ATTRIB_COUNT;
	dds_device_sr_attribs = g_new(char *, dds_device_sr_attribs_count);

	for (i = 0; i < total_dds_chn_count; i++) {
		unsigned n = i * CHANNEL_ATTRIB_COUNT;

		char * chn_name = g_strdup_printf(DDS_DEVICE".out_altvoltage%i_TX%i_%c_F%i",
			i, (i / 4) + 1, (i & 0x02) ? 'Q' : 'I', (i % 2) + 1);

		dds_device_sr_attribs[n + 0] = g_strconcat(chn_name, "_frequency", NULL);
		dds_device_sr_attribs[n + 1] = g_strconcat(chn_name, "_phase", NULL);
		dds_device_sr_attribs[n + 2] = g_strconcat(chn_name, "_raw", NULL);
		dds_device_sr_attribs[n + 3] = g_strconcat(chn_name, "_scale", NULL);
	}
}

static void destroy_dds_sr_attribs_list(void)
{
	guint i = 0;
	for (; i < dds_device_sr_attribs_count; i++) {
		g_free(dds_device_sr_attribs[i]);
	}
	g_free(dds_device_sr_attribs);
	dds_device_sr_attribs = NULL;
	dds_device_sr_attribs_count = 0;
}

static void multichip_sync()
{
	struct iio_device *hmc7004_dev;

	if (plugin_single_device_mode)
		return;

	hmc7004_dev = iio_context_find_device(ctx, "hmc7044");

	if (hmc7004_dev) {
		/* Turn off continous SYSREF, and enable GPI SYSREF request */
		iio_device_reg_write(hmc7004_dev, 0x5a, 0);
	}

	guint i = 0;
	for (; i <= 11; i++) {
		guint n = 0;
		for (; n < phy_devs_count; n++) {
			iio_device_attr_write_longlong(subcomponents[n].iio_dev, "multichip_sync", i);
		}
	}
}

static void update_label_from(GtkWidget *label, struct iio_device *dev, const char *channel,
                              const char *attribute, bool output, const char *unit, int scale)
{
	char buf[80];
	long long val = 0;
	struct iio_channel *ch;
	int ret = -1;

	ch = iio_device_find_channel(dev, channel, output);
	if (ch) {
		ret = iio_channel_attr_read_longlong(ch, attribute, &val);

		if (scale == 1)
			snprintf(buf, sizeof(buf), "%lld %s", val, unit);
		else if (scale > 0 && scale <= 10)
			snprintf(buf, sizeof(buf), "%.1f %s", (float)val / scale, unit);
		else if (scale > 10)
			snprintf(buf, sizeof(buf), "%.2f %s", (float)val / scale, unit);
		else if (scale > 100)
			snprintf(buf, sizeof(buf), "%.3f %s", (float)val / scale, unit);
	}

	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(label), buf);
	else
		gtk_label_set_text(GTK_LABEL(label), "<error>");
}

static void trigger_advanced_plugin_reload(void)
{
	struct osc_plugin *plugin;
	GSList *node;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;

		if (plugin && (!strncmp(plugin->name, "ADRV9009 Advanced", 17))) {
			if (plugin->handle_external_request) {
				plugin->handle_external_request(plugin, "RELOAD");
			}
		}
	}
}

int load_tal_profile(const char *file_name,
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

		ret = INT_MAX;
		guint i = 0;
		for (; i < phy_devs_count; i++) {
			ret2 = iio_device_attr_write_raw(subcomponents[i].iio_dev, "profile_config", buf, len);
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
	trigger_advanced_plugin_reload();

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
	struct iio_channel *ch;
	guint i = 0;

	/* Get ensm_mode from all devices. Notify user if any of devices has a different mode than the others. */
	for (; i < phy_devs_count; i++) {
		ret = iio_device_attr_read(subcomponents[i].iio_dev, "ensm_mode", buf, sizeof(buf));
		if (ret > 0) {
			if (i > 0) {
				if (strncmp(buf, gtk_label_get_text(GTK_LABEL(ensm_mode)), sizeof(buf))) {
					gtk_label_set_text(GTK_LABEL(ensm_mode), "<not synced>");
					break;
				}
			} else {
				gtk_label_set_text(GTK_LABEL(ensm_mode), buf);
			}
		} else {
			gtk_label_set_text(GTK_LABEL(ensm_mode), "<error>");
			break;
		}
	}

	for (i = 0; i < phy_devs_count; i++) {
		ch = iio_device_find_channel(subcomponents[i].iio_dev, "voltage0", false);
		if (ch) {
			ret = iio_channel_attr_read(ch, "gain_control_mode", buf, sizeof(buf));
		} else {
			ret = 0;
		}

		if (ret > 0)
			gtk_label_set_text(GTK_LABEL(subcomponents[i].rx_gain_control_rx1), buf);
		else
			gtk_label_set_text(GTK_LABEL(subcomponents[i].rx_gain_control_rx1), "<error>");

		ch = iio_device_find_channel(subcomponents[i].iio_dev, "voltage1", false);
		if (ch) {
			ret = iio_channel_attr_read(ch, "gain_control_mode", buf, sizeof(buf));
		} else {
			ret = 0;
		}

		if (ret > 0)
			gtk_label_set_text(GTK_LABEL(subcomponents[i].rx_gain_control_rx2), buf);
		else
			gtk_label_set_text(GTK_LABEL(subcomponents[i].rx_gain_control_rx2), "<error>");

		update_label_from(subcomponents[i].label_rf_bandwidth_rx,
			subcomponents[i].iio_dev,"voltage0", "rf_bandwidth", false, "MHz", 1000000);
		update_label_from(subcomponents[i].label_rf_bandwidth_obs,
			subcomponents[i].iio_dev, "voltage2", "rf_bandwidth", false, "MHz", 1000000);
		update_label_from(subcomponents[i].label_rf_bandwidth_tx,
			subcomponents[i].iio_dev, "voltage0", "rf_bandwidth", true, "MHz", 1000000);

		update_label_from(subcomponents[i].label_sampling_freq_rx,
			subcomponents[i].iio_dev, "voltage0", "sampling_frequency", false, "MSPS", 1000000);
		update_label_from(subcomponents[i].label_sampling_freq_obs,
			subcomponents[i].iio_dev, "voltage2", "sampling_frequency", false, "MSPS", 1000000);
		update_label_from(subcomponents[i].label_sampling_freq_tx,
			subcomponents[i].iio_dev, "voltage0", "sampling_frequency", true, "MSPS", 1000000);

		if (subcomponents[i].rx1_gain) {
			iio_widget_update(&subcomponents[i].rx_widgets[subcomponents[i].rx1_gain]);
		}

		if (subcomponents[i].rx2_gain) {
			iio_widget_update(&subcomponents[i].rx_widgets[subcomponents[i].rx2_gain]);
		}

		if (subcomponents[i].obs_gain) {
			iio_widget_update(&subcomponents[i].obsrx_widgets[subcomponents[i].obs_gain]);
		}
	}
}

static void set_ensm_mode_of_all_devices(const char *mode)
{
	guint i = 0;

	for (; i < phy_devs_count; i++) {
		iio_device_attr_write_raw(subcomponents[i].iio_dev, "ensm_mode", mode, strlen(mode));
	}
}

static void on_ensm_mode_available_changed(void)
{
	gchar *mode = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(ensm_mode_available));
	if (!mode)
		return;

	/* Sync all devices to the same ensm_mode */
	if (!plugin_single_device_mode)
		set_ensm_mode_of_all_devices(mode);

	glb_settings_update_labels();
}

static void rx_freq_info_update(void)
{
	double lo_freq = 0;

	if (cap) {
		rx_update_device_sampling_freq(CAP_DEVICE,
		                               USE_INTERN_SAMPLING_FREQ);
		lo_freq = mhz_scale * gtk_spin_button_get_value(
				GTK_SPIN_BUTTON(subcomponents[0].glb_widgets[subcomponents[0].trx_lo].widget));

		rx_update_channel_lo_freq(CAP_DEVICE, "all", lo_freq);
	}

	if (cap_obs) {
		const char *source;

		rx_update_device_sampling_freq(CAP_DEVICE_2,
					USE_INTERN_SAMPLING_FREQ);

		guint i = 0;
		for (; i < phy_devs_count; i++) {
			source = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(subcomponents[i].obs_port_select));

			if (source && strstr(source, "TX")) {
				lo_freq = mhz_scale * gtk_spin_button_get_value(
					GTK_SPIN_BUTTON(subcomponents[i].glb_widgets[subcomponents[i].trx_lo].widget));
			} else {
				lo_freq = mhz_scale * gtk_spin_button_get_value(
					GTK_SPIN_BUTTON(subcomponents[i].obsrx_widgets[subcomponents[i].aux_lo].widget));
			}
		}

		// TO DO: figure out what to do here. Do we set each group of channels with corresponding LO frequency?
		rx_update_channel_lo_freq(CAP_DEVICE_2, "all", lo_freq);
	}
}

static void sample_frequency_changed_cb(void *data)
{
	glb_settings_update_labels();
	rx_freq_info_update();
}

static void int_dec_freq_update(void)
{
	struct iio_channel *ch;
	double freq;
	gchar *text;

	ch = iio_device_find_channel(cap, "voltage0_i", false);
	iio_channel_attr_read_double(ch, "sampling_frequency", &freq);
	text = g_strdup_printf ("%f", freq / mhz_scale);

	guint i = 0;
	for (; i < phy_devs_count; i++)
		gtk_label_set_text(GTK_LABEL(subcomponents[i].label_sampling_freq_rx), text);
	g_free(text);

	ch = iio_device_find_channel(dds, "voltage0", true);
	text = g_strdup_printf ("%f", freq / mhz_scale);
	iio_channel_attr_read_double(ch, "sampling_frequency", &freq);

	for (i = 0; i < phy_devs_count; i++)
		gtk_label_set_text(GTK_LABEL(subcomponents[i].label_sampling_freq_tx), text);
	g_free(text);
}

static void int_dec_update_cb(GtkComboBox *cmb, gpointer data)
{
	int_dec_freq_update();
	rx_freq_info_update();
}

static void rssi_update_label(GtkWidget *label, struct iio_channel *ch)
{
	char buf[1024];
	int ret;

	/* don't update if it is hidden (to quiet down SPI) */
	if (!gtk_widget_is_drawable(GTK_WIDGET(label)))
		return;

	ret = iio_channel_attr_read(ch, "rssi", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(label), buf);
	else
		gtk_label_set_text(GTK_LABEL(label), "<error>");
}

static void rssi_update_labels(void)
{
	guint i = 0;
	for (; i < phy_devs_count; i++) {
		rssi_update_label(subcomponents[i].rx1_rssi, subcomponents[i].ch0);
		rssi_update_label(subcomponents[i].rx2_rssi, subcomponents[i].ch1);
		/*rssi_update_label(subcomponents[i].obs_rssi, subcomponents[i].ch2);*/
	}
}

static gboolean update_display(gpointer foo)
{
	if (this_page == gtk_notebook_get_current_page(nbook) || plugin_detached) {
		const char *gain_mode;
		guint i = 0;

		rssi_update_labels();
		
		for (; i < phy_devs_count; i++) {
			gain_mode = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(subcomponents[i].rx_gain_control_modes_rx1));

			if (gain_mode && strcmp(gain_mode, "manual")) {
				iio_widget_update(&subcomponents[i].rx_widgets[subcomponents[i].rx1_gain]);
				iio_widget_update(&subcomponents[i].rx_widgets[subcomponents[i].rx2_gain]);
			}
		}
	}

	return TRUE;
}

static void rx_phase_rotation_update()
{
	gdouble val[4];
	int iq_cnt = 2; /* two channel types: I, Q */
	int cap_chn_count = 4; /* number of input channel for a single capture device */
	guint i;
	unsigned int n;

	if (!cap)
		return;

	// Get all I/Q channels
	GArray *out = g_array_new(FALSE, FALSE, sizeof(struct iio_channel *));
	for (n = 0; n < iio_device_get_channels_count(cap); n++) {
		struct iio_channel *ch = iio_device_get_channel(cap, n);

		if (!iio_channel_is_output(ch) && iio_channel_is_scan_element(ch))
			g_array_append_val(out, ch);
	}

	for (i = 0; i < out->len - 1; i += iq_cnt) {
		struct iio_channel *i_chn = g_array_index(out, struct iio_channel*, i);
		struct iio_channel *q_chn = g_array_index(out, struct iio_channel*, i + 1);

		iio_channel_attr_read_double(i_chn, "calibscale", &val[0]);
		iio_channel_attr_read_double(i_chn, "calibphase", &val[1]);
		iio_channel_attr_read_double(q_chn, "calibscale", &val[2]);
		iio_channel_attr_read_double(q_chn, "calibphase", &val[3]);

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
			printf("error calculating phase rotations for device %s\n",
				iio_device_get_id(subcomponents[i / cap_chn_count].iio_dev));
			val[0] = 0.0;
		} else
			val[0] = (val[0] + val[1] + val[2] + val[3]) / 4.0;

		gtk_spin_button_set_value(GTK_SPIN_BUTTON(subcomponents[i / cap_chn_count].rx_phase_rotation[(i % cap_chn_count) / iq_cnt]), val[0]);
	}

	g_array_free(out, FALSE);
}

static void update_widgets(void)
{
	guint i = 0;

	for (; i < phy_devs_count; i++) {
		iio_update_widgets_of_device(subcomponents[i].widgets, subcomponents[i].num_glb +
			subcomponents[i].num_tx + subcomponents[i].num_rx + subcomponents[i].num_obsrx, subcomponents[i].iio_dev);
	}

	for (i = 0; i < num_fpga; i++)
		iio_widget_update(&fpga_widgets[i]);

	if (!plugin_single_device_mode) {
		iio_widget_update(&iio_ensm_mode_available);
	}

	if (dds)
		iio_update_widgets_of_device(subcomponents[0].widgets, subcomponents[0].num_glb + subcomponents[0].num_tx +
			subcomponents[0].num_rx + subcomponents[0].num_obsrx, dds);

	dac_data_manager_update_iio_widgets(dac_tx_manager);
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

static void profile_config_file_set_cb(GtkFileChooser *chooser, gpointer data)
{
	char *file_name = gtk_file_chooser_get_filename(chooser);

	load_tal_profile(file_name, adrv9009_panel, chooser, last_profile);
}

static int compare_gain(const char *a, const char *b) __attribute__((unused));
static int compare_gain(const char *a, const char *b)
{
	double val_a, val_b;
	sscanf(a, "%lf", &val_a);
	sscanf(b, "%lf", &val_b);

	if (val_a < val_b)
		return -1;
	else if (val_a > val_b)
		return 1;
	else
		return 0;
}

static double get_gui_tx_sampling_freq(void)
{
	return gtk_spin_button_get_value(GTK_SPIN_BUTTON(subcomponents[0].tx_widgets[subcomponents[0].tx_sample_freq].widget));
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
		iio_channel_attr_write_double(out0, "calibphase", (double)(-1 * sin(phase)));
		iio_channel_attr_write_double(out1, "calibscale", (double) cos(phase));
		iio_channel_attr_write_double(out1, "calibphase", (double) sin(phase));
	}
}

void mcs_sync_button_clicked(GtkButton *btn, gpointer data)
{
	multichip_sync();
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
		else if (GTK_IS_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "clicked");
		else
			printf("unhandled widget type, attribute: %s (%u)\n", widgets[i].attr_name, i);

		if (GTK_IS_SPIN_BUTTON(widgets[i].widget) &&
		    widgets[i].priv_progress != NULL) {
			iio_spin_button_progress_activate(&widgets[i]);
		} else {
			g_signal_connect(G_OBJECT(widgets[i].widget), signal_name, G_CALLBACK(save_widget_value),
			                 &widgets[i]);
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

static int adrv9009_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	int ret = 0;

	if (MATCH_ATTRIB("load_tal_profile_file")) {
		if (value[0]) {
			load_tal_profile(value, adrv9009_panel,
			                 GTK_FILE_CHOOSER(profile_config),
			                 last_profile);
		}
	} else if (MATCH_ATTRIB("ensm_mode")) {
		if (!plugin_single_device_mode) {
			set_ensm_mode_of_all_devices(value);
		}
	} else if (!strncmp(attrib, "dds_mode_tx", sizeof("dds_mode_tx") - 1)) {
		int tx = atoi(attrib + sizeof("dds_mode_tx") - 1);
		dac_data_manager_set_dds_mode(dac_tx_manager, DDS_DEVICE, tx, atoi(value));
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

static int adrv9009_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value, adrv9009_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	char *value;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(adrv9009_driver_attribs); i++) {
		value = read_token_from_ini(ini_fn, THIS_DRIVER, adrv9009_driver_attribs[i]);

		if (value) {
			adrv9009_handle_driver(NULL,
				adrv9009_driver_attribs[i], value);
			free(value);
		}
	}

	/* The gain_control_mode iio attribute should be set prior to setting
	 * hardwaregain iio attribute. This is neccessary due to the fact that
	 * some control modes change the hardwaregain automatically. */
	for (i = 0; i < phy_devs_count; i++){
		struct iio_device *dev = subcomponents[i].iio_dev;
		const char *dev_name  = iio_device_get_name(subcomponents[i].iio_dev);
		struct iio_channel *ch;
		char *attrib_name;

		ch = iio_device_find_channel(dev, "voltage0", false);
		attrib_name = g_strconcat(dev_name, ".in_voltage0_gain_control_mode", NULL);
		value = read_token_from_ini(ini_fn, THIS_DRIVER, attrib_name);
		g_free(attrib_name);

		if (ch && value) {
			iio_channel_attr_write(ch, "gain_control_mode", value);
			free(value);
		}

		ch = iio_device_find_channel(dev, "voltage1", false);
		attrib_name = g_strconcat(dev_name, ".in_voltage1_gain_control_mode", NULL);
		value = read_token_from_ini(ini_fn, THIS_DRIVER, attrib_name);
		g_free(attrib_name);

		if (ch && value) {
			iio_channel_attr_write(ch, "gain_control_mode", value);
			free(value);
		}

		update_from_ini(ini_fn, THIS_DRIVER, subcomponents[i].iio_dev, (const char * const*)subcomponents[i].sr_attribs,
					subcomponents[i].sr_attribs_count);
	}

	if (dds)
		update_from_ini(ini_fn, THIS_DRIVER, dds, (const char * const*)dds_device_sr_attribs,
						dds_device_sr_attribs_count);

	if (can_update_widgets)
		reload_button_clicked(NULL, NULL);

	multichip_sync();
}

/* Constructs a notebook with a page for each plugin subcomponent
 * (which corresponds to a adrv9009-phy device) and makes the notebook a child
 * of the given container.
 */
void buildTabsInContainer(GtkBox *container_box, enum plugin_section section, bool child_expand, bool child_fill)
{
	guint i;
	GtkNotebook *notebook = GTK_NOTEBOOK(gtk_notebook_new());

	/* Create notebook pages */
	for (i = 0; i < phy_devs_count; i++) {
		struct iio_device *dev = subcomponents[i].iio_dev;
		GtkWidget *page_label = gtk_label_new(iio_device_get_name(dev) ?: iio_device_get_id(dev));

		GtkWidget *page_container = gtk_vbox_new(FALSE, 0);
		gtk_notebook_append_page(notebook, page_container, page_label);
		GtkWidget *page = gtk_notebook_get_nth_page(notebook, i);
		gtk_widget_show(page);

		GtkWidget *content_widget = subcomponents[i].section_containers[section];
		if (!gtk_widget_get_parent(content_widget)) {
			gtk_box_pack_start(GTK_BOX(page_container), content_widget, FALSE, TRUE, 0);
		} else {
			gtk_widget_reparent(content_widget, page_container);
		}
	}

	gtk_box_pack_start(container_box, GTK_WIDGET(notebook), child_expand, child_fill, 0);
	gtk_widget_show(GTK_WIDGET(notebook));
}

static GtkWidget *adrv9009_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder = NULL;
	GtkWidget *dds_container;
	const char *freq_name;
	struct iio_channel *ch;

	can_update_widgets = false;

	ctx = osc_create_context();

	if (!ctx)
		return NULL;

	dds = iio_context_find_device(ctx, DDS_DEVICE);
	cap = iio_context_find_device(ctx, CAP_DEVICE);
	cap_obs = iio_context_find_device(ctx, CAP_DEVICE_2);

	builder = gtk_builder_new();
	if (osc_load_glade_file(builder, "adrv9009") < 0)
		return NULL;

	/* Are there more adrv9009-phy devices? */
	GArray *phy_adrv9009_devs = get_iio_devices_starting_with(ctx, PHY_DEVICE);
	phy_devs_count = phy_adrv9009_devs->len;
	plugin_single_device_mode = phy_devs_count == 1;

	/* Build list of DDS attributes */
	build_dds_sr_attribs_list(phy_devs_count);

	/* Make a data structure for each adrv9009-phy device found */
	subcomponents = g_new(struct plugin_subcomponent, phy_devs_count);
	guint i;
	for (i = 0; i < phy_devs_count; i++) {
		struct iio_device *dev = g_array_index(phy_adrv9009_devs, struct iio_device*, i);

		subcomponents[i].iio_dev = dev;
		subcomponents[i].ch0 = iio_device_find_channel(dev, "voltage0", false); /* RX1 */
		subcomponents[i].ch1 = iio_device_find_channel(dev, "voltage1", false); /* RX2 */
		subcomponents[i].ch2 = iio_device_find_channel(dev, "voltage2", false); /* OBS-RX1 */
		subcomponents[i].ch3 = iio_device_find_channel(dev, "voltage3", false); /* OBS-RX1 */
		subcomponents[i].alt_ch0 = iio_device_find_channel(dev, "altvoltage0", true);
		subcomponents[i].alt_ch1 = iio_device_find_channel(dev, "altvoltage1", true);
		subcomponents[i].out_ch0 = iio_device_find_channel(dev, "voltage0", true); /* TX1 */
		subcomponents[i].out_ch1 = iio_device_find_channel(dev, "voltage1", true); /* TX2 */

		if (i == 0) {
			subcomponents[i].builder = builder;
		} else {
			subcomponents[i].builder = gtk_builder_new();
		}

		subcomponents[i].num_glb = 0;
		subcomponents[i].num_tx = 0;
		subcomponents[i].num_rx = 0;
		subcomponents[i].num_obsrx = 0;
		subcomponents[i].rx1_gain = 0;
		subcomponents[i].rx2_gain = 0;
		subcomponents[i].obs_gain = 0;
		subcomponents[i].trx_lo = 0;
		subcomponents[i].aux_lo = 0;
		subcomponents[i].rx_sample_freq = 0;
		subcomponents[i].tx_sample_freq = 0;

		subcomponents[i].sr_attribs_count = ARRAY_SIZE(adrv9009_sr_attribs);
		subcomponents[i].sr_attribs = g_new(char *, subcomponents[i].sr_attribs_count);
		size_t n = 0;
		for (; n < subcomponents[i].sr_attribs_count; n++)
		{
			subcomponents[i].sr_attribs[n] = g_strconcat(
				iio_device_get_name(subcomponents[i].iio_dev), adrv9009_sr_attribs[n], NULL);
		}
	}

	if (dds) {
		dac_tx_manager = dac_data_manager_new(dds, NULL, ctx);
		dac_data_manager_set_buffer_size_alignment(dac_tx_manager, 16);
	}

	/* Extract UI objects for each subcomponent */
	gchar *ui_object_ids[] = {
		"sampling_freq_rx",
		"adjustment_sampl_freq_rx",
		"sampling_freq_tx",
		"adjustment_sampl_freq_tx",
		"sampling_freq_obs",
		"adjustment_sampl_freq_obs",
		"adjustment_tx_lo_freq",
		"global_settings_container",
		"adjustment_hw_gain_rx1",
		"adjustment_hw_gain_rx2",
		"boxReceive",
		"adjustment_hw_gain_tx1",
		"adjustment_hw_gain_tx2",
		"boxTransmit",
		"adjustment_sn_lo_freq",
		"adjustment_hw_gain_obs",
		"adjustment_hw_gain_obs2",
		"box_receive_settings_obs",
		"adjust_rx1_phase",
		"adjust_rx2_phase",
		"box_fpga_receive",
		NULL
	};

	for (i = 0; i < phy_devs_count; i++) {
		if (i > 0) {
			if (osc_load_objects_from_glade_file(subcomponents[i].builder, "adrv9009", ui_object_ids)) {
				fprintf(stderr, "Error, could not add objects from adrv9009 glade file\n");
				return FALSE;
			}
		}
		subcomponents[i].section_containers[SECTION_GLOBAL] =
			GTK_WIDGET(gtk_builder_get_object(subcomponents[i].builder, "global_settings_container"));
		subcomponents[i].section_containers[SECTION_RX] =
			GTK_WIDGET(gtk_builder_get_object(subcomponents[i].builder, "boxReceive"));
		subcomponents[i].section_containers[SECTION_TX] =
			GTK_WIDGET(gtk_builder_get_object(subcomponents[i].builder, "boxTransmit"));
		subcomponents[i].section_containers[SECTION_OBS] =
			GTK_WIDGET(gtk_builder_get_object(subcomponents[i].builder, "boxReceiveObs"));
		subcomponents[i].section_containers[SECTION_FPGA] =
			GTK_WIDGET(gtk_builder_get_object(subcomponents[i].builder, "boxFpgaReceive"));
	}

	/* Keep references to widgets for each subcomponent */
	for (i = 0; i < phy_devs_count; i++) {
		GtkBuilder *builder = subcomponents[i].builder;

		/* Receive Chain */
		subcomponents[i].rx_gain_control_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx1"));
		subcomponents[i].rx_gain_control_rx2 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx2"));
		subcomponents[i].rx_gain_control_modes_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx1"));
		subcomponents[i].rx1_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx1"));
		subcomponents[i].rx2_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx2"));
		subcomponents[i].label_rf_bandwidth_rx = GTK_WIDGET(gtk_builder_get_object(builder, "label_rf_bandwidth_rx"));
		subcomponents[i].label_sampling_freq_rx = GTK_WIDGET(gtk_builder_get_object(builder, "label_sampling_freq_rx"));

		/* Transmit Chain */
		subcomponents[i].label_rf_bandwidth_tx = GTK_WIDGET(gtk_builder_get_object(builder, "label_rf_bandwidth_tx"));
		subcomponents[i].label_sampling_freq_tx = GTK_WIDGET(gtk_builder_get_object(builder, "label_sampling_freq_tx"));

		/* Observation Receive Chain */
		subcomponents[i].obs_port_select = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_obs"));
		subcomponents[i].label_rf_bandwidth_obs = GTK_WIDGET(gtk_builder_get_object(builder, "label_rf_bandwidth_obs"));
		subcomponents[i].label_sampling_freq_obs = GTK_WIDGET(gtk_builder_get_object(builder, "label_sampling_freq_obs"));

		/* FPGA */
		subcomponents[i].rx_phase_rotation[0] = GTK_WIDGET(gtk_builder_get_object(builder, "rx1_phase_rotation"));
		subcomponents[i].rx_phase_rotation[1] = GTK_WIDGET(gtk_builder_get_object(builder, "rx2_phase_rotation"));
	}

	/* Configure/load UI that is shared among all subcomponents */
	nbook = GTK_NOTEBOOK(notebook);
	adrv9009_panel = GTK_WIDGET(gtk_builder_get_object(builder, "adrv9009_panel"));

	/* Create tabs when in multiple-device mode */
	if (!plugin_single_device_mode) {
		buildTabsInContainer(GTK_BOX(gtk_builder_get_object(builder, "boxGlobalSettings")),
							 SECTION_GLOBAL, FALSE, TRUE);
		buildTabsInContainer(GTK_BOX(gtk_builder_get_object(builder, "box_receive_settings")),
							 SECTION_RX, FALSE, TRUE);
		buildTabsInContainer(GTK_BOX(gtk_builder_get_object(builder, "box_transmit_settings")),
							 SECTION_TX, FALSE, TRUE);
		buildTabsInContainer(GTK_BOX(gtk_builder_get_object(builder, "box_receive_settings_obs")),
							 SECTION_OBS, FALSE, TRUE);
		buildTabsInContainer(GTK_BOX(gtk_builder_get_object(builder, "box_fpga_receive")),
							 SECTION_FPGA, FALSE, TRUE);
	}

	/* Global settings */

	profile_config = GTK_WIDGET(gtk_builder_get_object(builder, "profile_config"));
	ensm_mode = GTK_WIDGET(gtk_builder_get_object(builder, "ensm_mode"));
	ensm_mode_available = GTK_WIDGET(gtk_builder_get_object(builder, "ensm_mode_available"));
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

	gtk_combo_box_set_active(GTK_COMBO_BOX(ensm_mode_available), 0);
	
	for (i = 0; i < phy_devs_count; i++) {
		gtk_combo_box_set_active(GTK_COMBO_BOX(subcomponents[i].rx_gain_control_modes_rx1), 0);
	}

	/* FPGA settings */

	dds_container = GTK_WIDGET(gtk_builder_get_object(builder, "dds_transmit_block"));
	if (dac_tx_manager)
		gtk_container_add(GTK_CONTAINER(dds_container), dac_data_manager_get_gui_container(dac_tx_manager));
	gtk_widget_show_all(dds_container);

	fpga_rx_frequency_available = GTK_WIDGET(gtk_builder_get_object(builder, "fpga_rx_frequency_available"));
	fpga_tx_frequency_available = GTK_WIDGET(gtk_builder_get_object(builder, "fpga_tx_frequency_available"));

	gtk_combo_box_set_active(GTK_COMBO_BOX(fpga_rx_frequency_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(fpga_tx_frequency_available), 0);

	ch = iio_device_find_channel(cap, "voltage0_i", false);
	if (iio_channel_find_attr(ch, "sampling_frequency_available")) {
		iio_combo_box_init(&fpga_widgets[num_fpga++],
						   cap, ch, "sampling_frequency",
						   "sampling_frequency_available",
						   fpga_rx_frequency_available, NULL);
	} else {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder,
								  "receive_frame_dma_buf")));
	}

	ch = iio_device_find_channel(dds, "voltage0", true);
	if (iio_channel_find_attr(ch, "sampling_frequency_available")) {
		iio_combo_box_init(&fpga_widgets[num_fpga++],
						   dds, ch, "sampling_frequency",
						   "sampling_frequency_available",
						   fpga_tx_frequency_available, NULL);
	} else {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder,
								  "transmit_frame_dma_buf")));
	}

	/* Transmit settings */

	GtkWidget *sfreq = GTK_WIDGET(gtk_builder_get_object(builder, "sampling_freq_tx"));
	GtkAdjustment *sfreq_adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(sfreq));
	gtk_adjustment_set_upper(sfreq_adj, 307.20); // are these 3 lines necessary?

	/* Bind the IIO device files to the GUI widgets */

	/* Treat 'ensm_mode_available' separately because it will be shared between devices (when more are avaialable) */
	if (!plugin_single_device_mode) {
		iio_combo_box_init(&iio_ensm_mode_available, subcomponents[0].iio_dev, NULL,
				"ensm_mode", "ensm_mode_available", ensm_mode_available, NULL);
	}

	for (i = 0; i < phy_devs_count; i++) {
		GtkBuilder *builder = subcomponents[i].builder;

		subcomponents[i].glb_widgets = subcomponents[i].widgets;

		if (plugin_single_device_mode) {
			iio_combo_box_init(&subcomponents[0].glb_widgets[subcomponents[0].num_glb++],
					subcomponents[0].iio_dev, NULL, "ensm_mode", "ensm_mode_available",
					ensm_mode_available, NULL);
		}

		/* Global settings */

		iio_toggle_button_init_from_builder(&subcomponents[i].glb_widgets[subcomponents[i].num_glb++],
		                                    subcomponents[i].iio_dev, NULL, "calibrate_rx_qec_en", builder,
		                                    "calibrate_rx_qec_en", 0);

		iio_toggle_button_init_from_builder(&subcomponents[i].glb_widgets[subcomponents[i].num_glb++],
		                                    subcomponents[i].iio_dev, NULL, "calibrate_tx_qec_en", builder,
		                                    "calibrate_tx_qec_en", 0);

		iio_toggle_button_init_from_builder(&subcomponents[i].glb_widgets[subcomponents[i].num_glb++],
		                                    subcomponents[i].iio_dev, NULL, "calibrate_tx_lol_en", builder,
		                                    "calibrate_tx_lol_en", 0);

		iio_toggle_button_init_from_builder(&subcomponents[i].glb_widgets[subcomponents[i].num_glb++],
		                                    subcomponents[i].iio_dev, NULL, "calibrate_tx_lol_ext_en", builder,
		                                    "calibrate_tx_lol_ext_en", 0);

		iio_toggle_button_init_from_builder(&subcomponents[i].glb_widgets[subcomponents[i].num_glb++],
		                                    subcomponents[i].iio_dev, NULL, "calibrate_rx_phase_correction_en", builder,
		                                    "calibrate_rx_phase_correction_en", 0);

		iio_toggle_button_init_from_builder(&subcomponents[i].glb_widgets[subcomponents[i].num_glb++],
		                                    subcomponents[i].iio_dev, NULL, "calibrate_fhm_en", builder,
		                                    "calibrate_fhm_en", 0);

		iio_button_init_from_builder(&subcomponents[i].glb_widgets[subcomponents[i].num_glb++],
		                             subcomponents[i].iio_dev, NULL, "calibrate", builder,
		                             "calibrate");

		subcomponents[i].trx_lo = subcomponents[i].num_glb;

		if (iio_channel_find_attr(subcomponents[i].alt_ch0, "frequency"))
			freq_name = "frequency";
		else
			freq_name = "TRX_LO_frequency";

		iio_spin_button_s64_init_from_builder(&subcomponents[i].glb_widgets[subcomponents[i].num_glb++],
						      subcomponents[i].iio_dev, subcomponents[i].alt_ch0,
						      freq_name, builder, "tx_lo_freq", &mhz_scale);
		iio_spin_button_add_progress(&subcomponents[i].glb_widgets[subcomponents[i].num_glb - 1]);

		iio_toggle_button_init_from_builder(&subcomponents[i].glb_widgets[subcomponents[i].num_glb++],
						    subcomponents[i].iio_dev, subcomponents[i].alt_ch0,
						    "frequency_hopping_mode_enable", builder, "fhm_enable", 0);

		subcomponents[i].rx_widgets = &subcomponents[i].glb_widgets[subcomponents[i].num_glb];

		/* Receive Chain */
		if (subcomponents[i].ch0 && subcomponents[i].ch1) {
			iio_combo_box_init(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
					subcomponents[i].iio_dev, subcomponents[i].ch0, "gain_control_mode",
					"gain_control_mode_available",
					subcomponents[i].rx_gain_control_modes_rx1, NULL);

			subcomponents[i].rx1_gain = subcomponents[i].num_rx;
			iio_spin_button_init_from_builder(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
							subcomponents[i].iio_dev, subcomponents[i].ch0, "hardwaregain", builder,
							"hardware_gain_rx1", NULL);

			subcomponents[i].rx2_gain = subcomponents[i].num_rx;
			iio_spin_button_init_from_builder(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
							subcomponents[i].iio_dev, subcomponents[i].ch1, "hardwaregain", builder,
							"hardware_gain_rx2", NULL);

			subcomponents[i].rx_sample_freq = subcomponents[i].num_rx;
			iio_spin_button_int_init_from_builder(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
							subcomponents[i].iio_dev, subcomponents[i].ch0, "sampling_frequency", builder,
							"sampling_freq_rx", &mhz_scale);
			iio_spin_button_add_progress(&subcomponents[i].rx_widgets[subcomponents[i].num_rx - 1]);

			iio_toggle_button_init_from_builder(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
							subcomponents[i].iio_dev, subcomponents[i].ch0, "quadrature_tracking_en", builder,
							"rx1_quadrature_tracking_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
							subcomponents[i].iio_dev, subcomponents[i].ch1, "quadrature_tracking_en", builder,
							"rx2_quadrature_tracking_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
							subcomponents[i].iio_dev, subcomponents[i].ch0, "hd2_tracking_en", builder,
							"rx1_hd2_tracking_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
							subcomponents[i].iio_dev, subcomponents[i].ch1, "hd2_tracking_en", builder,
							"rx2_hd2_tracking_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
							subcomponents[i].iio_dev, subcomponents[i].ch0, "gain_control_pin_mode_en", builder,
							"rx1_gain_control_pin_mode_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
							subcomponents[i].iio_dev, subcomponents[i].ch1, "gain_control_pin_mode_en", builder,
							"rx2_gain_control_pin_mode_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
							subcomponents[i].iio_dev, subcomponents[i].ch0, "powerdown", builder,
							"rx1_powerdown_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].rx_widgets[subcomponents[i].num_rx++],
							subcomponents[i].iio_dev, subcomponents[i].ch1, "powerdown", builder,
							"rx2_powerdown_en", 0);

		} else {
			gtk_widget_hide(gtk_widget_get_parent(section_setting[SECTION_RX]));
		}

		/* Observation Receiver Chain */

		subcomponents[i].obsrx_widgets = &subcomponents[i].rx_widgets[subcomponents[i].num_rx];

		if (subcomponents[i].ch2) {
			iio_combo_box_init(&subcomponents[i].obsrx_widgets[subcomponents[i].num_obsrx++],
					subcomponents[i].iio_dev, subcomponents[i].ch2, "rf_port_select",
					"rf_port_select_available",
					subcomponents[i].obs_port_select, NULL);

			subcomponents[i].obs_gain = subcomponents[i].num_obsrx;
			iio_spin_button_init_from_builder(&subcomponents[i].obsrx_widgets[subcomponents[i].num_obsrx++],
							subcomponents[i].iio_dev, subcomponents[i].ch2, "hardwaregain", builder,
							"hardware_gain_obs1", NULL);

			iio_toggle_button_init_from_builder(&subcomponents[i].obsrx_widgets[subcomponents[i].num_obsrx++],
							subcomponents[i].iio_dev, subcomponents[i].ch2, "quadrature_tracking_en", builder,
							"obs1_quadrature_tracking_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].obsrx_widgets[subcomponents[i].num_obsrx++],
							subcomponents[i].iio_dev, subcomponents[i].ch2, "powerdown", builder,
							"obs1_powerdown_en", 0);

			if (subcomponents[i].ch3) {
				iio_spin_button_init_from_builder(&subcomponents[i].obsrx_widgets[subcomponents[i].num_obsrx++],
								subcomponents[i].iio_dev, subcomponents[i].ch3, "hardwaregain", builder,
								"hardware_gain_obs2", NULL);

				iio_toggle_button_init_from_builder(&subcomponents[i].obsrx_widgets[subcomponents[i].num_obsrx++],
								subcomponents[i].iio_dev, subcomponents[i].ch3, "quadrature_tracking_en", builder,
								"obs2_quadrature_tracking_en", 0);

				iio_toggle_button_init_from_builder(&subcomponents[i].obsrx_widgets[subcomponents[i].num_obsrx++],
								subcomponents[i].iio_dev, subcomponents[i].ch3, "powerdown", builder,
								"obs2_powerdown_en", 0);
			}

			subcomponents[i].aux_lo = subcomponents[i].num_obsrx;

			if (iio_channel_find_attr(subcomponents[i].alt_ch1, "frequency"))
				freq_name = "frequency";
			else
				freq_name = "AUX_OBS_RX_LO_frequency";

			iio_spin_button_s64_init_from_builder(&subcomponents[i].obsrx_widgets[subcomponents[i].num_obsrx++],
							subcomponents[i].iio_dev, subcomponents[i].alt_ch1, freq_name, builder,
							"sn_lo_freq", &mhz_scale);
			iio_spin_button_add_progress(&subcomponents[i].obsrx_widgets[subcomponents[i].num_obsrx - 1]);
		} else {
			gtk_widget_hide(gtk_widget_get_parent(section_setting[SECTION_OBS]));
		}

		/* Transmit Chain */

		subcomponents[i].tx_widgets = &subcomponents[i].obsrx_widgets[subcomponents[i].num_obsrx];

		if (subcomponents[i].out_ch0 && subcomponents[i].out_ch1) {
			iio_toggle_button_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch0, "pa_protection_en", builder,
							"pa_protection", 0);

			iio_spin_button_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch0, "hardwaregain", builder,
							"hardware_gain_tx1", &inv_scale);

			iio_spin_button_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch1, "hardwaregain", builder,
							"hardware_gain_tx2", &inv_scale);
			
			subcomponents[i].tx_sample_freq = subcomponents[i].num_tx;
			
			iio_spin_button_int_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch0, "sampling_frequency", builder,
							"sampling_freq_tx", &mhz_scale);
			iio_spin_button_add_progress(&subcomponents[i].tx_widgets[subcomponents[i].num_tx - 1]);

			iio_toggle_button_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch0, "quadrature_tracking_en", builder,
							"tx1_quadrature_tracking_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch1, "quadrature_tracking_en", builder,
							"tx2_quadrature_tracking_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch0, "lo_leakage_tracking_en", builder,
							"tx1_lo_leakage_tracking_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch1, "lo_leakage_tracking_en", builder,
							"tx2_lo_leakage_tracking_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch0, "atten_control_pin_mode_en", builder,
							"tx1_atten_control_pin_mode_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch1, "atten_control_pin_mode_en", builder,
							"tx2_atten_control_pin_mode_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch0, "powerdown", builder,
							"tx1_powerdown_en", 0);

			iio_toggle_button_init_from_builder(&subcomponents[i].tx_widgets[subcomponents[i].num_tx++],
							subcomponents[i].iio_dev, subcomponents[i].out_ch1, "powerdown", builder,
							"tx2_powerdown_en", 0);

		} else {
			gtk_widget_hide(gtk_widget_get_parent(section_setting[SECTION_TX]));
			gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "calibrate_tx_qec_en")));
			gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "calibrate_tx_lol_en")));
			gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "calibrate_tx_lol_ext_en")));
		}
	}

	if (ini_fn)
		load_profile(NULL, ini_fn);

	/* Update all widgets with current values */
	printf("Updating widgets...\n");
	update_widgets();
	rx_freq_info_update();
	printf("Updating FIR filter...\n");
	profile_update();
	glb_settings_update_labels();
	rssi_update_labels();
	if (dds)
	{
		dac_data_manager_freq_widgets_range_update(dac_tx_manager, get_gui_tx_sampling_freq() / 2.0);
		dac_data_manager_update_iio_widgets(dac_tx_manager);
	}
	/* Connect signals */

	g_builder_connect_signal(builder, "rx1_phase_rotation", "value-changed",
	                         G_CALLBACK(rx_phase_rotation_set), (gpointer *)0);

	g_builder_connect_signal(builder, "rx2_phase_rotation", "value-changed",
	                         G_CALLBACK(rx_phase_rotation_set), (gpointer *)2);

	g_builder_connect_signal(builder, "sampling_freq_tx", "value-changed",
	                         G_CALLBACK(tx_sample_rate_changed), NULL);

	g_builder_connect_signal(builder, "adrv9009_settings_reload", "clicked",
	                         G_CALLBACK(reload_button_clicked), NULL);

	g_builder_connect_signal(builder, "mcs_sync", "clicked",
	                         G_CALLBACK(mcs_sync_button_clicked), NULL);

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
	                       G_CALLBACK(on_ensm_mode_available_changed), NULL);

	g_signal_connect_after(fpga_rx_frequency_available, "changed",
			       G_CALLBACK(int_dec_update_cb), NULL);

	g_signal_connect_after(fpga_tx_frequency_available, "changed",
			       G_CALLBACK(int_dec_update_cb), NULL);

	for (i = 0; i < phy_devs_count; i++) {
		g_signal_connect_after(subcomponents[i].rx_gain_control_modes_rx1, "changed",
							G_CALLBACK(glb_settings_update_labels), NULL);
		make_widget_update_signal_based(subcomponents[i].glb_widgets, subcomponents[i].num_glb);
		make_widget_update_signal_based(subcomponents[i].rx_widgets, subcomponents[i].num_rx);
		make_widget_update_signal_based(subcomponents[i].obsrx_widgets, subcomponents[i].num_obsrx);
		make_widget_update_signal_based(subcomponents[i].tx_widgets, subcomponents[i].num_tx);

		if (subcomponents[i].rx_sample_freq)
		{
			iio_spin_button_set_on_complete_function(&subcomponents[i].rx_widgets[subcomponents[i].rx_sample_freq],
				sample_frequency_changed_cb, NULL);
		}
		if (subcomponents[i].tx_sample_freq)
		{
			iio_spin_button_set_on_complete_function(&subcomponents[i].tx_widgets[subcomponents[i].tx_sample_freq],
													sample_frequency_changed_cb, NULL);
		}
		if (subcomponents[i].trx_lo)
		{
			iio_spin_button_set_on_complete_function(&subcomponents[i].glb_widgets[subcomponents[i].trx_lo],
													sample_frequency_changed_cb, NULL);
		}
		if (subcomponents[i].aux_lo)
		{
			iio_spin_button_set_on_complete_function(&subcomponents[i].obsrx_widgets[subcomponents[i].aux_lo],
													sample_frequency_changed_cb, NULL);
		}
	}

	make_widget_update_signal_based(fpga_widgets, num_fpga);

	add_ch_setup_check_fct(CAP_DEVICE, channel_combination_check);
	struct iio_device *adc_dev;
	struct extra_dev_info *adc_info;

	adc_dev = iio_context_find_device(get_context_from_osc(), CAP_DEVICE);

	if (adc_dev) {
		adc_info = iio_device_get_data(adc_dev);

		if (adc_info)
			adc_info->plugin_fft_corr = 20 * log10(1/sqrt(HANNING_ENBW));
	}

	/* FIXME: Add later
	 * block_diagram_init(builder, 2, "ADRV9009.svg", "ADRV9009-N_PCBZ.jpg");
	 */
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(profile_config), OSC_FILTER_FILE_PATH);
	dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager, OSC_WAVEFORM_FILE_PATH);

	if (!dac_tx_manager)
		gtk_widget_hide(gtk_widget_get_parent(section_setting[SECTION_FPGA]));

	if (!cap)
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_fpga_rx")));

	if (plugin_single_device_mode)
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "mcs_sync")));

	g_timeout_add(1000, (GSourceFunc) update_display, ctx);
	can_update_widgets = true;

	multichip_sync();

	return adrv9009_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void adrv9009_get_preferred_size(const struct osc_plugin *plugin, int *width, int *height)
{
	if (width)
		*width = 1100;

	if (height)
		*height = 800;
}

static void save_widgets_to_ini(FILE *f)
{
	fprintf(f, "load_tal_profile_file = %s\n"
			   "ensm_mode=%s\n"
			   "dac_buf_filename = %s\n"
			   "global_settings_show = %i\n"
			   "tx_show = %i\n"
			   "rx_show = %i\n"
			   "obs_show = %i\n"
			   "fpga_show = %i\n",
			last_profile,
			(plugin_single_device_mode ? "" : gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(ensm_mode_available))),
			dac_data_manager_get_buffer_chooser_filename(dac_tx_manager),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_GLOBAL]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_TX]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_RX]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_OBS]),
			!!gtk_toggle_tool_button_get_active(section_toggle[SECTION_FPGA])
		);

	/* Save the state of each TX channel */
	if (dds) {
		/* Save state of DDS modes. We know there are 2 TXs for each device. */
		guint d;
		for (d = 0; d < phy_devs_count; d++) {
			fprintf(f, "dds_mode_tx%i=%i\n", (d * 2) + 1, dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE, (d * 2) + 1));
			fprintf(f, "dds_mode_tx%i=%i\n", (d * 2) + 2, dac_data_manager_get_dds_mode(dac_tx_manager, DDS_DEVICE, (d * 2) + 2));
		}

		/* Save state of buffer channels */
		int i = 0, tx_ch_count = device_scan_elements_count(dds);
		for (; i < tx_ch_count; i++) {
			fprintf(f, "tx_channel_%i = %i\n", i, dac_data_manager_get_tx_channel_state(dac_tx_manager, i));
		}
	}
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");

	if (f)
	{
		guint i = 0;

		write_driver_name_to_ini(f, THIS_DRIVER);
		for (; i < phy_devs_count; i++) {
			save_to_ini(f, NULL, subcomponents[i].iio_dev, (const char * const*)subcomponents[i].sr_attribs,
						subcomponents[i].sr_attribs_count);
		}

		if (dds)
			save_to_ini(f, NULL, dds, (const char * const*)dds_device_sr_attribs,
						dds_device_sr_attribs_count);

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

	/* Subcomponents cleanup */
	guint i = 0;
	for (; i < phy_devs_count; i++) {
		size_t n = 0;
		for (; n < subcomponents[i].sr_attribs_count; n++) {
			g_free(subcomponents[i].sr_attribs[n]);
		}
		g_free(subcomponents[i].sr_attribs);
	}
	g_free(subcomponents);

	osc_destroy_context(ctx);

	destroy_dds_sr_attribs_list();
}

struct osc_plugin plugin;

static bool adrv9009_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	return !!iio_context_find_device(osc_ctx, PHY_DEVICE);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = adrv9009_identify,
	.init = adrv9009_init,
	.handle_item = adrv9009_handle,
	.handle_external_request = handle_external_request,
	.update_active_page = update_active_page,
	.get_preferred_size = adrv9009_get_preferred_size,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
