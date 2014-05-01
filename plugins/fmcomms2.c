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

#include "../osc.h"
#include "../iio_widget.h"
#include "../iio_utils.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "./block_diagram.h"
#include "./datafile_in.h"

#define HANNING_ENBW 1.50

extern gfloat plugin_fft_corr;

static bool is_2rx_2tx;

static const gdouble mhz_scale = 1000000.0;
static const gdouble abs_mhz_scale = -1000000.0;
static const gdouble khz_scale = 1000.0;
static const gdouble inv_scale = -1.0;
static char *dac_buf_filename = NULL;

static bool dac_data_loaded = false;

static struct iio_widget glb_widgets[50];
static struct iio_widget tx_widgets[50];
static struct iio_widget rx_widgets[50];
static unsigned int rx1_gain, rx2_gain;
static unsigned int num_glb, num_tx, num_rx;
static unsigned int rx_lo, tx_lo;
static unsigned int rx_sample_freq, tx_sample_freq;

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

/* Widgets for Transmitter Settings */
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
static gboolean plugin_detached;

static char last_fir_filter[PATH_MAX];

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
	char *buf = NULL;
	float rates[6];
	char tmp[160];
	int ret;

	set_dev_paths("ad9361-phy");
	ret = read_devattr("ensm_mode", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(ensm_mode), buf);
	else
		gtk_label_set_text(GTK_LABEL(ensm_mode), "<error>");
	if (buf) {
		free(buf);
		buf = NULL;
	}

	ret = read_devattr("calib_mode", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(calib_mode), buf);
	else
		gtk_label_set_text(GTK_LABEL(calib_mode), "<error>");
	if (buf) {
		free(buf);
		buf = NULL;
	}

	ret = read_devattr("trx_rate_governor", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(trx_rate_governor), buf);
	else
		gtk_label_set_text(GTK_LABEL(trx_rate_governor), "<error>");
	if (buf) {
		free(buf);
		buf = NULL;
	}

	ret = read_devattr("in_voltage0_gain_control_mode", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(rx_gain_control_rx1), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx_gain_control_rx1), "<error>");
	if (buf)
		free(buf);

	if (is_2rx_2tx) {
		ret = read_devattr("in_voltage1_gain_control_mode", &buf);
		if (ret >= 0)
			gtk_label_set_text(GTK_LABEL(rx_gain_control_rx2), buf);
		else
			gtk_label_set_text(GTK_LABEL(rx_gain_control_rx2), "<error>");
		if (buf)
			free(buf);
	}

	ret = read_devattr("rx_path_rates", &buf);
	if (ret >= 0) {
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
	if (buf)
		free(buf);

	ret = read_devattr("tx_path_rates", &buf);
	if (ret >= 0) {
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
	if (buf)
		free(buf);

	iio_widget_update(&rx_widgets[rx1_gain]);
	iio_widget_update(&rx_widgets[rx2_gain]);

}

static void sample_frequency_changed_cb(void)
{
	glb_settings_update_labels();
	rx_update_labels();
}

static void rssi_update_labels(void)
{
	char *buf = NULL;
	int ret;

	set_dev_paths("ad9361-phy");
	ret = read_devattr("in_voltage0_rssi", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(rx1_rssi), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx1_rssi), "<error>");
	if (buf) {
		free(buf);
		buf = NULL;
	}

	if (!is_2rx_2tx)
		return;

	ret = read_devattr("in_voltage1_rssi", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(rx2_rssi), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx2_rssi), "<error>");
	if (buf) {
		free(buf);
		buf = NULL;
	}

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
	bool rx, tx, rxtx;

	set_dev_paths("ad9361-phy");
	read_devattr_bool("in_voltage_filter_fir_en", &rx);
	read_devattr_bool("out_voltage_filter_fir_en", &tx);
	read_devattr_bool("in_out_voltage_filter_fir_en", &rxtx);

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

	set_dev_paths("ad9361-phy");

	if (rxtx) {
		write_devattr("in_out_voltage_filter_fir_en", "1");
	} else if (disable) {
		write_devattr("in_out_voltage_filter_fir_en", "0");
	} else {
		write_devattr("out_voltage_filter_fir_en", tx ? "1" : "0");
		write_devattr("in_voltage_filter_fir_en", rx ? "1" : "0");
	}

	filter_fir_update();
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

static void fastlock_clicked(GtkButton *btn, gpointer data)
{
	int profile;

	switch ((int)data) {
		case 1: /* RX Store */
			iio_widget_save(&rx_widgets[rx_lo]);
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(rx_fastlock_profile));
			set_dev_paths("ad9361-phy");
			write_devattr_int("out_altvoltage0_RX_LO_fastlock_store", profile);
			break;
		case 2: /* TX Store */
			iio_widget_save(&tx_widgets[tx_lo]);
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile));
			set_dev_paths("ad9361-phy");
			write_devattr_int("out_altvoltage1_TX_LO_fastlock_store", profile);
			break;
		case 3: /* RX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(rx_fastlock_profile));
			set_dev_paths("ad9361-phy");
			write_devattr_int("out_altvoltage0_RX_LO_fastlock_recall", profile);
			iio_widget_update(&rx_widgets[rx_lo]);
			break;
		case 4: /* TX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile));
			set_dev_paths("ad9361-phy");
			write_devattr_int("out_altvoltage1_TX_LO_fastlock_recall", profile);
			iio_widget_update(&tx_widgets[tx_lo]);
			break;
	}
}

static void load_fir_filter(const char *file_name)
{
	char str[4096];
	int ret;

	set_dev_paths("ad9361-phy");
	sprintf(str, "cat %s > %s/filter_fir_config ", file_name, dev_name_dir());
	ret = system(str);
	if (ret < 0)
		fprintf(stderr, "FIR filter config failed\n");
	else
		strcpy(last_fir_filter, file_name);
}

void filter_fir_config_file_set_cb (GtkFileChooser *chooser, gpointer data)
{
	char *file_name = gtk_file_chooser_get_filename(chooser);

	load_fir_filter(file_name);
}

static void process_dac_buffer_file (const char *file_name)
{
	int ret, fd, size = 0;
	struct stat st;
	char *buf = NULL;
	FILE *infile;

	ret = analyse_wavefile(file_name, &buf, &size, is_2rx_2tx ? 2 : 1);
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

	set_dev_paths("cf-ad9361-dds-core-lpc");
	write_devattr_int("buffer/enable", 0);

	fd = iio_buffer_open(false, 0);
	if (fd < 0) {
		free(buf);
		return;
	}

	ret = write(fd, buf, size);
	if (ret != size) {
		fprintf(stderr, "Loading waveform failed %d\n", ret);
	}

	close(fd);
	free(buf);

	ret = write_devattr_int("buffer/enable", 1);
	if (ret < 0) {
		fprintf(stderr, "Failed to enable buffer: %d\n", ret);
	}

	dac_data_loaded = true;

	if (dac_buf_filename)
		free(dac_buf_filename);
	dac_buf_filename = malloc(strlen(file_name) + 1);
	strcpy(dac_buf_filename, file_name);

}

static void dac_buffer_config_file_set_cb (GtkFileChooser *chooser, gpointer data)
{
	char *file_name = gtk_file_chooser_get_filename(chooser);
	if (file_name)
		process_dac_buffer_file((const char *)file_name);
}

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
	gint ch1 = TX1_T1_I + (((int)channel - 1) * 4);
	gint ch2 = TX1_T2_I + (((int)channel - 1) * 4);

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

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[(int)channel]))) {
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
	gint ch1 = TX1_T1_I + (((int)channel - 1) * 4);
	gint ch2 = TX1_T2_I + (((int)channel - 1) * 4);

	gdouble freq1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_freq[ch1]));
	gdouble freq2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds_freq[ch2]));

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[(int)channel]))) {
		case DDS_ONE_TONE:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_freq[ch1 + 2]), freq1);
			break;
		case DDS_TWO_TONE:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_freq[ch1 + 2]), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds_freq[ch2 + 2]), freq2);
			break;
		default:
			printf("%s: error : %i\n", __func__,
					gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[(int)channel])));
			break;
	}

	dds_locked_phase_cb(NULL, channel);
}


static void dds_locked_scale_cb(GtkComboBoxText *box, gpointer channel)
{
	gint ch1 = TX1_T1_I + (((int)channel - 1) * 4);
	gint ch2 = TX1_T2_I + (((int)channel - 1) * 4);

	gint scale1 = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[ch1]));
	gint scale2 = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[ch2]));

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode_tx[(int)channel]))) {
		case DDS_ONE_TONE:
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[ch1 + 2]), scale1);
			break;
		case DDS_TWO_TONE:
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[ch1 + 2]), scale1);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[ch2 + 2]), scale2);
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

static void enable_dds(bool on_off)
{
	int ret;

	set_dev_paths("cf-ad9361-dds-core-lpc");
	write_devattr_int("out_altvoltage0_TX1_I_F1_raw", on_off ? 1 : 0);

	if (on_off || dac_data_loaded) {
		ret = write_devattr_int("buffer/enable", !on_off);
		if (ret < 0) {
			fprintf(stderr, "Failed to enable buffer: %d\n", ret);

		}
	}
}

#define IIO_SPIN_SIGNAL "value-changed"
#define IIO_COMBO_SIGNAL "changed"

static void manage_dds_mode(GtkComboBox *box, gint channel)
{
	gint active, i, start, end;
	static gint *mag = NULL;

	if (!mag) {
		mag = g_renew(gint, mag, 9);
		mag[TX1_T1_I] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX1_T1_I]));
		mag[TX1_T2_I] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX1_T2_I]));
		mag[TX1_T1_Q] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX1_T1_Q]));
		mag[TX1_T2_Q] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX1_T2_Q]));
		mag[TX2_T1_I] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX2_T1_I]));
		mag[TX2_T2_I] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX2_T2_I]));
		mag[TX2_T1_Q] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX2_T1_Q]));
		mag[TX2_T2_Q] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX2_T2_Q]));
		active = mag[1];
		while (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX1_T1_I])) >= 0) {
			active++;
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[TX1_T1_I]), active);
		}
		mag[TX_OFF] = active - 1;
		gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[TX1_T1_I]), mag[TX1_T1_I]);
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
		if (channel == 1) {
		 	start = TX1_T1_I;
			end = TX1_T2_Q;
		} else {
			start = TX2_T1_I;
			end = TX2_T2_Q;
		}
		for (i = start; i <= end; i++) {
			if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[i])) != mag[TX_OFF]) {
				 mag[i] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[i]));
				 gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[i]), mag[TX_OFF]);
			}
 
		}
		start = 0;
		for (i = TX1_T1_I; i <= TX2_T2_Q; i++) {
			if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[i])) !=  mag[TX_OFF])
				start++;
		}
		if (!start)
			enable_dds(false);
		else
			enable_dds(true);

		gtk_widget_hide(channel_I_tx[channel]);
		gtk_widget_hide(channel_Q_tx[channel]);
		gtk_widget_hide(dac_buffer);

		break;
	case DDS_ONE_TONE:
		enable_dds(true);
		gtk_widget_hide(dac_buffer);
		gtk_label_set_markup(GTK_LABEL(dds_I_TX_l[channel]),"<b>Single Tone</b>");

		gtk_widget_show_all(channel_I_tx[channel]);
		gtk_widget_hide(channel_I_tone2_tx[channel]);
		gtk_widget_hide(channel_Q_tx[channel]);

		if (channel == 1) {
			if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX1_T1_I])) == mag[TX_OFF]) {
				gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[TX1_T1_I]), mag[TX1_T1_I]);
				gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[TX1_T1_Q]), mag[TX1_T1_Q]);
			}

			if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX1_T2_I])) != mag[TX_OFF]) {
				mag[TX1_T2_I] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX1_T2_I]));
				gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[TX1_T2_I]), mag[TX_OFF]);
				mag[TX1_T2_Q] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX1_T2_Q]));
				gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[TX1_T2_Q]), mag[TX_OFF]);
			}

			start = TX1_T1_I;
			end = TX1_T2_I;
		} else {
			if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX2_T1_I])) == mag[TX_OFF]) {
				gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[TX2_T1_I]), mag[TX2_T1_I]);
				gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[TX2_T1_Q]), mag[TX2_T1_Q]);
			}

			if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX2_T2_I])) != mag[TX_OFF]) {
				mag[TX2_T2_I] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX2_T2_I]));
				gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[TX2_T2_I]), mag[TX_OFF]);
				mag[TX2_T2_Q] = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[TX2_T2_Q]));
				gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[TX2_T2_Q]), mag[TX_OFF]);
			}

			start = TX2_T1_I;
			end = TX2_T2_I;
		}

		/* Connect the widgets that are showing */
		if (!dds_scale_hid[start])
			dds_scale_hid[start] = g_signal_connect(dds_scale[start], IIO_COMBO_SIGNAL,
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
			if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[i])) == mag[TX_OFF]) {
				gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[i]), mag[i]);
			}
		}

		if (channel == 1)
			end = TX1_T2_I;
		else 
			end = TX2_T2_I;

		for (i = start; i <= end; i++) {
			if (!dds_scale_hid[i])
				dds_scale_hid[i] = g_signal_connect(dds_scale[i], IIO_COMBO_SIGNAL,
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
			if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_scale[i])) == mag[TX_OFF])
				 gtk_combo_box_set_active(GTK_COMBO_BOX(dds_scale[i]), mag[i]);

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
		enable_dds(false);
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
int channel_combination_check(struct iio_channel_info *channels, int ch_count, char **ch_names)
{
	bool consecutive_ch = FALSE;
	int i, k = 0;

	for (i = 0; i < ch_count; i++)
		if (channels[i].enabled) {
			ch_names[k++] = channels[i].name;
			if (i > 0)
				if (channels[i - 1].enabled) {
					consecutive_ch = TRUE;
					break;
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
	set_dev_paths(iio_w->device_name);
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

static int fmcomms2_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *fmcomms2_panel;
	bool shared_scale_available;
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

	is_2rx_2tx = iio_devattr_exists("ad9361-phy", "in_voltage1_hardwaregain");

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
		"ad9361-phy", "ensm_mode", "ensm_mode_available",
		ensm_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		"ad9361-phy", "calib_mode", "calib_mode_available",
		calib_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		"ad9361-phy", "trx_rate_governor", "trx_rate_governor_available",
		trx_rate_governor_available, NULL);

	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		"ad9361-phy", "dcxo_tune_coarse", builder, "dcxo_coarse_tune",
		0);
	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		"ad9361-phy", "dcxo_tune_fine", builder, "dcxo_fine_tune",
		0);

	/* Receive Chain */

	iio_combo_box_init(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage0_gain_control_mode",
		"in_voltage_gain_control_mode_available",
		rx_gain_control_modes_rx1, NULL);

	iio_combo_box_init(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage0_rf_port_select",
		"in_voltage_rf_port_select_available",
		rf_port_select_rx, NULL);

	if (is_2rx_2tx)
		iio_combo_box_init(&rx_widgets[num_rx++],
			"ad9361-phy", "in_voltage1_gain_control_mode",
			"in_voltage_gain_control_mode_available",
			rx_gain_control_modes_rx2, NULL);
	rx1_gain = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage0_hardwaregain", builder,
		"hardware_gain_rx1", NULL);

	if (is_2rx_2tx) {
		rx2_gain = num_rx;
		iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"ad9361-phy", "in_voltage1_hardwaregain", builder,
			"hardware_gain_rx2", NULL);
	}
	rx_sample_freq = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage_sampling_frequency", builder,
		"sampling_freq_rx", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage_rf_bandwidth", builder, "rf_bandwidth_rx",
		&mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);
	rx_lo = num_rx;
	iio_spin_button_s64_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "out_altvoltage0_RX_LO_frequency", builder,
		"rx_lo_freq", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage_quadrature_tracking_en", builder,
		"quad", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage_rf_dc_offset_tracking_en", builder,
		"rfdc", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage_bb_dc_offset_tracking_en", builder,
		"bbdc", 0);

	iio_spin_button_init_from_builder(&rx_widgets[num_rx],
		"cf-ad9361-lpc", "in_voltage1_calibphase",
		builder, "rx1_phase_rotation", NULL);
	iio_spin_button_add_progress(&rx_widgets[num_rx++]);


	/* Transmit Chain */

	iio_combo_box_init(&tx_widgets[num_tx++],
		"ad9361-phy", "out_voltage0_rf_port_select",
		"out_voltage_rf_port_select_available",
		rf_port_select_tx, NULL);

	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
		"ad9361-phy", "out_voltage0_hardwaregain", builder,
		"hardware_gain_tx1", &inv_scale);

	if (is_2rx_2tx)
		iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad9361-phy", "out_voltage1_hardwaregain", builder,
			"hardware_gain_tx2", &inv_scale);
	tx_sample_freq = num_tx;
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad9361-phy", "out_voltage_sampling_frequency", builder,
		"sampling_freq_tx", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad9361-phy", "out_voltage_rf_bandwidth", builder,
		"rf_bandwidth_tx", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	tx_lo = num_tx;
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
		"ad9361-phy", "out_altvoltage1_TX_LO_frequency", builder,
		"tx_lo_freq", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	shared_scale_available = iio_devattr_exists("cf-ad9361-dds-core-lpc",
			"out_altvoltage_scale_available");

	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage0_TX1_I_F1_frequency",
			dds_freq[TX1_T1_I], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage1_TX1_I_F2_frequency",
			dds_freq[TX1_T2_I], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage2_TX1_Q_F1_frequency",
			dds_freq[TX1_T1_Q], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage3_TX1_Q_F2_frequency",
			dds_freq[TX1_T2_Q], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage4_TX2_I_F1_frequency",
			dds_freq[TX2_T1_I], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage5_TX2_I_F2_frequency",
			dds_freq[TX2_T2_I], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage6_TX2_Q_F1_frequency",
			dds_freq[TX2_T1_Q], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage7_TX2_Q_F2_frequency",
			dds_freq[TX2_T2_Q], &abs_mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage0_TX1_I_F1_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX1_I_F1_scale_available",
			dds_scale[TX1_T1_I], compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage1_TX1_I_F2_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX1_I_F2_scale_available",
			dds_scale[TX1_T2_I], compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage2_TX1_Q_F1_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX1_Q_F1_scale_available",
			dds_scale[TX1_T1_Q], compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage3_TX1_Q_F2_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX1_Q_F2_scale_available",
			dds_scale[TX1_T2_Q], compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage4_TX2_I_F1_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX2_I_F1_scale_available",
			dds_scale[TX2_T1_I], compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage5_TX2_I_F2_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX2_I_F2_scale_available",
			dds_scale[TX2_T2_I], compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage6_TX2_Q_F1_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX2_Q_F1_scale_available",
			dds_scale[TX2_T1_Q], compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage7_TX2_Q_F2_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX2_Q_F2_scale_available",
			dds_scale[TX2_T2_Q], compare_gain);

	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage0_TX1_I_F1_phase",
			dds_phase[TX1_T1_I], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage1_TX1_I_F2_phase",
			dds_phase[TX1_T2_I], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage2_TX1_Q_F1_phase",
			dds_phase[TX1_T1_Q], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage3_TX1_Q_F2_phase",
			dds_phase[TX1_T2_Q], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage4_TX2_I_F1_phase",
			dds_phase[TX2_T1_I], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage5_TX2_I_F2_phase",
			dds_phase[TX2_T2_I], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage6_TX2_Q_F1_phase",
			dds_phase[TX2_T1_Q], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage7_TX2_Q_F2_phase",
			dds_phase[TX2_T2_Q], &khz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	/* Signals connect */
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
		glb_settings_update_labels);
	iio_spin_button_set_on_complete_function(&tx_widgets[tx_sample_freq],
		glb_settings_update_labels);
	iio_spin_button_set_on_complete_function(&tx_widgets[rx_lo],
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

	manage_dds_mode(GTK_COMBO_BOX(dds_mode_tx[1]), 1);
	manage_dds_mode(GTK_COMBO_BOX(dds_mode_tx[2]), 2);

	add_ch_setup_check_fct("cf-ad9361-lpc", channel_combination_check);
	plugin_fft_corr = 20 * log10(1/sqrt(HANNING_ENBW));

	block_diagram_init(builder, "fmcomms2.svg");

	this_page = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), fmcomms2_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), fmcomms2_panel, "FMComms2");
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(filter_fir_config), OSC_FILTER_FILE_PATH);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dac_buffer), OSC_WAVEFORM_FILE_PATH);

	if (!is_2rx_2tx) {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame7")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame10")));
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
	"ad9361-phy.trx_rate_governor",
	"ad9361-phy.dcxo_tune_coarse",
	"ad9361-phy.dcxo_tune_fine",
	"ad9361-phy.ensm_mode",
	"ad9361-phy.in_voltage0_rf_port_select",
	"ad9361-phy.in_voltage0_gain_control_mode",
	"ad9361-phy.in_voltage0_hardwaregain",
	"ad9361-phy.in_voltage1_gain_control_mode",
	"ad9361-phy.in_voltage1_hardwaregain",
	"ad9361-phy.in_voltage_bb_dc_offset_tracking_en",
	"ad9361-phy.in_voltage_quadrature_tracking_en",
	"ad9361-phy.in_voltage_rf_dc_offset_tracking_en",
	"ad9361-phy.out_voltage0_rf_port_select",
	"ad9361-phy.out_altvoltage0_RX_LO_frequency",
	"ad9361-phy.out_altvoltage1_TX_LO_frequency",
	"ad9361-phy.out_voltage0_hardwaregain",
	"ad9361-phy.out_voltage1_hardwaregain",
	"ad9361-phy.out_voltage_sampling_frequency",
	"ad9361-phy.in_voltage_rf_bandwidth",
	"ad9361-phy.out_voltage_rf_bandwidth",
	"load_fir_filter_file",
	"ad9361-phy.in_voltage_filter_fir_en",
	"ad9361-phy.out_voltage_filter_fir_en",
	"ad9361-phy.in_out_voltage_filter_fir_en",
	"global_settings_show",
	"tx_show",
	"rx_show",
	"fpga_show",
	"dds_mode_tx1",
	"dds_mode_tx2",
	"dac_buf_filename",
	"cf-ad9361-dds-core-lpc.out_altvoltage0_TX1_I_F1_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage0_TX1_I_F1_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage0_TX1_I_F1_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage0_TX1_I_F1_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage1_TX1_I_F2_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage1_TX1_I_F2_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage1_TX1_I_F2_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage1_TX1_I_F2_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage2_TX1_Q_F1_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage2_TX1_Q_F1_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage2_TX1_Q_F1_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage2_TX1_Q_F1_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage3_TX1_Q_F2_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage3_TX1_Q_F2_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage3_TX1_Q_F2_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage3_TX1_Q_F2_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage4_TX2_I_F1_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage4_TX2_I_F1_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage4_TX2_I_F1_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage4_TX2_I_F1_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage5_TX2_I_F2_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage5_TX2_I_F2_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage5_TX2_I_F2_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage5_TX2_I_F2_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage6_TX2_Q_F1_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage6_TX2_Q_F1_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage6_TX2_Q_F1_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage6_TX2_Q_F1_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage7_TX2_Q_F2_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage7_TX2_Q_F2_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage7_TX2_Q_F2_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage7_TX2_Q_F2_scale",
	SYNC_RELOAD,
	NULL,
};

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static bool fmcomms2_identify(void)
{
	return !set_dev_paths("ad9361-phy");
}

struct osc_plugin plugin = {
	.name = "FMComms2/3/4",
	.identify = fmcomms2_identify,
	.init = fmcomms2_init,
	.save_restore_attribs = fmcomms2_sr_attribs,
	.handle_item = handle_item,
	.update_active_page = update_active_page,
};
