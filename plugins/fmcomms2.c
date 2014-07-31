/**
 * Copyright (C) 2012-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <glib/gthread.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <malloc.h>
#include <values.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/utsname.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "./block_diagram.h"
#include "./datafile_in.h"

#define HANNING_ENBW 1.50

#ifndef SLAVE
#define PHY_DEVICE "ad9361-phy"
#define DDS_DEVICE "cf-ad9361-dds-core-lpc"
#define CAP_DEVICE "cf-ad9361-lpc"
#else
#define PHY_DEVICE "ad9361-phy-hpc"
#define DDS_DEVICE "cf-ad9361-dds-core-hpc"
#define CAP_DEVICE "cf-ad9361-hpc"
#endif

extern gfloat plugin_fft_corr;
extern bool dma_valid_selection(unsigned mask, unsigned channel_count);

static bool is_2rx_2tx;
static bool dds_activated, dds_disabled;

static const gdouble mhz_scale = 1000000.0;
static const gdouble abs_mhz_scale = -1000000.0;
static const gdouble khz_scale = 1000.0;
static const gdouble inv_scale = -1.0;
static char *dac_buf_filename = NULL;

static struct iio_buffer *dds_buffer;

static struct iio_widget glb_widgets[50];
static struct iio_widget tx_widgets[50];
static struct iio_widget rx_widgets[50];
static unsigned int rx1_gain, rx2_gain;
static unsigned int num_glb, num_tx, num_rx;
static unsigned int rx_lo, tx_lo;
static unsigned int rx_sample_freq, tx_sample_freq;

static struct iio_context *ctx;
static struct iio_device *dev, *dds, *cap;
static int dds_num_channels;

/* Widgets for Global Settings */
static GtkWidget *ensm_mode;
static GtkWidget *ensm_mode_available;
static GtkWidget *calib_mode;
static GtkWidget *calib_mode_available;
static GtkWidget *trx_rate_governor;
static GtkWidget *trx_rate_governor_available;
static GtkWidget *filter_fir_config;
static GtkWidget *dac_buffer;
#define SECTION_GLOBAL 0
#define SECTION_TX 1
#define SECTION_RX 2
#define SECTION_FPGA 3
static GtkToggleToolButton *section_toggle[4];
static GtkWidget *section_setting[4];

/* Widgets for Receive Settings */
static GtkWidget *rx_gain_control_rx1;
static GtkWidget *rx_gain_control_modes_rx1;
static GtkWidget *rf_port_select_rx;
static GtkWidget *rx_gain_control_rx2;
static GtkWidget *rx_gain_control_modes_rx2;
static GtkWidget *rx1_rssi;
static GtkWidget *rx2_rssi;
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

/* Widgets for Transmitter Settings */
static GtkWidget *tx_channel_list;
static GtkWidget *dds_mode_tx[3];
#define TX_OFF   0
#define TX1_T1_I 1
#define TX1_T2_I 2
#define TX1_T1_Q 3
#define TX1_T2_Q 4
#define TX2_T1_I 5
#define TX2_T2_I 6
#define TX2_T1_Q 7
#define TX2_T2_Q 8

static GtkWidget *dds_freq[9], *dds_scale[9], *dds_phase[9], *dds_freq[9];
static GtkAdjustment *adj_freq[9];

static GtkWidget *channel_I_tx[3], *channel_Q_tx[3];
static GtkWidget *channel_I_tone2_tx[3];
static GtkWidget *dds_I_TX_l[3];

static gulong dds_freq_hid[9], dds_scale_hid[9], dds_phase_hid[9];

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *fmcomms2_panel;
static gboolean plugin_detached;

static char last_fir_filter[PATH_MAX];

static void enable_dds(bool on_off);


#define TX_CHANNEL_NAME 0
#define TX_CHANNEL_ACTIVE 1
#define TX_CHANNEL_REF_INDEX 2

static int tx_enabled_channels_count(GtkTreeView *treeview, unsigned *enabled_mask)
{
	GtkTreeIter iter;
	gboolean enabled;
	int num_enabled = 0;
	int ch_pos = 0;

	GtkTreeModel *model = gtk_tree_view_get_model(treeview);
	gboolean next_iter = gtk_tree_model_get_iter_first(model, &iter);


	if (enabled_mask)
		*enabled_mask = 0;

	while (next_iter) {
		gtk_tree_model_get(model, &iter, TX_CHANNEL_ACTIVE, &enabled, -1);
		if (enabled) {
			num_enabled++;
			if (enabled_mask)
				*enabled_mask |= 1 << ch_pos;
		}
		ch_pos++;
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}

	return num_enabled;
}

static void tx_channels_check_valid_setup(void)
{
	int enabled_channels;
	unsigned mask;

	enabled_channels = tx_enabled_channels_count(GTK_TREE_VIEW(tx_channel_list), &mask);
	if (dma_valid_selection(mask, dds_num_channels) && enabled_channels > 0) {
		gtk_widget_set_sensitive(dac_buffer, true);
		if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[1])) == 4)
			g_signal_emit_by_name(dac_buffer, "file-set", NULL);
	} else {
		gtk_widget_set_sensitive(dac_buffer, false);
	}
}

static void tx_channel_toggled(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer plot)
{
	GtkTreePath* path = gtk_tree_path_new_from_string(pathStr);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean active;
	gint ch_index;

	if (!gtk_cell_renderer_get_sensitive(GTK_CELL_RENDERER(renderer)))
		return;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(tx_channel_list));
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, TX_CHANNEL_ACTIVE, &active, TX_CHANNEL_REF_INDEX, &ch_index, -1);
	active = !active;
	gtk_tree_store_set(GTK_TREE_STORE(model), &iter, TX_CHANNEL_ACTIVE, active, -1);
	gtk_tree_path_free(path);

	struct iio_channel *channel = iio_device_get_channel(dds, ch_index);
	if (active)
		iio_channel_enable(channel);
	else
		iio_channel_disable(channel);

	tx_channels_check_valid_setup();
}

static void tx_channel_list_init(GtkBuilder *builder)
{
	GtkWidget *tx_ch_frame;
	GtkTreeView *treeview = GTK_TREE_VIEW(tx_channel_list);
	GtkTreeStore *treestore;
	GtkTreeIter iter;
	int i;

	tx_ch_frame = GTK_WIDGET(gtk_builder_get_object(builder, "frame_tx_channels"));
	if (strcmp(PHY_DEVICE, "ad9361-phy") != 0) {
		gtk_widget_hide(tx_ch_frame);
		return;
	}
	gtk_widget_show(tx_ch_frame);

	treestore = GTK_TREE_STORE(gtk_tree_view_get_model(treeview));
	for (i = 0; i < iio_device_get_channels_count(dds); i++) {
		struct iio_channel *ch = iio_device_get_channel(dds, i);

		if (!iio_channel_is_scan_element(ch))
			continue;

		gtk_tree_store_append(treestore, &iter, NULL);
		gtk_tree_store_set(treestore, &iter,
				TX_CHANNEL_NAME, iio_channel_get_id(ch),
				TX_CHANNEL_ACTIVE, iio_channel_is_enabled(ch),
				TX_CHANNEL_REF_INDEX, i, -1);
	}

	g_builder_connect_signal(builder, "cellrenderertogglechannel", "toggled",
			G_CALLBACK(tx_channel_toggled), NULL);
	tx_channels_check_valid_setup();
}

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

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

static void sample_frequency_changed_cb(void)
{
	glb_settings_update_labels();
	rx_update_labels();
}

static void rssi_update_labels(void)
{
	char buf[1024];
	int ret;

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev, "voltage0", false),
			"rssi", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(rx1_rssi), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx1_rssi), "<error>");

	if (!is_2rx_2tx)
		return;

	ret = iio_channel_attr_read(
			iio_device_find_channel(dev, "voltage1", false),
			"rssi", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(rx2_rssi), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx2_rssi), "<error>");
}

static void update_display (void *ptr)
{
	const char *gain_mode;

	/* This thread never exists, and just updates the control frame */
	while (1) {
		if (this_page == gtk_notebook_get_current_page(nbook) || plugin_detached) {
			gdk_threads_enter();
			rssi_update_labels();
			gain_mode = gtk_combo_box_get_active_text(GTK_COMBO_BOX(rx_gain_control_modes_rx1));
			if (gain_mode && strcmp(gain_mode, "manual"))
				iio_widget_update(&rx_widgets[rx1_gain]);

			gain_mode = gtk_combo_box_get_active_text(GTK_COMBO_BOX(rx_gain_control_modes_rx2));
			if (is_2rx_2tx && gain_mode && strcmp(gain_mode, "manual"))
				iio_widget_update(&rx_widgets[rx2_gain]);
			gdk_threads_leave();
		}
		sleep(1);
	}
}

void filter_fir_update(void)
{
	bool rx = false, tx = false, rxtx = false;
	struct iio_channel *chn;

	iio_device_attr_read_bool(dev, "in_out_voltage_filter_fir_en", &rxtx);

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
	glb_settings_update_labels();
}

void filter_fir_enable(void)
{
	bool rx, tx, rxtx, disable;

	rx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx));
	tx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (fir_filter_en_tx));
	rxtx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx_tx));
	disable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (disable_all_fir_filters));

	if (rxtx || disable) {
		iio_device_attr_write_bool(dev,
				"in_out_voltage_filter_fir_en", rxtx);
	} else {
		struct iio_channel *chn;
		chn = iio_device_find_channel(dev, "voltage0", true);
		if (chn)
			iio_channel_attr_write_bool(chn, "filter_fir_en", tx);

		chn = iio_device_find_channel(dev, "voltage0", false);
		if (chn)
			iio_channel_attr_write_bool(chn, "filter_fir_en", rx);
	}

	filter_fir_update();
}

static void rx_phase_rotation_update()
{
	struct iio_channel *out[4];
	gdouble val[4];
	int i, d = 0;

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

static void reload_button_clicked(GtkButton *btn, gpointer data)
{
	iio_update_widgets(glb_widgets, num_glb);
	iio_update_widgets(tx_widgets, num_tx);
	iio_update_widgets(rx_widgets, num_rx);
	filter_fir_update();
	rx_update_labels();
	glb_settings_update_labels();
	rssi_update_labels();
	rx_phase_rotation_update();
}

static void hide_section_cb(GtkToggleToolButton *btn, GtkWidget *section)
{
	GtkWidget *toplevel;

	if (gtk_toggle_tool_button_get_active(btn)) {
		gtk_object_set(GTK_OBJECT(btn), "stock-id", "gtk-go-down", NULL);
		gtk_widget_show(section);
	} else {
		gtk_object_set(GTK_OBJECT(btn), "stock-id", "gtk-go-up", NULL);
		gtk_widget_hide(section);
		toplevel = gtk_widget_get_toplevel(GTK_WIDGET(btn));
		if (GTK_WIDGET_TOPLEVEL(toplevel))
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
					"RX_LO_fastlock_store", profile);
			break;
		case 2: /* TX Store */
			iio_widget_save(&tx_widgets[tx_lo]);
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile));
			write_int(iio_device_find_channel(dev, "altvoltage1", true),
					"TX_LO_fastlock_store", profile);
			break;
		case 3: /* RX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(rx_fastlock_profile));
			write_int(iio_device_find_channel(dev, "altvoltage0", true),
					"RX_LO_fastlock_recall", profile);
			iio_widget_update(&rx_widgets[rx_lo]);
			break;
		case 4: /* TX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile));
			write_int(iio_device_find_channel(dev, "altvoltage1", true),
					"TX_LO_fastlock_recall", profile);
			iio_widget_update(&tx_widgets[tx_lo]);
			break;
	}
}

static int load_fir_filter(const char *file_name)
{
	int ret = -1;
	FILE *f = fopen(file_name, "r");
	if (f) {
		char *buf;
		ssize_t len;

		fseek(f, 0, SEEK_END);
		len = ftell(f);
		buf = malloc(len);
		fseek(f, 0, SEEK_SET);
		len = fread(buf, 1, len, f);
		fclose(f);

		ret = iio_device_attr_write_raw(dev,
				"filter_fir_config", buf, len);
		free(buf);
	}

	if (ret < 0) {
		fprintf(stderr, "FIR filter config failed\n");
		GtkWidget *toplevel = gtk_widget_get_toplevel(fmcomms2_panel);
		if (!gtk_widget_is_toplevel(toplevel))
			toplevel = NULL;

		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(toplevel),
						GTK_DIALOG_MODAL,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_CLOSE,
						"\nFailed to configure the FIR filter using the selected file.");
		gtk_window_set_title(GTK_WINDOW(dialog), "FIR Filter Configuration Failed");
		if (gtk_dialog_run(GTK_DIALOG(dialog)))
			gtk_widget_destroy(dialog);

	} else {
		strcpy(last_fir_filter, file_name);
	}

	return ret;
}

void filter_fir_config_file_set_cb (GtkFileChooser *chooser, gpointer data)
{
	char *file_name = gtk_file_chooser_get_filename(chooser);

	if (load_fir_filter(file_name) < 0) {
		if (strlen(last_fir_filter) == 0)
			gtk_file_chooser_set_filename(chooser, "(None)");
		else
			gtk_file_chooser_set_filename(chooser, last_fir_filter);
	}
}

static void process_dac_buffer_file (const char *file_name)
{
	int ret, size = 0, s_size;
	struct stat st;
	char *buf = NULL, *tmp;
	FILE *infile;
	unsigned int buffer_channels = 0;
	unsigned int major, minor;
	struct utsname uts;

	uname(&uts);
	sscanf(uts.release, "%u.%u", &major, &minor);
	if (major < 2 || (major == 3 && minor < 14)) {
		if (is_2rx_2tx)
			buffer_channels = 4;
		else
			buffer_channels = 2;
	} else {
		buffer_channels = tx_enabled_channels_count(GTK_TREE_VIEW(tx_channel_list), NULL);
	}

	ret = analyse_wavefile(file_name, &buf, &size, buffer_channels / 2);
	if (ret == -3)
		return;

	if (ret == -1 || buf == NULL) {
		stat(file_name, &st);
		buf = malloc(st.st_size);
		if (buf == NULL)
			return;
		infile = fopen(file_name, "r");
		size = fread(buf, 1, st.st_size, infile);
		fclose(infile);
	}

	if (dds_buffer) {
		iio_buffer_destroy(dds_buffer);
		dds_buffer = NULL;
	}

	enable_dds(false);

	s_size = iio_device_get_sample_size(dds);
	if (!s_size) {
		fprintf(stderr, "Unable to create buffer due to sample size: %s\n", strerror(errno));
		free(buf);
		return;
	}

	dds_buffer = iio_device_create_buffer(dds, size / s_size, true);
	if (!dds_buffer) {
		fprintf(stderr, "Unable to create buffer: %s\n", strerror(errno));
		free(buf);
		return;
	}

	memcpy(iio_buffer_start(dds_buffer), buf,
			iio_buffer_end(dds_buffer) - iio_buffer_start(dds_buffer));

	iio_buffer_push(dds_buffer);
	free(buf);

	tmp = strdup(file_name);
	if (dac_buf_filename)
		free(dac_buf_filename);
	dac_buf_filename = tmp;
	printf("Waveform loaded\n");
}

static void dac_buffer_config_file_set_cb (GtkFileChooser *chooser, gpointer data)
{
	char *file_name = gtk_file_chooser_get_filename(chooser);
	if (file_name)
		process_dac_buffer_file((const char *)file_name);
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

#define DDS_DISABLED  0
#define DDS_ONE_TONE  1
#define DDS_TWO_TONE  2
#define DDS_INDEPDENT 3
#define DDS_BUFFER    4

static void dds_locked_phase_cb(GtkToggleButton *btn, gpointer channel)
{
	gint ch1 = TX1_T1_I + (((long) channel - 1) * 4);
	gint ch2 = TX1_T2_I + (((long) channel - 1) * 4);

	gdouble phase1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_phase[ch1]));
	gdouble phase2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_phase[ch2]));

	gdouble freq1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_freq[ch1]));
	gdouble freq2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_freq[ch2]));

	gdouble inc1, inc2;

	if (freq1 >= 0)
		inc1 = 90.0;
	else
		inc1 = 270;

	if ((phase1 - inc1) < 0)
		phase1 += 360;

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[(long)channel]))) {
		case DDS_ONE_TONE:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_phase[ch1 + 2]), phase1 - inc1);
			break;
		case DDS_TWO_TONE:
			if (freq2 >= 0)
				inc2 = 90;
			else
				inc2 = 270;
			if ((phase2 - inc2) < 0)
				phase2 += 360;

			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_phase[ch1 + 2]), phase1 - inc1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_phase[ch2 + 2]), phase2 - inc2);
			break;
		default:
			printf("%s: error\n", __func__);
			break;
	}
}

static void dds_locked_freq_cb(GtkToggleButton *btn, gpointer channel)
{
	gint ch1 = TX1_T1_I + (((long)channel - 1) * 4);
	gint ch2 = TX1_T2_I + (((long)channel - 1) * 4);

	gdouble freq1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_freq[ch1]));
	gdouble freq2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_freq[ch2]));

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[(long)channel]))) {
		case DDS_ONE_TONE:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_freq[ch1 + 2]), freq1);
			break;
		case DDS_TWO_TONE:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_freq[ch1 + 2]), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_freq[ch2 + 2]), freq2);
			break;
		default:
			printf("%s: error : %i\n", __func__,
					gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[(long)channel])));
			break;
	}

	dds_locked_phase_cb(NULL, channel);
}

static void dds_locked_scale_cb(GtkSpinButton *btn, gpointer channel)
{
	gint ch1 = TX1_T1_I + (((long)channel - 1) * 4);
	gint ch2 = TX1_T2_I + (((long)channel - 1) * 4);

	gdouble scale1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[ch1]));
	gdouble scale2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[ch2]));

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[(long)channel]))) {
		case DDS_ONE_TONE:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[ch1 + 2]), scale1);
			break;
		case DDS_TWO_TONE:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[ch1 + 2]), scale1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[ch2 + 2]), scale2);
			break;
		default:
			break;
	}

}

static void tx_sample_rate_changed(GtkSpinButton *spinbutton, gpointer user_data)
{
	gdouble val, rate;
	int i;

	rate = gtk_spin_button_get_value(spinbutton) / 2.0;
	for (i = TX1_T1_I; i <= TX2_T2_Q ; i++) {
		val = gtk_adjustment_get_value(adj_freq[i]);
		if (fabs(val) > rate)
			gtk_adjustment_set_value(adj_freq[i], rate);
		gtk_adjustment_set_lower(adj_freq[i], -1 * rate);
		gtk_adjustment_set_upper(adj_freq[i], rate);
	}
}

static void rx_phase_rotation_set(GtkSpinButton *spinbutton, gpointer user_data)
{
	glong offset = (glong) user_data;
	struct iio_channel *out0, *out1;
	gdouble val, phase;

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

static void enable_dds(bool on_off)
{
	int ret;

	if (on_off == dds_activated && !dds_disabled)
		return;
	dds_activated = on_off;

	if (dds_buffer) {
		iio_buffer_destroy(dds_buffer);
		dds_buffer = NULL;
	}

	ret = iio_channel_attr_write_bool(iio_device_find_channel(dds, "altvoltage0", true), "raw", on_off);
	if (ret < 0) {
		fprintf(stderr, "Failed to toggle DDS: %d\n", ret);
		return;
	}
}

/* The Slave device doesn't have a own DMA, therefore no buffer and enable
 * however the select mux still needs to be set.
 * This is a temp workaround
 */

static void slave_enable_dma_mux(bool enable)
{
#ifdef SLAVE

enum dds_data_select {
	DATA_SEL_DDS,
	DATA_SEL_SED,
	DATA_SEL_DMA,
	DATA_SEL_ZERO,	/* OUTPUT 0 */
	DATA_SEL_PN7,
	DATA_SEL_PN15,
	DATA_SEL_PN23,
	DATA_SEL_PN31,
	DATA_SEL_LB,	/* loopback data (ADC) */
	DATA_SEL_PNXX,	/* (Device specific) */
};

#define ADI_REG_CHAN_CNTRL_7(c)		(0x0418 + (c) * 0x40) /* v8.0 */
#define ADI_DAC_DDS_SEL(x)		(((x) & 0xF) << 0)

	int i;

	for (i = 0; i < 4; i++)
		iio_device_reg_write(dds, 0x80000000 | ADI_REG_CHAN_CNTRL_7(i),
			ADI_DAC_DDS_SEL(enable ? DATA_SEL_DMA : DATA_SEL_DDS));
#endif
}

#define IIO_SPIN_SIGNAL "value-changed"
#define IIO_COMBO_SIGNAL "changed"

static void manage_dds_mode(GtkComboBox *box, glong channel)
{
	gint active, i, start, end;
	static gdouble *mag = NULL;

	if (!mag) {
		mag = g_renew(gdouble, mag, 9);
		mag[TX1_T1_I] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX1_T1_I]));
		mag[TX1_T2_I] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX1_T2_I]));
		mag[TX1_T1_Q] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX1_T1_Q]));
		mag[TX1_T2_Q] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX1_T2_Q]));
		mag[TX2_T1_I] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX2_T1_I]));
		mag[TX2_T2_I] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX2_T2_I]));
		mag[TX2_T1_Q] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX2_T1_Q]));
		mag[TX2_T2_Q] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX2_T2_Q]));
		mag[TX_OFF] = 0.000000;
	}

	active = gtk_combo_box_get_active(box);

	if (active != 4) {
		if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[1])) == 4) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode_tx[1]), active);
			manage_dds_mode(GTK_COMBO_BOX(dds_mode_tx[1]), 1);
		}
		if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[2])) == 4) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode_tx[2]), active);
			manage_dds_mode(GTK_COMBO_BOX(dds_mode_tx[2]), 2);
		}
	}

	switch (active) {
	case DDS_DISABLED:
		slave_enable_dma_mux(0);
		if (channel == 1) {
		 	start = TX1_T1_I;
			end = TX1_T2_Q;
		} else {
			start = TX2_T1_I;
			end = TX2_T2_Q;
		}
		for (i = start; i <= end; i++) {
			if (gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[i])) != mag[TX_OFF]) {
				 mag[i] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[i]));
				 gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[i]), mag[TX_OFF]);
			}

		}
		start = 0;
		for (i = TX1_T1_I; i <= TX2_T2_Q; i++) {
			if (gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[i])) !=  mag[TX_OFF])
				start++;
		}
		if (!dds_activated && dds_buffer) {
			iio_buffer_destroy(dds_buffer);
			dds_buffer = NULL;
		}
		dds_disabled = true;
		if (!start)
			enable_dds(false);
		else
			enable_dds(true);

		gtk_widget_hide(channel_I_tx[channel]);
		gtk_widget_hide(channel_Q_tx[channel]);
		gtk_widget_hide(dac_buffer);

		break;
	case DDS_ONE_TONE:
		slave_enable_dma_mux(0);
		enable_dds(true);
		gtk_widget_hide(dac_buffer);
		gtk_label_set_markup(GTK_LABEL(dds_I_TX_l[channel]),"<b>Single Tone</b>");

		gtk_widget_show_all(channel_I_tx[channel]);
		gtk_widget_hide(channel_I_tone2_tx[channel]);
		gtk_widget_hide(channel_Q_tx[channel]);

		if (channel == 1) {
			if (gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX1_T1_I])) == mag[TX_OFF]) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[TX1_T1_I]), mag[TX1_T1_I]);
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[TX1_T1_Q]), mag[TX1_T1_Q]);
			}

			if (gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX1_T2_I])) != mag[TX_OFF]) {
				mag[TX1_T2_I] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX1_T2_I]));
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[TX1_T2_I]), mag[TX_OFF]);
				mag[TX1_T2_Q] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX1_T2_Q]));
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[TX1_T2_Q]), mag[TX_OFF]);
			}

			start = TX1_T1_I;
			end = TX1_T2_I;
		} else {
			if (gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX2_T1_I])) == mag[TX_OFF]) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[TX2_T1_I]), mag[TX2_T1_I]);
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[TX2_T1_Q]), mag[TX2_T1_Q]);
			}

			if (gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX2_T2_I])) != mag[TX_OFF]) {
				mag[TX2_T2_I] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX2_T2_I]));
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[TX2_T2_I]), mag[TX_OFF]);
				mag[TX2_T2_Q] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[TX2_T2_Q]));
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[TX2_T2_Q]), mag[TX_OFF]);
			}

			start = TX2_T1_I;
			end = TX2_T2_I;
		}

		/* Connect the widgets that are showing */
		if (!dds_scale_hid[start])
			dds_scale_hid[start] = g_signal_connect(dds_scale[start], IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_scale_cb), (gpointer *)channel);
		if (!dds_freq_hid[start])
			dds_freq_hid[start] = g_signal_connect(dds_freq[start], IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_freq_cb), (gpointer *)channel);
		if (!dds_phase_hid[start])
			dds_phase_hid[start] = g_signal_connect(dds_phase[start], IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_phase_cb), (gpointer *)channel);

		/* Disconnect the rest */
		if (dds_scale_hid[end]) {
			g_signal_handler_disconnect(dds_scale[end], dds_scale_hid[end]);
			dds_scale_hid[end] = 0;
		}
		if (dds_freq_hid[end]) {
			g_signal_handler_disconnect(dds_freq[end], dds_freq_hid[end]);
			dds_freq_hid[end] = 0;
		}
		if (dds_phase_hid[end]) {
			g_signal_handler_disconnect(dds_phase[end], dds_phase_hid[end]);
			dds_phase_hid[end] = 0;
		}

		dds_locked_scale_cb(NULL, (gpointer *)channel);
		dds_locked_freq_cb(NULL, (gpointer *)channel);
		dds_locked_phase_cb(NULL, (gpointer *)channel);
		break;
	case DDS_TWO_TONE:
		slave_enable_dma_mux(0);
		enable_dds(true);
		gtk_widget_hide(dac_buffer);
		gtk_widget_show_all(channel_I_tx[channel]);
		gtk_widget_hide(channel_Q_tx[channel]);

		gtk_label_set_markup(GTK_LABEL(dds_I_TX_l[channel]),"<b>Two Tones</b>");

		if (channel == 1) {
			start = TX1_T1_I;
			end = TX1_T2_Q;
		} else {
			start = TX2_T1_I;
			end = TX2_T2_Q;
		}

		for (i = start; i <= end; i++) {
			if (gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[i])) == mag[TX_OFF]) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[i]), mag[i]);
			}
		}

		if (channel == 1)
			end = TX1_T2_I;
		else
			end = TX2_T2_I;

		for (i = start; i <= end; i++) {
			if (!dds_scale_hid[i])
				dds_scale_hid[i] = g_signal_connect(dds_scale[i], IIO_SPIN_SIGNAL,
						G_CALLBACK(dds_locked_scale_cb), (gpointer *)channel);
			if (!dds_freq_hid[i])
				dds_freq_hid[i] = g_signal_connect(dds_freq[i] , IIO_SPIN_SIGNAL,
						G_CALLBACK(dds_locked_freq_cb), (gpointer *)channel);
			if (!dds_phase_hid[i])
				dds_phase_hid[i] = g_signal_connect(dds_phase[i], IIO_SPIN_SIGNAL,
						G_CALLBACK(dds_locked_phase_cb), (gpointer *)channel);
		}

		/* Force sync */
		dds_locked_scale_cb(NULL, (gpointer *)channel);
		dds_locked_freq_cb(NULL, (gpointer *)channel);
		dds_locked_phase_cb(NULL, (gpointer *)channel);

		break;
	case DDS_INDEPDENT:
		/* Independant/Individual control */
		slave_enable_dma_mux(0);
		enable_dds(true);
		gtk_widget_show_all(channel_I_tx[channel]);
		gtk_widget_show_all(channel_Q_tx[channel]);
		gtk_widget_hide(dac_buffer);
		gtk_label_set_markup(GTK_LABEL(dds_I_TX_l[channel]),"<b>Channel I</b>");

		if (channel == 1) {
			start = TX1_T1_I;
			end = TX1_T2_Q;
		} else {
			start = TX2_T1_I;
			end = TX2_T2_Q;
		}

		for (i = start; i <= end; i++) {
			if (gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_scale[i])) == mag[TX_OFF])
				 gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_scale[i]), mag[i]);

			if (dds_scale_hid[i]) {
				g_signal_handler_disconnect(dds_scale[i], dds_scale_hid[i]);
				dds_scale_hid[i] = 0;
			}
			if (dds_freq_hid[i]) {
				g_signal_handler_disconnect(dds_freq[i], dds_freq_hid[i]);
				dds_freq_hid[i] = 0;
			}
			if (dds_phase_hid[i]) {
				g_signal_handler_disconnect(dds_phase[i], dds_phase_hid[i]);
				dds_phase_hid[i] = 0;
			}
		}
		break;
	case DDS_BUFFER:
		slave_enable_dma_mux(1);
		if ((dds_activated || dds_disabled) && dac_buf_filename) {
			dds_disabled = false;
			process_dac_buffer_file(dac_buf_filename);
		}
		gtk_widget_show(dac_buffer);
		gtk_widget_hide(channel_I_tx[1]);
		gtk_widget_hide(channel_Q_tx[1]);
		gtk_widget_hide(channel_I_tx[2]);
		gtk_widget_hide(channel_Q_tx[2]);
		gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode_tx[1]), 4);
		gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode_tx[2]), 4);
		break;
	default:
		printf("glade file out of sync with C file - please contact developers\n");
		break;
	}
}

/* Check for a valid two channels combination (ch0->ch1, ch2->ch3, ...)
 *
 * struct iio_channel_info *chanels - list of channels of a device
 * int ch_count - number of channel in the list
 * char* ch_name - output parameter: stores references to the enabled
 *                 channels.
 * Return 1 if the channel combination is valid
 * Return 0 if the combination is not valid
 */
int channel_combination_check(struct iio_device *dev, const char **ch_names)
{
	bool consecutive_ch = FALSE;
	unsigned int i, k, nb_channels = iio_device_get_channels_count(dev);

	for (i = 0, k = 0; i < nb_channels; i++) {
		struct iio_channel *ch = iio_device_get_channel(dev, i);
		struct extra_info *info = iio_channel_get_data(ch);

		if (info->may_be_enabled) {
			const char *name = iio_channel_get_name(ch) ?: iio_channel_get_id(ch);
			ch_names[k++] = name;

			if (i > 0) {
				struct extra_info *prev = iio_channel_get_data(iio_device_get_channel(dev, i - 1));
				if (prev->may_be_enabled) {
					consecutive_ch = TRUE;
					break;
				}
			}
		}
	}
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

int handle_external_request (const char *request)
{
	int ret = 0;

	if (!strcmp(request, "Reload Settings")) {
		reload_button_clicked(NULL, 0);
		ret = 1;
	}

	return ret;
}

static int fmcomms2_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	struct iio_channel *ch0 = iio_device_find_channel(dev, "voltage0", false),
			   *ch1 = iio_device_find_channel(dev, "voltage1", false),
			   *ch2, *ch3, *ch4, *ch5, *ch6, *ch7;
	int i;

	for (i = 0; i <= TX2_T2_Q; i++) {
		dds_freq_hid[i] = 0;
		dds_scale_hid[i] = 0;
		dds_phase_hid[i] = 0;
	}

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (!gtk_builder_add_from_file(builder, "fmcomms2.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "fmcomms2.glade", NULL);

	is_2rx_2tx = ch1 && iio_channel_find_attr(ch1, "hardwaregain");

	fmcomms2_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms2_panel"));

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

	rf_port_select_rx = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_rx"));
	rx_gain_control_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx1"));
	rx_gain_control_rx2 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx2"));
	rx_gain_control_modes_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx1"));
	rx_gain_control_modes_rx2 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx2"));
	rx1_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx1"));
	rx2_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx2"));
	rx_fastlock_profile = GTK_WIDGET(gtk_builder_get_object(builder, "rx_fastlock_profile"));

	/* Transmit Chain */

	rf_port_select_tx = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_tx"));
	tx_fastlock_profile = GTK_WIDGET(gtk_builder_get_object(builder, "tx_fastlock_profile"));
	tx_channel_list = GTK_WIDGET(gtk_builder_get_object(builder, "treeview_tx_channels"));
	dds_mode_tx[1] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_mode_tx1"));
	dds_mode_tx[2] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_mode_tx2"));

	dds_freq[TX1_T1_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx1_freq"));
	dds_freq[TX1_T2_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx1_freq"));
	dds_freq[TX1_T1_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx1_freq"));
	dds_freq[TX1_T2_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx1_freq"));

	dds_freq[TX2_T1_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx2_freq"));
	dds_freq[TX2_T2_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx2_freq"));
	dds_freq[TX2_T1_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx2_freq"));
	dds_freq[TX2_T2_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx2_freq"));

	dds_scale[TX1_T1_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx1_scale"));
	dds_scale[TX1_T2_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx1_scale"));
	dds_scale[TX1_T1_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx1_scale"));
	dds_scale[TX1_T2_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx1_scale"));

	dds_scale[TX2_T1_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx2_scale"));
	dds_scale[TX2_T2_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx2_scale"));
	dds_scale[TX2_T1_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx2_scale"));
	dds_scale[TX2_T2_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx2_scale"));

	dds_phase[TX1_T1_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx1_phase"));
	dds_phase[TX1_T2_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx1_phase"));
	dds_phase[TX1_T1_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx1_phase"));
	dds_phase[TX1_T2_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx1_phase"));

	dds_phase[TX2_T1_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx2_phase"));
	dds_phase[TX2_T2_I] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx2_phase"));
	dds_phase[TX2_T1_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx2_phase"));
	dds_phase[TX2_T2_Q] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx2_phase"));

	adj_freq[TX1_T1_I] = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX1_I1_freq"));
	adj_freq[TX1_T2_I] = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX1_I2_freq"));
	adj_freq[TX1_T1_Q] = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX1_Q1_freq"));
	adj_freq[TX1_T2_Q] = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX1_Q2_freq"));
	adj_freq[TX2_T1_I] = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX2_I1_freq"));
	adj_freq[TX2_T2_I] = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX2_I2_freq"));
	adj_freq[TX2_T1_Q] = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX2_Q1_freq"));
	adj_freq[TX2_T2_Q] = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX2_Q2_freq"));

	dds_I_TX_l[1] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_I_TX1_l"));
	dds_I_TX_l[2] = GTK_WIDGET(gtk_builder_get_object(builder, "dds_I_TX2_l"));
	channel_I_tx[1] = GTK_WIDGET(gtk_builder_get_object(builder, "frame_Channel_I_tx1"));
	channel_Q_tx[1] = GTK_WIDGET(gtk_builder_get_object(builder, "frame_Channel_Q_tx1"));
	channel_I_tx[2] = GTK_WIDGET(gtk_builder_get_object(builder, "frame_Channel_I_tx2"));
	channel_Q_tx[2] = GTK_WIDGET(gtk_builder_get_object(builder, "frame_Channel_Q_tx2"));
	channel_I_tone2_tx[1] = GTK_WIDGET(gtk_builder_get_object(builder, "frame_Tone2_ch_I_tx1"));
	channel_I_tone2_tx[2] = GTK_WIDGET(gtk_builder_get_object(builder, "frame_Tone2_ch_I_tx2"));
	dac_buffer = GTK_WIDGET(gtk_builder_get_object(builder, "dac_buffer"));

	rx_phase_rotation[0] = GTK_WIDGET(gtk_builder_get_object(builder, "rx1_phase_rotation"));
	rx_phase_rotation[1] = GTK_WIDGET(gtk_builder_get_object(builder, "rx2_phase_rotation"));

	gtk_combo_box_set_active(GTK_COMBO_BOX(ensm_mode_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(trx_rate_governor_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes_rx1), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes_rx2), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode_tx[1]), 1);
	gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode_tx[2]), 1);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rf_port_select_rx), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rf_port_select_tx), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_fastlock_profile), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(tx_fastlock_profile), 0);

	/* Bind the IIO device files to the GUI widgets */

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

	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		dev, NULL, "dcxo_tune_coarse", builder, "dcxo_coarse_tune",
		0);
	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		dev, NULL, "dcxo_tune_fine", builder, "dcxo_fine_tune",
		0);

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
	iio_spin_button_s64_init_from_builder(&rx_widgets[num_rx++],
		dev, ch1, "frequency", builder,
		"rx_lo_freq", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "quadrature_tracking_en", builder,
		"quad", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "rf_dc_offset_tracking_en", builder,
		"rfdc", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		dev, ch0, "bb_dc_offset_tracking_en", builder,
		"bbdc", 0);

	iio_spin_button_init_from_builder(&rx_widgets[num_rx],
		dev, ch1, "calibphase",
		builder, "rx1_phase_rotation", NULL);
	iio_spin_button_add_progress(&rx_widgets[num_rx++]);

	/* Transmit Chain */

	ch0 = iio_device_find_channel(dev, "voltage0", true);
	if (is_2rx_2tx)
		ch1 = iio_device_find_channel(dev, "voltage1", true);

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

	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
		dev, ch1, "frequency", builder, "tx_lo_freq", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	ch0 = iio_device_find_channel(dds, "TX1_I_F1", true);

	iio_spin_button_init(&tx_widgets[num_tx++],
			dds, ch0, "frequency", dds_freq[TX1_T1_I], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	ch1 = iio_device_find_channel(dds, "TX1_I_F2", true);
	iio_spin_button_init(&tx_widgets[num_tx++],
			dds, ch1, "frequency", dds_freq[TX1_T2_I], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	ch2 = iio_device_find_channel(dds, "TX1_Q_F1", true);
	iio_spin_button_init(&tx_widgets[num_tx++],
			dds, ch2, "frequency", dds_freq[TX1_T1_Q], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	ch3 = iio_device_find_channel(dds, "TX1_Q_F2", true);
	iio_spin_button_init(&tx_widgets[num_tx++],
			dds, ch3, "frequency", dds_freq[TX1_T2_Q], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	ch4 = iio_device_find_channel(dds, "TX2_I_F1", true);
	iio_spin_button_init(&tx_widgets[num_tx++],
			dds, ch4, "frequency", dds_freq[TX2_T1_I], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	ch5 = iio_device_find_channel(dds, "TX2_I_F2", true);
	iio_spin_button_init(&tx_widgets[num_tx++],
			dds, ch5, "frequency", dds_freq[TX2_T2_I], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	ch6 = iio_device_find_channel(dds, "TX2_Q_F1", true);
	iio_spin_button_init(&tx_widgets[num_tx++],
			dds, ch6, "frequency", dds_freq[TX2_T1_Q], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	ch7 = iio_device_find_channel(dds, "TX2_Q_F2", true);
	iio_spin_button_init(&tx_widgets[num_tx++],
			dds, ch7, "frequency", dds_freq[TX2_T2_Q], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch0, "scale",
			dds_scale[TX1_T1_I], NULL);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch1, "scale",
			dds_scale[TX1_T2_I], NULL);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch2, "scale",
			dds_scale[TX1_T1_Q], NULL);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch3, "scale",
			dds_scale[TX1_T2_Q], NULL);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch4, "scale",
			dds_scale[TX2_T1_I], NULL);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch5, "scale",
			dds_scale[TX2_T2_I], NULL);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch6, "scale",
			dds_scale[TX2_T1_Q], NULL);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch7, "scale",
			dds_scale[TX2_T2_Q], NULL);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch0, "phase",
			dds_phase[TX1_T1_I], &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch1, "phase",
			dds_phase[TX1_T2_I], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch2, "phase",
			dds_phase[TX1_T1_Q], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch3, "phase",
			dds_phase[TX1_T2_Q], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch4, "phase",
			dds_phase[TX2_T1_I], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch5, "phase",
			dds_phase[TX2_T2_I], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch6, "phase",
			dds_phase[TX2_T1_Q], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++], dds, ch7, "phase",
			dds_phase[TX2_T2_Q], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	tx_channel_list_init(builder);

	/* Signals connect */

	g_builder_connect_signal(builder, "rx1_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)0);

	g_builder_connect_signal(builder, "rx2_phase_rotation", "value-changed",
			G_CALLBACK(rx_phase_rotation_set), (gpointer *)2);

	g_builder_connect_signal(builder, "sampling_freq_tx", "value-changed",
			G_CALLBACK(tx_sample_rate_changed), NULL);

	g_signal_connect(dds_mode_tx[1], "changed", G_CALLBACK(manage_dds_mode),
			(gpointer *)1);
	g_signal_connect(dds_mode_tx[2], "changed", G_CALLBACK(manage_dds_mode),
			(gpointer *)2);

	g_builder_connect_signal(builder, "fmcomms2_settings_reload", "clicked",
		G_CALLBACK(reload_button_clicked), NULL);

	g_builder_connect_signal(builder, "filter_fir_config", "file-set",
		G_CALLBACK(filter_fir_config_file_set_cb), NULL);

	g_builder_connect_signal(builder, "dac_buffer", "file-set",
		G_CALLBACK(dac_buffer_config_file_set_cb), NULL);

	g_builder_connect_signal(builder, "rx_fastlock_store", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 1);
	g_builder_connect_signal(builder, "tx_fastlock_store", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 2);
	g_builder_connect_signal(builder, "rx_fastlock_recall", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 3);
	g_builder_connect_signal(builder, "tx_fastlock_recall", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 4);

	g_builder_connect_signal(builder, "sampling_freq_tx", "value-changed",
			G_CALLBACK(tx_sample_rate_changed), NULL);

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

	iio_spin_button_set_on_complete_function(&rx_widgets[rx_sample_freq],
		sample_frequency_changed_cb);
	iio_spin_button_set_on_complete_function(&tx_widgets[tx_sample_freq],
		sample_frequency_changed_cb);
	iio_spin_button_set_on_complete_function(&rx_widgets[rx_lo],
		sample_frequency_changed_cb);
	iio_spin_button_set_on_complete_function(&tx_widgets[tx_lo],
		sample_frequency_changed_cb);

	iio_update_widgets(glb_widgets, num_glb);
	tx_update_values();
	rx_update_values();
	filter_fir_update();
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(disable_all_fir_filters), true);

	glb_settings_update_labels();
	rssi_update_labels();

	for (i = 0; i < iio_device_get_channels_count(dds); i++) {
		struct iio_channel *ch = iio_device_get_channel(dds, i);

		if (iio_channel_is_scan_element(ch))
			dds_num_channels++;
	}

	manage_dds_mode(GTK_COMBO_BOX(dds_mode_tx[1]), 1);
	manage_dds_mode(GTK_COMBO_BOX(dds_mode_tx[2]), 2);

	add_ch_setup_check_fct("cf-ad9361-lpc", channel_combination_check);
	plugin_fft_corr = 20 * log10(1/sqrt(HANNING_ENBW));

	block_diagram_init(builder, 2, "fmcomms2.svg", "AD_FMCOMM2S2_RevC.jpg");

	this_page = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), fmcomms2_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), fmcomms2_panel, "FMComms2");
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(filter_fir_config), OSC_FILTER_FILE_PATH);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dac_buffer), OSC_WAVEFORM_FILE_PATH);

	if (!is_2rx_2tx) {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_rx2")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_fpga_tx2")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame_fpga_rx2")));
	}

	g_thread_new("Update_thread", (void *) &update_display, NULL);

	return 0;
}

#define SYNC_RELOAD "SYNC_RELOAD"

static char *handle_item(struct osc_plugin *plugin, const char *attrib,
			 const char *value)
{
	char *buf;

	if (MATCH_ATTRIB(SYNC_RELOAD)) {
		if (value)
			reload_button_clicked(NULL, 0);
		else
			return "1";
	} else if (MATCH_ATTRIB("load_fir_filter_file")) {
		if (value) {
			if (value[0]) {
				load_fir_filter(value);
				gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(filter_fir_config), value);
			}
		} else {
			return last_fir_filter;
		}
	} else if (MATCH_ATTRIB("dds_mode_tx1")) {
		if (value) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode_tx[1]), atoi(value));
		} else {
			buf = malloc (10);
			sprintf(buf, "%i", gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[1])));
			return buf;
		}
	} else if (MATCH_ATTRIB("dds_mode_tx2")) {
		if (value) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode_tx[2]), atoi(value));
		} else {
			buf = malloc (10);
			sprintf(buf, "%i", gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[2])));
			return buf;
		}
	} else if (MATCH_ATTRIB("dac_buf_filename") &&
				gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[1])) == 4) {
		if (value) {
			gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dac_buffer), value);
			process_dac_buffer_file(value);
		} else
			return dac_buf_filename;
	} else if (MATCH_ATTRIB("global_settings_show")) {
		if (value) {
			if (atoi(value))
				gtk_toggle_tool_button_set_active(section_toggle[SECTION_GLOBAL], true);
			else
				gtk_toggle_tool_button_set_active(section_toggle[SECTION_GLOBAL], false);
			hide_section_cb(section_toggle[SECTION_GLOBAL], section_setting[SECTION_GLOBAL]);
		} else {
			buf = malloc (10);
			if (gtk_toggle_tool_button_get_active(section_toggle[SECTION_GLOBAL]))
				sprintf(buf, "1 # show");
			else
				sprintf(buf, "0 # hide");
			return buf;
		}
	} else if (MATCH_ATTRIB("tx_show")) {
		if (value) {
			if (atoi(value))
				gtk_toggle_tool_button_set_active(section_toggle[SECTION_TX], true);
			else
				gtk_toggle_tool_button_set_active(section_toggle[SECTION_TX], false);
			hide_section_cb(section_toggle[SECTION_TX], section_setting[SECTION_TX]);
		} else {
			buf = malloc (10);
			if (gtk_toggle_tool_button_get_active(section_toggle[SECTION_TX]))
				sprintf(buf, "1 # show");
			else
				sprintf(buf, "0 # hide");
			return buf;
		}

	} else if (MATCH_ATTRIB("rx_show")) {
		if (value) {
			if (atoi(value))
				gtk_toggle_tool_button_set_active(section_toggle[SECTION_RX], true);
			else
				gtk_toggle_tool_button_set_active(section_toggle[SECTION_RX], false);
			hide_section_cb(section_toggle[SECTION_RX], section_setting[SECTION_RX]);
		} else {
			buf = malloc (10);
			if (gtk_toggle_tool_button_get_active(section_toggle[SECTION_RX]))
				sprintf(buf, "1 # show");
			else
				sprintf(buf, "0 # hide");
			return buf;
		}

	} else if (MATCH_ATTRIB("fpga_show")) {
		if (value) {
			if (atoi(value))
				gtk_toggle_tool_button_set_active(section_toggle[SECTION_FPGA], true);
			else
				gtk_toggle_tool_button_set_active(section_toggle[SECTION_FPGA], false);
			hide_section_cb(section_toggle[SECTION_FPGA], section_setting[SECTION_FPGA]);
		} else {
			buf = malloc (10);
			if (gtk_toggle_tool_button_get_active(section_toggle[SECTION_FPGA]))
				sprintf(buf, "1 # show");
			else
				sprintf(buf, "0 # hide");
			return buf;
		}

	} else {
		if (value) {
			printf("Unhandled tokens in ini file,\n"
				"\tSection %s\n\tAtttribute : %s\n\tValue: %s\n",
				"FMComms2", attrib, value);
			return "FAIL";
		}
	}

	return NULL;
}

static const char *fmcomms2_sr_attribs[] = {
	PHY_DEVICE".trx_rate_governor",
	PHY_DEVICE".dcxo_tune_coarse",
	PHY_DEVICE".dcxo_tune_fine",
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
	PHY_DEVICE".out_altvoltage0_RX_LO_frequency",
	PHY_DEVICE".out_altvoltage1_TX_LO_frequency",
	PHY_DEVICE".out_voltage0_hardwaregain",
	PHY_DEVICE".out_voltage1_hardwaregain",
	PHY_DEVICE".out_voltage_sampling_frequency",
	PHY_DEVICE".in_voltage_rf_bandwidth",
	PHY_DEVICE".out_voltage_rf_bandwidth",
	"load_fir_filter_file",
	PHY_DEVICE".in_voltage_filter_fir_en",
	PHY_DEVICE".out_voltage_filter_fir_en",
	PHY_DEVICE".in_out_voltage_filter_fir_en",
	"global_settings_show",
	"tx_show",
	"rx_show",
	"fpga_show",
	"dds_mode_tx1",
	"dds_mode_tx2",
	"dac_buf_filename",
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
	SYNC_RELOAD,
	NULL,
};

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void fmcomms2_get_preferred_size(int *width, int *height)
{
	if (width)
		*width = 1100;
	if (height)
		*height = 800;
}

static void context_destroy(void)
{
	iio_context_destroy(ctx);
}

static bool fmcomms2_identify(void)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	if (!iio_context_find_device(osc_ctx, PHY_DEVICE)
		|| !iio_context_find_device(osc_ctx, DDS_DEVICE))
		return false;

	ctx = osc_create_context();
	dev = iio_context_find_device(ctx, PHY_DEVICE);
	dds = iio_context_find_device(ctx, DDS_DEVICE);
	cap = iio_context_find_device(ctx, CAP_DEVICE);


	if (!dev || !dds || !cap)
		iio_context_destroy(ctx);
	return !!dev && !!dds && !!cap;
}

struct osc_plugin plugin = {
#ifdef SLAVE
	.name = "FMComms2/3/4-HPC",
#else
	.name = "FMComms2/3/4",
#endif

	.identify = fmcomms2_identify,
	.init = fmcomms2_init,
	.save_restore_attribs = fmcomms2_sr_attribs,
	.handle_item = handle_item,
	.handle_external_request = handle_external_request,
	.update_active_page = update_active_page,
	.get_preferred_size = fmcomms2_get_preferred_size,
	.destroy = context_destroy,
};
