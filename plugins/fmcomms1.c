/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
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

#include "../iio_widget.h"
#include "../iio_utils.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "../osc.h"

static const gdouble mhz_scale = 1000000.0;
static const gdouble khz_scale = 1000.0;

#define VERSION_SUPPORTED 0
static struct fmcomms1_calib_data *cal_data;

static GtkWidget *vga_gain0, *vga_gain1;
static GtkAdjustment *adj_gain0, *adj_gain1;

static GtkWidget *dds_mode;
static GtkWidget *dds1_freq, *dds2_freq, *dds3_freq, *dds4_freq;
static GtkWidget *dds1_scale, *dds2_scale, *dds3_scale, *dds4_scale;
static GtkWidget *dds1_phase, *dds2_phase, *dds3_phase, *dds4_phase;
static GtkAdjustment *adj1_freq, *adj2_freq, *adj3_freq, *adj4_freq;
static GtkWidget *dds1_freq_l, *dds2_freq_l, *dds3_freq_l, *dds4_freq_l;
static GtkWidget *dds1_scale_l, *dds2_scale_l, *dds3_scale_l, *dds4_scale_l;
static GtkWidget *dds1_phase_l, *dds2_phase_l, *dds3_phase_l, *dds4_phase_l;
static GtkWidget *dds_I_l, *dds_I1_l, *dds_I2_l;
static GtkWidget *dds_Q_l, *dds_Q1_l, *dds_Q2_l;
static gulong dds1_freq_hid = 0, dds2_freq_hid = 0;
static gulong dds1_scale_hid = 0, dds2_scale_hid = 0;
static gulong dds1_phase_hid = 0, dds2_phase_hid = 0;

static GtkWidget *avg_I, *avg_Q;
static GtkWidget *span_I, *span_Q;
static GtkWidget *radius_IQ, *angle_IQ;

static GtkWidget *load_eeprom;

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static struct iio_widget cal_widgets[100];
static unsigned int num_tx, num_rx, num_cal;

static const char *adc_freq_device;
static const char *adc_freq_file;

static int num_tx_pll, num_rx_pll;

typedef struct _Dialogs Dialogs;
struct _Dialogs
{
	GtkWidget *calibrate;
};
static Dialogs dialogs;

static int kill_thread;
static int fmcomms1_cal_eeprom(void);

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

static void cal_update_values(void)
{
	iio_update_widgets(cal_widgets, num_cal);
}

static void cal_save_values(void)
{
	iio_save_widgets(cal_widgets, num_cal);
	iio_update_widgets(cal_widgets, num_cal);
}

static void save_button_clicked(GtkButton *btn, gpointer data)
{
	iio_save_widgets(tx_widgets, num_tx);
	iio_save_widgets(rx_widgets, num_rx);
	rx_update_labels();
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

double fract_to_float(unsigned short val)
{
	double ret = 0;

	if (val & 0x8000) {
		ret = 1.0000;
		val &= ~0x8000;
	}

	ret += (double)val / 0x8000;

	return ret;
}

struct fmcomms1_calib_data *find_entry(struct fmcomms1_calib_data *ptr, unsigned f)
{
	struct fmcomms1_calib_data *data;
	int ind = 0;
	int delta, gindex = 0;
	int min_delta = 2147483647;

	data = ptr;

	do {
		if (data->adi_magic0 != ADI_MAGIC_0 || data->adi_magic1 != ADI_MAGIC_1) {
			fprintf (stderr, "invalid magic detected\n");
			return NULL;
		}
		if (data->version != ADI_VERSION(VERSION_SUPPORTED)) {
			fprintf (stderr, "unsupported version detected %c\n", data->version);
			return NULL;
		}


		if (f) {
			delta = abs(f - data->cal_frequency_MHz);
			if (delta < min_delta) {
				gindex = ind;
				min_delta = delta;
			}

		}
		ind++;
	} while (data++->next);

	return &ptr[gindex];
}

void store_entry_hw(struct fmcomms1_calib_data *data, unsigned tx, unsigned rx)
{
	if (!data)
		return;

	if (tx) {
		set_dev_paths("cf-ad9122-core-lpc");
		write_devattr_slonglong("out_voltage0_calibbias", data->i_dac_offset);
		write_devattr_slonglong("out_voltage0_calibscale", data->i_dac_fs_adj);
		write_devattr_slonglong("out_voltage0_phase", data->i_phase_adj);
		write_devattr_slonglong("out_voltage1_calibbias", data->q_dac_offset);
		write_devattr_slonglong("out_voltage1_calibscale", data->q_dac_fs_adj);
		write_devattr_slonglong("out_voltage1_phase", data->q_phase_adj);
		tx_update_values();
	}

	if (rx) {
		set_dev_paths("cf-ad9643-core-lpc");
		write_devattr_slonglong("in_voltage0_calibbias", data->i_adc_offset_adj);
		write_devattr_double("in_voltage0_calibscale", fract_to_float(data->i_adc_gain_adj));
		write_devattr_slonglong("in_voltage1_calibbias", data->q_adc_offset_adj);
		write_devattr_double("in_voltage1_calibscale", fract_to_float(data->q_adc_gain_adj));
		rx_update_values();
	}
}

static gdouble pll_get_freq(struct iio_widget *widget)
{
	gdouble freq;

	gdouble scale = widget->priv ? *(gdouble *)widget->priv : 1.0;
	freq = gtk_spin_button_get_value(GTK_SPIN_BUTTON (widget->widget));
	freq *= scale;

	return freq;
}


static void dds_locked_freq_cb(GtkToggleButton *btn, gpointer data)
{
	gdouble freq1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_freq));
	gdouble freq2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds2_freq));

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode))) {
		case 1:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds2_freq), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_freq), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_freq), freq1);
			break;
		case 2:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_freq), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_freq), freq2);
			break;
		default:
			printf("%s: error\n", __func__);
			break;
	}
}


static void dds_locked_phase_cb(GtkToggleButton *btn, gpointer data)
{

	gdouble phase1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_phase));
	gdouble phase2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds2_phase));

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode))) {
		case 1:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds2_phase), phase1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_phase), phase1 + 90.0);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_phase), phase1 + 90.0);
			break;
		case 2:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_phase), phase1 + 90.0);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_phase), phase2 + 90.0);
			break;
		default:
			printf("%s: error\n", __func__);
			break;
	}
}
static void dds_locked_scale_cb(GtkComboBoxText *box, gpointer data)
{
	gint scale1 = gtk_combo_box_get_active(GTK_COMBO_BOX(dds1_scale));
	gint scale2 = gtk_combo_box_get_active(GTK_COMBO_BOX(dds2_scale));

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode))) {
		case 1:
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds2_scale), scale1);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds3_scale), scale1);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds4_scale), scale1);
			break;
		case 2:
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds3_scale), scale1);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds4_scale), scale2);
			break;
		default:
			printf("%s: error\n", __func__);
			break;
	}
}

static void gain_amp_locked_cb(GtkToggleButton *btn, gpointer data)
{

	if(gtk_toggle_button_get_active(btn)) {
		gdouble tmp;
		tmp = gtk_spin_button_get_value(GTK_SPIN_BUTTON(vga_gain0));
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(vga_gain1), tmp);
		gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(vga_gain1), adj_gain0);
	} else {
		gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(vga_gain1), adj_gain1);
	}
}

static void load_cal_eeprom()
{
	gdouble freq;
	freq = pll_get_freq(&tx_widgets[num_tx_pll]);
	store_entry_hw(find_entry(cal_data, (unsigned) (freq / mhz_scale)), 1, 0);

	freq = pll_get_freq(&rx_widgets[num_rx_pll]);
	store_entry_hw(find_entry(cal_data,  (unsigned) (freq / mhz_scale)), 0, 1);
}

void display_cal(void *ptr)
{
	int size, channels, num_samples, i, j;
	int8_t *buf = NULL;
	static gfloat **cooked_data = NULL;
	gfloat max_x, min_x, avg_x;
	gfloat max_y, min_y, avg_y;
	gfloat max_r, min_r, max_theta, min_theta, rad;
	char cbuf[256];

	while (!kill_thread) {
		size = plugin_data_capture_size();
		while (!kill_thread && !capture_function) {
			sleep(1);
		}

		size = plugin_data_capture_size();
		channels = plugin_data_capture_num_active_channels();
		num_samples = size / plugin_data_capture_bytes_per_sample();

		if (size != 0 && channels == 2) {
			buf = g_renew(int8_t, buf, size);
			cooked_data = g_renew(gfloat *, cooked_data, channels);
			for (i = 0; i < channels; i++) {
				cooked_data[i] = g_new(gfloat, num_samples);
				for (j = 0; j < num_samples; j++)
					cooked_data[i][j] = 0.0f;
			}

			if (!buf || !cooked_data) {
				printf("%s : malloc failed\n", __func__);
				kill_thread = 1;
			}

			if (!plugin_data_capture(buf))
				continue;

			pthread_mutex_lock(&mutex);

			plugin_data_capture_demux(buf, cooked_data, size/4, channels);

			avg_x = avg_y = 0.0;
			max_x = max_y = -MAXFLOAT;
			min_x = min_y = MAXFLOAT;

			for (i = 0; i < num_samples; i++) {
				avg_x += cooked_data[0][i];
				avg_y += cooked_data[1][i];
				if (max_x <= cooked_data[0][i])
					max_x = cooked_data[0][i];
				if (min_x >= cooked_data[0][i])
					min_x = cooked_data[0][i];
				if (max_y <= cooked_data[1][i])
					max_y = cooked_data[1][i];
				if (min_y >= cooked_data[1][i])
					min_y = cooked_data[1][i];
			}
			avg_x /= num_samples;
			avg_y /= num_samples;

			max_r = max_theta = -MAXFLOAT;
			min_r = min_theta = MAXFLOAT;

			for (i = 0; i < num_samples; i++) {
				rad = sqrtf((cooked_data[0][i] * cooked_data[0][i]) +
					(cooked_data[1][i] * cooked_data[1][i]));
				if (max_r <= rad) {
					max_r = rad;
					if (cooked_data[1][i])
						max_theta = asinf(cooked_data[0][i]/cooked_data[1][i]);
					else
						max_theta = 0.0f;
				}
				if (min_r >= rad) {
					min_r = rad;
					if (cooked_data[1][i])
						min_theta = asinf(cooked_data[0][i]/cooked_data[1][i]);
					else
						min_theta = 0.0f;
				}
			}

			sprintf(cbuf, "avg: %3.0f | mid : %3.0f", avg_y, (min_y + max_y)/2);
			gtk_label_set_text(GTK_LABEL(avg_I), cbuf);

			sprintf(cbuf, "avg: %3.0f | mid : %3.0f", avg_x, (min_x + max_x)/2);
			gtk_label_set_text(GTK_LABEL(avg_Q), cbuf);

			sprintf(cbuf, "%3.0f <-> %3.0f (%3.0f)", max_y, min_y, (max_y - min_y));
			gtk_label_set_text(GTK_LABEL(span_I), cbuf);

			sprintf(cbuf, "%3.0f <-> %3.0f (%3.0f)", max_x, min_x, (max_x - min_x));
			gtk_label_set_text(GTK_LABEL(span_Q), cbuf);

			sprintf(cbuf, "max: %3.0f | min: %3.0f", max_r, min_r);
			gtk_label_set_text(GTK_LABEL(radius_IQ), cbuf);

			sprintf(cbuf, "max: %0.3f | min: %0.3f", max_theta * 180 / M_PI, min_theta * 180 / M_PI);
			gtk_label_set_text(GTK_LABEL(angle_IQ), cbuf);
		}
	}
	g_free(buf);

	pthread_mutex_unlock(&mutex);
}

G_MODULE_EXPORT void cal_dialog(GtkButton *btn, Dialogs *data)
{
	gint ret;
	pthread_t thread;

	kill_thread = 0;
	pthread_create(&thread, NULL, (void *) &display_cal, NULL);
	gtk_widget_show(dialogs.calibrate);

	if (fmcomms1_cal_eeprom() == 0)
		gtk_widget_hide(load_eeprom);

	do {
		ret = gtk_dialog_run(GTK_DIALOG(dialogs.calibrate));
		switch (ret) {
		case 1:
			load_cal_eeprom();
		case GTK_RESPONSE_APPLY:
			cal_save_values();
			break;
		case GTK_RESPONSE_CLOSE:
		case GTK_RESPONSE_DELETE_EVENT:
			/* Closing */
			break;
		default:
			printf("unhandled event code : %i\n", ret);
			break;
		}

	} while (ret != GTK_RESPONSE_CLOSE &&		/* Clicked on the close button */
		 ret != GTK_RESPONSE_DELETE_EVENT);	/* Clicked on the close icon */

	kill_thread = 1;

	if (!capture_function)
		pthread_mutex_unlock(&mutex);

	pthread_join(thread, NULL);

	gtk_widget_hide(dialogs.calibrate);
}

static void enable_dds(bool on_off)
{
	set_dev_paths("cf-ad9122-core-lpc");
	write_devattr_int("out_altvoltage0_1A_raw", on_off ? 1 : 0);
}

static void manage_dds_mode()
{
	gint active;

	active = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode));
	switch (active) {
	case 0:
		/* Disabled */
		enable_dds(false);
		gtk_widget_hide(dds1_freq);
		gtk_widget_hide(dds2_freq);
		gtk_widget_hide(dds3_freq);
		gtk_widget_hide(dds4_freq);
		gtk_widget_hide(dds1_scale);
		gtk_widget_hide(dds2_scale);
		gtk_widget_hide(dds3_scale);
		gtk_widget_hide(dds4_scale);
		gtk_widget_hide(dds1_phase);
		gtk_widget_hide(dds2_phase);
		gtk_widget_hide(dds3_phase);
		gtk_widget_hide(dds4_phase);
		gtk_widget_hide(dds1_freq_l);
		gtk_widget_hide(dds2_freq_l);
		gtk_widget_hide(dds3_freq_l);
		gtk_widget_hide(dds4_freq_l);
		gtk_widget_hide(dds1_scale_l);
		gtk_widget_hide(dds2_scale_l);
		gtk_widget_hide(dds3_scale_l);
		gtk_widget_hide(dds4_scale_l);
		gtk_widget_hide(dds1_phase_l);
		gtk_widget_hide(dds2_phase_l);
		gtk_widget_hide(dds3_phase_l);
		gtk_widget_hide(dds4_phase_l);
		gtk_widget_hide(dds_I_l);
		gtk_widget_hide(dds_I1_l);
		gtk_widget_hide(dds_I2_l);
		gtk_widget_hide(dds_Q_l);
		gtk_widget_hide(dds_Q1_l);
		gtk_widget_hide(dds_Q2_l);
		break;
	case 1:
		/* One tone */
		enable_dds(true);
		gtk_widget_show(dds1_freq);
		gtk_widget_hide(dds2_freq);
		gtk_widget_hide(dds3_freq);
		gtk_widget_hide(dds4_freq);
		gtk_widget_show(dds1_scale);
		gtk_widget_hide(dds2_scale);
		gtk_widget_hide(dds3_scale);
		gtk_widget_hide(dds4_scale);
		gtk_widget_hide(dds1_phase);
		gtk_widget_hide(dds2_phase);
		gtk_widget_hide(dds3_phase);
		gtk_widget_hide(dds4_phase);
		gtk_widget_show(dds1_freq_l);
		gtk_widget_hide(dds2_freq_l);
		gtk_widget_hide(dds3_freq_l);
		gtk_widget_hide(dds4_freq_l);
		gtk_widget_show(dds1_scale_l);
		gtk_widget_hide(dds2_scale_l);
		gtk_widget_hide(dds3_scale_l);
		gtk_widget_hide(dds4_scale_l);
		gtk_widget_hide(dds1_phase_l);
		gtk_widget_hide(dds2_phase_l);
		gtk_widget_hide(dds3_phase_l);
		gtk_widget_hide(dds4_phase_l);
		gtk_widget_show(dds_I_l);
		gtk_label_set_text(GTK_LABEL(dds_I_l), "Single Tone");
		gtk_widget_hide(dds_I1_l);
		gtk_widget_hide(dds_I2_l);
		gtk_widget_hide(dds_Q_l);
		gtk_widget_hide(dds_Q1_l);
		gtk_widget_hide(dds_Q2_l);

		if (!dds1_scale_hid)
			dds1_scale_hid = g_signal_connect(dds1_scale , "changed",
					G_CALLBACK(dds_locked_scale_cb), NULL);

		if (!dds1_freq_hid)
			dds1_freq_hid = g_signal_connect(dds1_freq , "changed",
					G_CALLBACK(dds_locked_freq_cb), NULL);

		if (!dds1_phase_hid)
			dds1_freq_hid = g_signal_connect(dds1_freq , "changed",
					G_CALLBACK(dds_locked_phase_cb), NULL);

		if (dds2_scale_hid) {
			g_signal_handler_disconnect(dds2_scale, dds2_scale_hid);
			dds2_scale_hid = 0;
		}

		if (dds2_freq_hid) {
			g_signal_handler_disconnect(dds2_freq, dds2_freq_hid);
			dds2_freq_hid = 0;
		}

		if (dds1_phase_hid) {
			g_signal_handler_disconnect(dds1_phase, dds1_phase_hid);
			dds1_phase_hid = 0;
		}
		if (dds2_phase_hid) {
			g_signal_handler_disconnect(dds2_phase, dds2_phase_hid);
			dds2_phase_hid = 0;
		}

		gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds1_phase), 0.0);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds2_phase), 0.0);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_phase), 90.0);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_phase), 90.0);


		break;
	case 2:
		/* Two tones */
		enable_dds(true);
		gtk_widget_show(dds1_freq);
		gtk_widget_show(dds2_freq);
		gtk_widget_hide(dds3_freq);
		gtk_widget_hide(dds4_freq);
		gtk_widget_show(dds1_scale);
		gtk_widget_show(dds2_scale);
		gtk_widget_hide(dds3_scale);
		gtk_widget_hide(dds4_scale);
		gtk_widget_show(dds1_phase);
		gtk_widget_show(dds2_phase);
		gtk_widget_hide(dds3_phase);
		gtk_widget_hide(dds4_phase);
		gtk_widget_show(dds1_freq_l);
		gtk_widget_show(dds2_freq_l);
		gtk_widget_hide(dds3_freq_l);
		gtk_widget_hide(dds4_freq_l);
		gtk_widget_show(dds1_scale_l);
		gtk_widget_show(dds2_scale_l);
		gtk_widget_hide(dds3_scale_l);
		gtk_widget_hide(dds4_scale_l);
		gtk_widget_show(dds1_phase_l);
		gtk_widget_show(dds2_phase_l);
		gtk_widget_hide(dds3_phase_l);
		gtk_widget_hide(dds4_phase_l);
		gtk_widget_show(dds_I_l);
		gtk_label_set_text(GTK_LABEL(dds_I_l), "Two Tones");
		gtk_widget_show(dds_I1_l);
		gtk_widget_show(dds_I2_l);
		gtk_widget_hide(dds_Q_l);
		gtk_widget_hide(dds_Q1_l);
		gtk_widget_hide(dds_Q2_l);

		if (!dds1_scale_hid)
			dds1_scale_hid = g_signal_connect(dds1_scale , "changed",
					G_CALLBACK(dds_locked_scale_cb), NULL);
		if (!dds2_scale_hid)
			dds2_scale_hid = g_signal_connect(dds2_scale , "changed",
					G_CALLBACK(dds_locked_scale_cb), NULL);

		if (!dds1_freq_hid)
			dds1_freq_hid = g_signal_connect(dds1_freq , "changed",
					G_CALLBACK(dds_locked_freq_cb), NULL);
		if (!dds2_freq_hid)
			dds2_freq_hid = g_signal_connect(dds2_freq , "changed",
					G_CALLBACK(dds_locked_freq_cb), NULL);

		if (!dds1_phase_hid)
			dds1_phase_hid = g_signal_connect(dds1_phase , "changed",
					G_CALLBACK(dds_locked_phase_cb), NULL);
		if (!dds2_phase_hid)
			dds2_phase_hid = g_signal_connect(dds2_phase , "changed",
					G_CALLBACK(dds_locked_phase_cb), NULL);

		break;
	case 3:
		/* Independant/Individual control */
		enable_dds(true);
		gtk_widget_show(dds1_freq);
		gtk_widget_show(dds2_freq);
		gtk_widget_show(dds3_freq);
		gtk_widget_show(dds4_freq);
		gtk_widget_show(dds1_scale);
		gtk_widget_show(dds2_scale);
		gtk_widget_show(dds3_scale);
		gtk_widget_show(dds4_scale);
		gtk_widget_show(dds1_phase);
		gtk_widget_show(dds2_phase);
		gtk_widget_show(dds3_phase);
		gtk_widget_show(dds4_phase);
		gtk_widget_show(dds1_freq_l);
		gtk_widget_show(dds2_freq_l);
		gtk_widget_show(dds3_freq_l);
		gtk_widget_show(dds4_freq_l);
		gtk_widget_show(dds1_scale_l);
		gtk_widget_show(dds2_scale_l);
		gtk_widget_show(dds3_scale_l);
		gtk_widget_show(dds4_scale_l);
		gtk_widget_show(dds1_phase_l);
		gtk_widget_show(dds2_phase_l);
		gtk_widget_show(dds3_phase_l);
		gtk_widget_show(dds4_phase_l);
		gtk_widget_show(dds_I_l);
		gtk_label_set_text(GTK_LABEL(dds_I_l), "Channel I");
		gtk_widget_show(dds_I1_l);
		gtk_widget_show(dds_I2_l);
		gtk_widget_show(dds_Q_l);
		gtk_widget_show(dds_Q1_l);
		gtk_widget_show(dds_Q2_l);


		if (dds1_scale_hid) {
			g_signal_handler_disconnect(dds1_scale, dds1_scale_hid);
			dds1_scale_hid = 0;
		}
		if (dds2_scale_hid) {
			g_signal_handler_disconnect(dds2_scale, dds2_scale_hid);
			dds2_scale_hid = 0;
		}

		if (dds1_freq_hid) {
			g_signal_handler_disconnect(dds1_freq, dds1_freq_hid);
			dds1_freq_hid = 0;
		}
		if (dds2_freq_hid) {
			g_signal_handler_disconnect(dds2_freq, dds2_freq_hid);
			dds2_freq_hid = 0;
		}

		if (dds1_phase_hid) {
			g_signal_handler_disconnect(dds1_phase, dds1_phase_hid);
			dds1_phase_hid = 0;
		}

		if (dds2_phase_hid) {
			g_signal_handler_disconnect(dds2_phase, dds2_phase_hid);
			dds2_phase_hid = 0;
		}

		break;
	case 4:
		/* Buffer */
		enable_dds(false);
		gtk_widget_hide(dds1_freq);
		gtk_widget_hide(dds2_freq);
		gtk_widget_hide(dds3_freq);
		gtk_widget_hide(dds4_freq);
		gtk_widget_hide(dds1_scale);
		gtk_widget_hide(dds2_scale);
		gtk_widget_hide(dds3_scale);
		gtk_widget_hide(dds4_scale);
		gtk_widget_hide(dds1_phase);
		gtk_widget_hide(dds2_phase);
		gtk_widget_hide(dds3_phase);
		gtk_widget_hide(dds4_phase);
		gtk_widget_hide(dds1_freq_l);
		gtk_widget_hide(dds2_freq_l);
		gtk_widget_hide(dds3_freq_l);
		gtk_widget_hide(dds4_freq_l);
		gtk_widget_hide(dds1_scale_l);
		gtk_widget_hide(dds2_scale_l);
		gtk_widget_hide(dds3_scale_l);
		gtk_widget_hide(dds4_scale_l);
		gtk_widget_hide(dds1_phase_l);
		gtk_widget_hide(dds2_phase_l);
		gtk_widget_hide(dds3_phase_l);
		gtk_widget_hide(dds4_phase_l);
		gtk_widget_hide(dds_I_l);
		gtk_widget_hide(dds_I1_l);
		gtk_widget_hide(dds_I2_l);
		gtk_widget_hide(dds_Q_l);
		gtk_widget_hide(dds_Q1_l);
		gtk_widget_hide(dds_Q2_l);
		break;
	default:
		printf("glade file out of sync with C file - please contact developers\n");
		break;
	}

}

static int fmcomms1_cal_eeprom(void)
{
	char eprom_names[512];
	FILE *efp, *fp;
	int num, tmp;

	/* flushes all open output streams */
	fflush(NULL);

	cal_data = malloc(FAB_SIZE_CAL_EEPROM);
	if (cal_data == NULL) {
		return -ENOMEM;
	}

	fp = popen("find /sys -name eeprom 2>/dev/null", "r");

	if(fp == NULL) {
		fprintf(stderr, "can't execute find\n");
		return -errno;
	}

	num = 0;

	while(fgets(eprom_names, sizeof(eprom_names), fp) != NULL){
		num++;
		/* strip trailing new lines */
		if (eprom_names[strlen(eprom_names) - 1] == '\n')
			eprom_names[strlen(eprom_names) - 1] = '\0';

		efp = fopen(eprom_names, "rb");
		if(efp == NULL)
			return -errno;

		memset(cal_data, 0, FAB_SIZE_CAL_EEPROM);
		tmp = fread(cal_data, FAB_SIZE_CAL_EEPROM, 1, efp);
		fclose(efp);

		if (!tmp || cal_data->adi_magic0 != ADI_MAGIC_0 || cal_data->adi_magic1 != ADI_MAGIC_1) {
			continue;
		}

		if (cal_data->version != ADI_VERSION(VERSION_SUPPORTED)) {
			continue;
		}

		fprintf (stdout, "Found Calibration EEPROM @ %s\n", eprom_names);
		pclose(fp);

		return 0;
	}

	pclose(fp);

	return -ENODEV;
}

static int fmcomms1_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *fmcomms1_panel;

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "fmcomms1.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "fmcomms1.glade", NULL);

	fmcomms1_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms1_panel"));

	load_eeprom = GTK_WIDGET(gtk_builder_get_object(builder, "LoadCal2eeprom"));

	avg_I = GTK_WIDGET(gtk_builder_get_object(builder, "avg_I"));
	avg_Q = GTK_WIDGET(gtk_builder_get_object(builder, "avg_Q"));
	span_I = GTK_WIDGET(gtk_builder_get_object(builder, "span_I"));
	span_Q = GTK_WIDGET(gtk_builder_get_object(builder, "span_Q"));
	radius_IQ = GTK_WIDGET(gtk_builder_get_object(builder, "radius_IQ"));
	angle_IQ = GTK_WIDGET(gtk_builder_get_object(builder, "angle_IQ"));

	vga_gain0 = GTK_WIDGET(gtk_builder_get_object(builder, "adc_gain0"));
	adj_gain0 = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(vga_gain0));

	vga_gain1 = GTK_WIDGET(gtk_builder_get_object(builder, "adc_gain1"));
	adj_gain1 = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(vga_gain1));

	dds_mode = GTK_WIDGET(gtk_builder_get_object(builder, "dds_mode"));


	dds_I_l  = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I_l"));

	dds_I1_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_l"));

	dds1_freq    = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_freq"));
	adj1_freq    = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(dds1_freq));
	dds1_freq_l  = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_freq_l"));

	dds1_scale   = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_scale"));
	dds1_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_scale_l"));

	dds1_phase   = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_phase"));
	dds1_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_phase_l"));


	dds_I2_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_l"));

	dds2_freq = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_freq"));
	adj2_freq = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(dds2_freq));
	dds2_freq_l  = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_freq_l"));

	dds2_scale = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_scale"));
	dds2_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_scale_l"));

	dds2_phase   = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_phase"));
	dds2_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_phase_l"));


	dds_Q_l  = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q_l"));

	dds_Q1_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_l"));

	dds3_freq = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_freq"));
	adj3_freq = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(dds3_freq));
	dds3_freq_l  = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_freq_l"));

	dds3_scale = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_scale"));
	dds3_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_scale_l"));

	dds3_phase =  GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_phase"));
	dds3_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_phase_l"));


	dds_Q2_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_l"));

	dds4_freq = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_freq"));
	adj4_freq = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(dds4_freq));
	dds4_freq_l  = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_freq_l"));

	dds4_scale = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_scale"));
	dds4_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_scale_l"));

	dds4_phase =  GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_phase"));
	dds4_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_phase_l"));

	dialogs.calibrate =  GTK_WIDGET(gtk_builder_get_object(builder, "cal_dialog"));

	if (iio_devattr_exists("cf-ad9643-core-lpc", "in_voltage_sampling_frequency")) {
		adc_freq_device = "cf-ad9643-core-lpc";
		adc_freq_file = "in_voltage_sampling_frequency";
	} else {
		adc_freq_device = "ad9523-lpc";
		adc_freq_file = "out_altvoltage2_ADC_CLK_frequency";
	}

	gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode), 1);
	manage_dds_mode();
	g_signal_connect( dds_mode, "changed", G_CALLBACK(manage_dds_mode), NULL);

	/* Bind the IIO device files to the GUI widgets */

	/* The next free frequency related widgets - keep in this order! */
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage_1A_sampling_frequency",
			builder, "dac_data_clock", &mhz_scale);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage_interpolation_frequency",
			"out_altvoltage_interpolation_frequency_available",
			builder, "dac_interpolation_clock", NULL);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc",
			"out_altvoltage_interpolation_center_shift_frequency",
			"out_altvoltage_interpolation_center_shift_frequency_available",
			builder, "dac_fcenter_shift", NULL);
	/* DDS */
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage0_1A_frequency",
			dds3_freq, &mhz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage2_2A_frequency",
			dds4_freq, &mhz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage1_1B_frequency",
			dds1_freq, &mhz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage3_2B_frequency",
			dds2_freq, &mhz_scale);

	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage0_1A_scale",
			"out_altvoltage_1A_scale_available",
			dds3_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage2_2A_scale",
			"out_altvoltage_2A_scale_available",
			dds4_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage1_1B_scale",
			"out_altvoltage_1B_scale_available",
			dds1_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage3_2B_scale",
			"out_altvoltage_2B_scale_available",
			dds2_scale, compare_gain);

	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage0_1A_phase",
			dds3_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage1_1B_phase",
			dds4_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage2_2A_phase",
			dds1_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage3_2B_phase",
			dds2_phase, &khz_scale);

	num_tx_pll = num_tx;
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_frequency",
			builder, "tx_lo_freq", &mhz_scale);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_powerdown",
			builder, "tx_lo_powerdown", 1);

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_frequency_resolution",
			builder, "tx_lo_spacing", NULL);

	/* Calibration */
	iio_spin_button_s64_init_from_builder(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage0_calibbias",
			builder, "dac_calibbias0", NULL);
	iio_spin_button_s64_init_from_builder(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage0_calibscale",
			builder, "dac_calibscale0", NULL);
	iio_spin_button_s64_init_from_builder(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage0_phase",
			builder, "dac_calibphase0", NULL);
	iio_spin_button_s64_init_from_builder(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage1_calibbias",
			builder, "dac_calibbias1", NULL);
	iio_spin_button_s64_init_from_builder(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage1_calibscale",
			builder, "dac_calibscale1", NULL);
	iio_spin_button_s64_init_from_builder(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage1_phase",
			builder, "dac_calibphase1", NULL);
	iio_spin_button_s64_init_from_builder(&cal_widgets[num_cal++],
			"cf-ad9643-core-lpc", "in_voltage0_calibbias",
			builder, "adc_calibbias0", NULL);
	iio_spin_button_s64_init_from_builder(&cal_widgets[num_cal++],
			"cf-ad9643-core-lpc", "in_voltage1_calibbias",
			builder, "adc_calibbias1", NULL);
	iio_spin_button_init_from_builder(&cal_widgets[num_cal++],
			"cf-ad9643-core-lpc", "in_voltage0_calibscale",
			builder, "adc_calibscale0", NULL);
	iio_spin_button_init_from_builder(&cal_widgets[num_cal++],
			"cf-ad9643-core-lpc", "in_voltage1_calibscale",
			builder, "adc_calibscale1", NULL);

	/* Rx Widgets */
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_frequency_resolution",
			builder, "rx_lo_spacing", NULL);
	num_rx_pll = num_rx;

	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_frequency",
			builder, "rx_lo_freq", &mhz_scale);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_powerdown",
			builder, "rx_lo_powerdown", 1);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			adc_freq_device, adc_freq_file,
			builder, "adc_freq", &mhz_scale);

	iio_spin_button_init(&rx_widgets[num_rx++],
			"ad8366-lpc", "out_voltage0_hardwaregain",
			vga_gain0, NULL);
	iio_spin_button_init(&rx_widgets[num_rx++],
			"ad8366-lpc", "out_voltage1_hardwaregain",
			vga_gain1, NULL);

	g_builder_connect_signal(builder, "fmcomms1_settings_save", "clicked",
		G_CALLBACK(save_button_clicked), NULL);

	g_builder_connect_signal(builder, "calibrate_dialog", "clicked",
		G_CALLBACK(cal_dialog), NULL);

	g_signal_connect(
		GTK_WIDGET(gtk_builder_get_object(builder, "gain_amp_together")),
		"toggled", G_CALLBACK(gain_amp_locked_cb), NULL);

	fmcomms1_cal_eeprom();
	tx_update_values();
	rx_update_values();
	cal_update_values();

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), fmcomms1_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), fmcomms1_panel, "FMComms1");

	return 0;
}

static bool fmcomms1_identify(void)
{
	return !set_dev_paths("cf-ad9122-core-lpc");
}

const struct osc_plugin plugin = {
	.name = "FMComms1",
	.identify = fmcomms1_identify,
	.init = fmcomms1_init,
};
