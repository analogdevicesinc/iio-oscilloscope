/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <glib/gthread.h>
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
#include <sys/stat.h>

#include "../osc.h"
#include "../iio_widget.h"
#include "../iio_utils.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "../ini/ini.h"

static const gdouble mhz_scale = 1000000.0;
static const gdouble khz_scale = 1000.0;

static bool dac_data_loaded = false;

#define VERSION_SUPPORTED 0
static struct fmcomms1_calib_data *cal_data = NULL;

static GtkWidget *vga_gain0, *vga_gain1;
static GtkAdjustment *adj_gain0, *adj_gain1;
static GtkWidget *rf_out;

static GtkWidget *dds_mode;
static GtkWidget *dac_buffer;
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

static GtkWidget *dac_shift;

static GtkWidget *rx_lo_freq, *tx_lo_freq;

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
	GtkWidget *filechooser;
};
static Dialogs dialogs;
static GtkWidget *cal_save, *cal_open, *cal_tx, *cal_rx;
static GtkWidget *I_dac_pha_adj, *I_dac_offs, *I_dac_fs_adj;
static GtkWidget *Q_dac_pha_adj, *Q_dac_offs, *Q_dac_fs_adj;
static GtkWidget *I_adc_offset_adj, *I_adc_gain_adj, *I_adc_phase_adj;
static GtkWidget *Q_adc_offset_adj, *Q_adc_gain_adj, *Q_adc_phase_adj;
static double cal_rx_level = 0;

static GtkWidget *ad9122_temp;

static int kill_thread;
static int fmcomms1_cal_eeprom(void);

static int oneover(const gchar *num)
{
	float close;

	close = powf(2.0, roundf(log2f(1.0 / atof(num))));
	return (int)close;

}
static void rf_out_update(void)
{
	char buf[1024], dds1_m[16], dds2_m[16];
	static GtkTextBuffer *tbuf = NULL;
	GtkTextIter iter;
	float dac_shft = 0, dds1, dds2, tx_lo;

	if (tbuf == NULL) {
		tbuf = gtk_text_buffer_new(NULL);
		gtk_text_view_set_buffer(GTK_TEXT_VIEW(rf_out), tbuf);
	}

	memset(buf, 0, 1024);

	sprintf(buf, "\n");
	gtk_text_buffer_set_text(tbuf, buf, -1);
	gtk_text_buffer_get_iter_at_line(tbuf, &iter, 1);

	tx_lo = gtk_spin_button_get_value (GTK_SPIN_BUTTON(tx_lo_freq));
	dds1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_freq));
	dds2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds2_freq));
	if (gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dac_shift)))
		dac_shft = atof(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dac_shift)))/1000000.0;

	if(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds1_scale)))
		sprintf(dds1_m, "1/%i", oneover(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds1_scale))));
	else
		sprintf(dds1_m, "?");

	if(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds2_scale)))
		sprintf(dds2_m, "1/%i", oneover(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds2_scale))));
	else
		sprintf(dds2_m, "?");

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode)) == 1 ||
			gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode)) == 2) {
		sprintf(buf, "%4.3f MHz : Image\n", tx_lo - dds1 - dac_shft);
		gtk_text_buffer_insert(tbuf, &iter, buf, -1);
	}
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode)) == 2) {
		sprintf(buf, "%4.3f MHz : Image\n", tx_lo - dds2 - dac_shft);
		gtk_text_buffer_insert(tbuf, &iter, buf, -1);
	}

	sprintf(buf, "%4.3f MHz : LO Leakage\n", tx_lo);
	gtk_text_buffer_insert(tbuf, &iter, buf, -1);

	switch(gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode))) {
	case 0:
		break;
	case 1:
		sprintf(buf, "%4.3f MHz : Signal %s\n", tx_lo + dds1 + dac_shft, dds1_m);
		gtk_text_buffer_insert(tbuf, &iter, buf, -1);
		break;
	case 2:
		sprintf(buf, "%4.3f MHz : Signal %s\n", tx_lo + dds1 + dac_shft, dds1_m);
		gtk_text_buffer_insert(tbuf, &iter, buf, -1);
		sprintf(buf, "%4.3f MHz : Signal %s\n", tx_lo + dds2 + dac_shft, dds2_m);
		gtk_text_buffer_insert(tbuf, &iter, buf, -1);
		break;
	case 3:
	case 4:
		sprintf(buf, "\n");
		gtk_text_buffer_insert(tbuf, &iter, buf, -1);
		break;

	}

}

short convert(double scale, float val)
{
	return (unsigned short) (val * scale + 32767.0);
}

int analyse_wavefile(char *file_name, char **buf, int *count)
{
	int ret, j, i = 0, size, rep, tx = 1;
	double max = 0.0, val[4], scale;
	double i1, q1, i2, q2;
	char line[80];

	FILE *infile = fopen(file_name, "r");
	if (infile == NULL)
		return -3;

	if (fgets(line, 80, infile) != NULL) {
	if (strncmp(line, "TEXT", 4) == 0) {
		ret = sscanf(line, "TEXT REPEAT %d", &rep);
		if (ret != 1) {
			rep = 1;
		}
		size = 0;
		while (fgets(line, 80, infile)) {
			ret = sscanf(line, "%lf%*[, \t]%lf%*[, \t]%lf%*[, \t]%lf",
				     &val[0], &val[1], &val[2], &val[3]);

			if (!(ret == 4 || ret == 2)) {
				fclose(infile);
				return -2;
			}

			for (i = 0; i < ret; i++)
				if (fabs(val[i]) > max)
					max = fabs(val[i]);

			size += ((tx == 2) ? 8 : 4);


		}

	size *= rep;
	scale = 32767.0 / max;

	*buf = malloc(size);
	if (*buf == NULL)
		return 0;

	unsigned long long *sample = *((unsigned long long **)buf);
	unsigned int *sample_32 = *((unsigned int **)buf);

	rewind(infile);

	if (fgets(line, 80, infile) != NULL) {
		if (strncmp(line, "TEXT", 4) == 0) {
			size = 0;
			i = 0;
			while (fgets(line, 80, infile)) {

				ret = sscanf(line, "%lf%*[, \t]%lf%*[, \t]%lf%*[, \t]%lf",
					     &i1, &q1, &i2, &q2);
				for (j = 0; j < rep; j++) {
					if (ret == 4 && tx == 2) {
						sample[i++] = ((unsigned long long)convert(scale, q2) << 48) +
							((unsigned long long)convert(scale, i2) << 32) +
							(convert(scale, q1) << 16) +
							(convert(scale, i1) << 0);

						size += 8;
					}
					if (ret == 2 && tx == 2) {
						sample[i++] = ((unsigned long long)convert(scale, q1) << 48) +
							((unsigned long long)convert(scale, i1) << 32) +
							(convert(scale, q1) << 16) +
							(convert(scale, i1) << 0);

						size += 8;
					}
					if (tx == 1) {
						sample_32[i++] = (convert(scale, q1) << 16) +
							(convert(scale, i1) << 0);

						size += 4;
					}
				}
			}
		}
	}

	fclose(infile);
	*count = size;

	}} else {
		fclose(infile);
		*buf = NULL;
		return -1;
	}

	return 0;
}

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
	rf_out_update();
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
	rf_out_update();
	iio_save_widgets(tx_widgets, num_tx);
	iio_save_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

void dac_buffer_config_file_set_cb(GtkFileChooser *chooser, gpointer data)
{
	int ret, fd, size;
	struct stat st;
	char *buf;
	FILE *infile;

	char *file_name = gtk_file_chooser_get_filename(chooser);
	ret = analyse_wavefile(file_name, &buf, &size);
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

	set_dev_paths("cf-ad9122-core-lpc");
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

static unsigned short float_to_fract(double val)
{
	unsigned short fract = 0;
	unsigned long long llval;

	if (val <= 0.000000) {
		fract = 0x8000;
		val *= -1.0;
	}

	val *= 1000000;

	llval = (unsigned long long)val * 0x8000UL;
	fract |= (llval / 1000000);

	return fract;
}

static double fract_to_float(unsigned short val)
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
		cal_update_values();
	}

	if (rx) {
		set_dev_paths("cf-ad9643-core-lpc");
		write_devattr_slonglong("in_voltage0_calibbias", data->i_adc_offset_adj);
		write_devattr_double("in_voltage0_calibscale", fract_to_float(data->i_adc_gain_adj));
		write_devattr_slonglong("in_voltage1_calibbias", data->q_adc_offset_adj);
		write_devattr_double("in_voltage1_calibscale", fract_to_float(data->q_adc_gain_adj));
		cal_update_values();
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
	size_t mode = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode));

	switch (mode) {
		case 1:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds2_freq), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_freq), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_freq), freq1);
			break;
		case 2:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_freq), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_freq), freq2);
			break;
		case 0: /* Off */
		case 3: /* Independent I/Q Control */
		case 4: /* DAC output */
			break;
		default:
			printf("%s: unknown mode (%d)error\n", __func__, (int)mode);
			break;
	}
}


static void dds_locked_phase_cb(GtkToggleButton *btn, gpointer data)
{

	gdouble phase1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_phase));
	gdouble phase2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds2_phase));
	size_t mode = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode));

	switch (mode) {
		case 1:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds2_phase), phase1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_phase), phase1 + 90.0);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_phase), phase1 + 90.0);
			break;
		case 2:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_phase), phase1 + 90.0);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_phase), phase2 + 90.0);
			break;
		case 0: /* Off */
		case 3: /* Independent I/Q Control */
		case 4: /* DAC output */
			break;
		default:
			printf("%s: unknown mode (%d)error\n", __func__, (int)mode);
			break;
	}
}
static void dds_locked_scale_cb(GtkComboBoxText *box, gpointer data)
{
	gint scale1 = gtk_combo_box_get_active(GTK_COMBO_BOX(dds1_scale));
	gint scale2 = gtk_combo_box_get_active(GTK_COMBO_BOX(dds2_scale));
	size_t mode = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode));

	switch (mode) {
		case 1:
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds2_scale), scale1);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds3_scale), scale1);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds4_scale), scale1);
			break;
		case 2:
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds3_scale), scale1);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds4_scale), scale2);
			break;
		case 0: /* Off */
		case 3: /* Independent I/Q Control */
		case 4: /* DAC output */
			break;
		default:
			printf("%s: unknown mode (%d)error\n", __func__, (int)mode);
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

static bool cal_rx_flag = false;
static gfloat knob_max, knob_min, knob_steps;
static int delay;

static void cal_rx_button_clicked(GtkButton *btn, gpointer data)
{
	cal_rx_flag = true;

	delay = 50 * plugin_data_capture_size(NULL) * plugin_get_fft_avg(NULL);

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_adc_phase_adj), 0);

	knob_steps = 10;
	knob_max = 1;
	knob_min = -1;
	gtk_widget_hide(cal_rx);
}

static void dac_temp_update()
{
	double temp;
	char buf[25];

	gdk_threads_enter();
	set_dev_paths("cf-ad9122-core-lpc");
	if (read_devattr_double("in_temp0_input", &temp) < 0) {
		/* Just assume it's 25C */
		temp = 2500;
		write_devattr_double("in_temp0_input", temp);
	}

	sprintf(buf, "%2.1f", temp/1000);
	gtk_label_set_text(GTK_LABEL(ad9122_temp), buf);
	gdk_threads_leave();
}

#define RX_CAL_THRESHOLD -75

static void display_cal(void *ptr)
{
	int size, channels, num_samples, i;
	int8_t *buf = NULL;
	gfloat **cooked_data = NULL;
	struct marker_type *markers = NULL;
	gfloat *channel_I, *channel_Q;
	gfloat max_x, min_x, avg_x;
	gfloat max_y, min_y, avg_y;
	gfloat max_r, min_r, max_theta, min_theta, rad;
	GtkSpinButton *knob;
	gfloat knob_value, knob_min_value, knob_min_knob = 0.0, knob_dc_value = 0.0, knob_twist;
	gfloat span_I_val, span_Q_val;
	gfloat gain = 1.0;
	char cbuf[256];
	bool show = false;
	const char *device_ref;
	int ret, attempt = 0;

	device_ref = plugin_get_device_by_reference("cf-ad9643-core-lpc");
	if (!device_ref)
		goto display_call_ret;

	while (!kill_thread) {
		while (!kill_thread && !capture_function) {
			/* Wait 1/2 second */
			dac_temp_update();
			usleep(500000);
		}

		if (kill_thread) {
			size = 0;
		} else {
			size = plugin_data_capture_size(device_ref);
			channels = plugin_data_capture_num_active_channels(device_ref);
			i = plugin_data_capture_bytes_per_sample(device_ref);
			if (i)
				num_samples = size / i;
			else
				num_samples = 0;
		}

		if (size != 0 && channels == 2) {
			gdk_threads_enter();
			if (show && !cal_rx_flag)
				gtk_widget_show(cal_rx);
			else
				gtk_widget_hide(cal_rx);
			gdk_threads_leave();

			/* grab the data */
			if (cal_rx_flag && cal_rx_level && plugin_get_marker_type(device_ref) == MARKER_IMAGE) {
				do {
					ret = plugin_data_capture(device_ref, (void **)&buf, &cooked_data, &markers);
				} while ((ret == -EBUSY) && !kill_thread);
			} else {
				do {
					ret = plugin_data_capture(device_ref, (void **)&buf, &cooked_data, NULL);
				} while ((ret == -EBUSY) && !kill_thread);
			}

			/* If the lock is broken, then die nicely */
			if (kill_thread || ret != 0) {
				size = 0;
				kill_thread = 1;
				break;
			}

			channel_I = cooked_data[0];
			channel_Q = cooked_data[1];
			avg_x = avg_y = 0.0;
			max_x = max_y = -MAXFLOAT;
			min_x = min_y = MAXFLOAT;
			max_r = max_theta = -MAXFLOAT;
			min_r = min_theta = MAXFLOAT;

			for (i = 0; i < num_samples; i++) {
				avg_x += channel_Q[i];
				avg_y += channel_I[i];
				rad = sqrtf((channel_I[i] * channel_I[i]) +
						(channel_Q[i] * channel_Q[i]));

				if (max_x <= channel_Q[i])
					max_x = channel_Q[i];
				if (min_x >= channel_Q[i])
					min_x = channel_Q[i];
				if (max_y <= channel_I[i])
					max_y = channel_I[i];
				if (min_y >= channel_I[i])
					min_y = channel_I[i];

				if (max_r <= rad) {
					max_r = rad;
					if (channel_I[i])
						max_theta = asinf(channel_Q[i]/rad);
					else
						max_theta = 0.0f;
				}
				if (min_r >= rad) {
					min_r = rad;
					if (channel_I[i])
						min_theta = asinf(channel_Q[i]/rad);
					else
						min_theta = 0.0f;
				}
			}

			avg_x /= num_samples;
			avg_y /= num_samples;

			if (min_r >= 10)
				show = true;
			else
				show = false;

			gdk_threads_enter();

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

			gdk_threads_leave();

			if (cal_rx_flag) {
				gfloat span_I_set, span_Q_set;

				gdk_threads_enter();
				/* DC correction */
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_adc_offset_adj),
					gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_offset_adj)) +
					(min_y + max_y) / -2.0);
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_adc_offset_adj),
					gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_offset_adj)) +
					(min_x + max_x) / -2.0);

				/* Scale connection */
				span_I_set = gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_gain_adj));
				span_Q_set = gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_gain_adj));
				span_I_val = (max_y - min_y) / span_I_set;
				span_Q_val = (max_x - min_x) / span_Q_set;

				if (cal_rx_level && plugin_get_marker_type(device_ref) == MARKER_IMAGE) {
					if (attempt == 0)
						gain = (span_I_set + span_I_set) / 2;
					gain *= 1.0 / exp10((markers[0].y - cal_rx_level) / 20);
				}

				gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_adc_gain_adj),
					(span_I_val + span_Q_val)/(2.0 * span_I_val) * gain);
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_adc_gain_adj),
					(span_I_val + span_Q_val)/(2.0 * span_Q_val) * gain);
				cal_save_values();
				gdk_threads_leave ();

				usleep(delay);
				if (plugin_get_marker_type(device_ref) != MARKER_IMAGE)
					cal_rx_flag = false;
			}

			if (cal_rx_flag) {
				int bigger = 0;
				gfloat last_val = FLT_MAX;

				if (attempt == 0) {
					/* if the current value is OK, we leave it alone */
					do {
						ret = plugin_data_capture(device_ref, NULL, NULL, &markers);
					} while ((ret == -EBUSY) && !kill_thread);

					/* If the lock is broken, then die nicely */
					if (kill_thread || ret != 0) {
						size = 0;
						kill_thread = 1;
						break;
					}

					/* make sure image, and DC are below */
					if ((markers[2].y <= RX_CAL_THRESHOLD) && (markers[1].y <= RX_CAL_THRESHOLD)) {
						goto skip_rx_cal;
					}
				}

				gdk_threads_enter();
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_adc_phase_adj), 0);
				knob = GTK_SPIN_BUTTON(I_adc_phase_adj);
				gdk_threads_leave();

				attempt++;
				knob_min_value = 0;

				knob_twist = (knob_max - knob_min)/knob_steps;
				for (knob_value = knob_min;
						knob_value <= knob_max;
						knob_value += knob_twist) {

					gdk_threads_enter();
					gtk_spin_button_set_value(knob, knob_value);
					gdk_threads_leave();
					usleep(delay);

					/* grab the data */
					do {
						ret = plugin_data_capture(device_ref, NULL, NULL, &markers);
					} while ((ret == -EBUSY) && !kill_thread);

					/* If the lock is broken, then die nicely */
					if (kill_thread || ret != 0) {
						size = 0;
						kill_thread = 1;
						break;
					}

					if (markers[2].y <= knob_min_value) {
						knob_min_value = markers[2].y;
						knob_dc_value = markers[1].y;
						knob_min_knob = knob_value;
						bigger = 0;
					}

					if (markers[2].y > last_val)
						bigger++;

					if (bigger == 2)
						break;

					last_val = markers[2].y;
				}

				knob_max = knob_min_knob + (1 * knob_twist);
				if (knob_max >= 1.0)
					knob_max = 1.0;

				knob_min = knob_min_knob - (1 * knob_twist);
				if (knob_min <= -1.0)
					knob_min = -1.0;

				gdk_threads_enter();
				gtk_spin_button_set_value(knob, knob_min_knob);
				gdk_threads_leave();
				usleep(delay);

				if (attempt >= 5 || ((knob_min_value <= RX_CAL_THRESHOLD) &&
						    (knob_dc_value <= RX_CAL_THRESHOLD))) {
skip_rx_cal:
					attempt = 0;
					cal_rx_flag = false;
					if (ptr) {
						kill_thread = 1;
						size = 0;
						break;
					}
				}
			}
		}
	}

display_call_ret:
	/* free the buffers */
	plugin_data_capture(NULL, (void **)&buf, &cooked_data, &markers);

	gdk_threads_enter();
	gtk_dialog_response(GTK_DIALOG(dialogs.calibrate), GTK_RESPONSE_CLOSE);
	gdk_threads_leave();
	kill_thread = 1;
}


char * get_filename(char *name, bool load)
{
	gint ret;
	char *filename, buf[256];

	if (load) {
		gtk_widget_hide(cal_save);
		gtk_widget_show(cal_open);
	} else {
		gtk_widget_hide(cal_open);
		gtk_widget_show(cal_save);
		gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialogs.filechooser), true);
		if (name)
			gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialogs.filechooser), name);
		else {
			sprintf(buf, "%03.0f.txt", gtk_spin_button_get_value (GTK_SPIN_BUTTON(rx_lo_freq)));
			gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialogs.filechooser), buf);
		}
	}
	ret = gtk_dialog_run(GTK_DIALOG(dialogs.filechooser));
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialogs.filechooser));

	if (filename) {
		switch(ret) {
			/* Response Codes encoded in glade file */
			case GTK_RESPONSE_CANCEL:
				g_free(filename);
				filename = NULL;
				break;
			case 2: /* open */
			case 1: /* save */
				break;
			default:
				printf("unknown ret (%i) in %s\n", ret, __func__);
				g_free(filename);
				filename = NULL;
				break;
		}
	}
	gtk_widget_hide(dialogs.filechooser);

	return filename;
}


#define MATCH_SECT(s) (strcmp(section, s) == 0)
#define MATCH_NAME(n) (strcmp(name, n) == 0)
#define RX_F   "Rx_Frequency"
#define TX_F   "Tx_Frequency"
#define DDS1_F "DDS1_Frequency"
#define DDS1_S "DDS1_Scale"
#define DDS1_P "DDS1_Phase"
#define DDS2_F "DDS2_Frequency"
#define DDS2_S "DDS2_Scale"
#define DDS2_P "DDS2_Phase"
#define DDS3_F "DDS3_Frequency"
#define DDS3_S "DDS3_Scale"
#define DDS3_P "DDS3_Phase"
#define DDS4_F "DDS4_Frequency"
#define DDS4_S "DDS4_Scale"
#define DDS4_P "DDS4_Phase"

#define DAC_I_P "I_pha_adj"
#define DAC_I_O "I_dac_offs"
#define DAC_I_G "I_fs_adj"
#define DAC_Q_P "Q_pha_adj"
#define DAC_Q_O "Q_dac_offs"
#define DAC_Q_G "Q_fs_adj"

#define ADC_I_O "I_adc_offset_adj"
#define ADC_I_G "I_adc_gain_adj"
#define ADC_I_P "I_adc_phase_adj"
#define ADC_Q_O "Q_adc_offset_adj"
#define ADC_Q_G "Q_adc_gain_adj"
#define ADC_Q_P "Q_adc_phase_adj"

static void combo_box_set_active_text(GtkWidget *combobox, const char* text)
{
	GtkTreeModel *tree = gtk_combo_box_get_model(GTK_COMBO_BOX(combobox));
	gboolean valid;
	GtkTreeIter iter;
	gint i = 0;

	valid = gtk_tree_model_get_iter_first (tree, &iter);
	while (valid) {
		gchar *str_data;

		gtk_tree_model_get(tree, &iter, 0, &str_data, -1);
		if (!strcmp(str_data, text)) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(combobox), i);
			break;
		}

		i++;
		g_free (str_data);
		valid = gtk_tree_model_iter_next (tree, &iter);
	}
}

static int parse_cal_handler(void* user, const char* section, const char* name, const char* value)
{

	if (MATCH_SECT("SYS_SETTINGS")) {
		if(MATCH_NAME(RX_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_lo_freq), atof(value));
		else if (MATCH_NAME(TX_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_lo_freq), atof(value));

		else if (MATCH_NAME(DDS1_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds1_freq), atof(value));
		else if (MATCH_NAME(DDS1_S))
			combo_box_set_active_text(dds1_scale, value);
		else if (MATCH_NAME(DDS1_P))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds1_phase), atof(value));

		else if (MATCH_NAME(DDS2_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds2_freq), atof(value));
		else if (MATCH_NAME(DDS2_S))
			combo_box_set_active_text(dds2_scale, value);
		else if (MATCH_NAME(DDS2_P))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds2_phase), atof(value));

		else if (MATCH_NAME(DDS3_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_freq), atof(value));
		else if (MATCH_NAME(DDS3_S))
			combo_box_set_active_text(dds3_scale, value);
		else if (MATCH_NAME(DDS3_P))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_phase), atof(value));

		else if (MATCH_NAME(DDS4_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_freq), atof(value));
		else if (MATCH_NAME(DDS4_S))
			combo_box_set_active_text(dds4_scale, value);
		else if (MATCH_NAME(DDS4_P))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_phase), atof(value));
	} else if (MATCH_SECT("DAC_SETTINGS")) {
		if(MATCH_NAME(DAC_I_P))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_dac_pha_adj), atof(value));
		else if (MATCH_NAME(DAC_I_O))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_dac_offs), atof(value));
		else if (MATCH_NAME(DAC_I_G))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_dac_fs_adj), atof(value));
		else if(MATCH_NAME(DAC_Q_P))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_dac_pha_adj), atof(value));
		else if(MATCH_NAME(DAC_Q_O))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_dac_offs), atof(value));
		else if(MATCH_NAME(DAC_Q_G))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_dac_fs_adj), atof(value));
	} else if (MATCH_SECT("ADC_SETTINGS")) {
		if(MATCH_NAME(ADC_I_O))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_adc_offset_adj), (gfloat)atoi(value));
		else if (MATCH_NAME(ADC_Q_O))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_adc_offset_adj), (gfloat)atoi(value));
		else if (MATCH_NAME(ADC_I_G))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_adc_gain_adj), atof(value));
		else if (MATCH_NAME(ADC_Q_G))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_adc_gain_adj), atof(value));
		else if (MATCH_NAME(ADC_I_P))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_adc_phase_adj), atof(value));
		else if (MATCH_NAME(ADC_Q_P))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_adc_phase_adj), atof(value));
	}
	return 1;
}

void load_cal(char * resfile)
{

	if (ini_parse(resfile, parse_cal_handler, NULL) >= 0) {
		/* Sucess */
	}

	return;
}

void save_cal(char * resfile)
{
	FILE* file;
	time_t clock = time(NULL);

	file = fopen(resfile, "w");
	if (!file)
		return;

	fprintf(file, ";Calibration time: %s\n", ctime(&clock));

	fprintf(file, "\n[SYS_SETTINGS]\n");
	fprintf(file, "%s = %f\n", RX_F, gtk_spin_button_get_value (GTK_SPIN_BUTTON(rx_lo_freq)));
	fprintf(file, "%s = %f\n", TX_F, gtk_spin_button_get_value (GTK_SPIN_BUTTON(tx_lo_freq)));
	fprintf(file, "dds_mode = %i", gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode)));
	fprintf(file, "%s = %f\n", DDS1_F, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_freq)));
	fprintf(file, "%s = %s\n", DDS1_S, gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds1_scale)));
	fprintf(file, "%s = %f\n", DDS1_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_phase)));
	fprintf(file, "%s = %f\n", DDS2_F, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds2_freq)));
	fprintf(file, "%s = %s\n", DDS2_S, gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds2_scale)));
	fprintf(file, "%s = %f\n", DDS2_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds2_phase)));
	fprintf(file, "%s = %f\n", DDS3_F, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds3_freq)));
	fprintf(file, "%s = %s\n", DDS3_S, gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds3_scale)));
	fprintf(file, "%s = %f\n", DDS3_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds3_phase)));
	fprintf(file, "%s = %f\n", DDS4_F, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds4_freq)));
	fprintf(file, "%s = %s\n", DDS4_S, gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds4_scale)));
	fprintf(file, "%s = %f\n", DDS4_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds4_phase)));

	fprintf(file, "\n[DAC_SETTINGS]\n");
	fprintf(file, "%s = %f\n", DAC_I_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_dac_pha_adj)));
	fprintf(file, "%s = %f\n", DAC_Q_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_dac_pha_adj)));
	fprintf(file, "%s = %f\n", DAC_I_O, gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_dac_offs)));
	fprintf(file, "%s = %f\n", DAC_Q_O, gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_dac_offs)));
	fprintf(file, "%s = %f\n", DAC_I_G, gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_dac_fs_adj)));
	fprintf(file, "%s = %f\n", DAC_Q_G, gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_dac_fs_adj)));

	fprintf(file, "\n[ADC_SETTINGS]\n");
	fprintf(file, "%s = %i\n", ADC_I_O, (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_offset_adj)));
	fprintf(file, "%s = %i\n", ADC_Q_O, (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_offset_adj)));
	fprintf(file, "%s = %f #0x%x\n", ADC_I_G, gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_gain_adj)),
				float_to_fract(gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_gain_adj))));
	fprintf(file, "%s = %f #0x%x\n", ADC_Q_G, gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_gain_adj)),
				float_to_fract(gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_gain_adj))));
	fprintf(file, "%s = %f #0x%x\n", ADC_I_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_phase_adj)),
				float_to_fract(gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_phase_adj))));
	fprintf(file, "%s = %f #0x%x\n", ADC_Q_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_phase_adj)),
				float_to_fract(gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_phase_adj))));

	/* Don't have this info yet
	 * fprintf(file, "\n[SAVE]\n");
	 * fprintf(file, "PlotFile = %s\n", plotfile);
	 *
	 * fprintf(file, "\n[RESULTS]\n");
	 * fprintf(file, "Signal = %lf dBm\n", res->signal_lvl);
	 * fprintf(file, "Carrier = %lf dBm\n", res->carrier_lvl);
	 * fprintf(file, "Sideband = %lf dBm\n", res->sideband_lvl);
	 * fprintf(file, "Carrier Suppression = %lf dBc\n", res->signal_lvl - res->carrier_lvl);
	 * fprintf(file, "Sideband Suppression = %lf dBc\n", res->signal_lvl - res->sideband_lvl);
	 */

	fclose(file);
	return;

}

G_MODULE_EXPORT void cal_dialog(GtkButton *btn, Dialogs *data)
{
	gint ret;
	char *filename = NULL;

	kill_thread = 0;

	g_thread_new("Display_thread", (void *) &display_cal, NULL);

	gtk_widget_show(dialogs.calibrate);

	gtk_widget_hide(cal_rx);
	gtk_widget_hide(cal_tx);

	if (fmcomms1_cal_eeprom() < 0)
		gtk_widget_hide(load_eeprom);

	do {
		ret = gtk_dialog_run(GTK_DIALOG(dialogs.calibrate));
		switch (ret) {
			case 1: /* Load data from EEPROM */
				load_cal_eeprom();
				break;
			case 2: /* Save data to EEPROM */
				/* TODO */
				printf("sorry, not implmented yet\n");
				break;
			case 3: /* Load data from file */
				filename = get_filename(filename, true);
				if (filename) {
					load_cal(filename);
				}
				break;
			case 4: /* Save data to file */
				filename = get_filename(filename, false);
				if (filename) {
					save_cal(filename);
				}
				break;
			case 5: /* Cal Tx side */
				break;
			case 6: /* Cal Rx side */
				break;
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

	if (filename)
		g_free(filename);

	gtk_widget_hide(dialogs.calibrate);
}

static void enable_dds(bool on_off)
{
	int ret;

	set_dev_paths("cf-ad9122-core-lpc");
	write_devattr_int("out_altvoltage0_1A_raw", on_off ? 1 : 0);

	if (on_off || dac_data_loaded) {
		ret = write_devattr_int("buffer/enable", !on_off);
		if (ret < 0) {
			fprintf(stderr, "Failed to enable buffer: %d\n", ret);

		}
	}
}

static void manage_dds_mode()
{
	gint active;

	rf_out_update();
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
		gtk_widget_hide(dac_buffer);
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
		gtk_label_set_markup(GTK_LABEL(dds_I_l), "<b>Single Tone</b>");
		gtk_widget_show(dds_I1_l);
		gtk_widget_hide(dds_I2_l);
		gtk_widget_hide(dds_Q_l);
		gtk_widget_hide(dds_Q1_l);
		gtk_widget_hide(dds_Q2_l);
		gtk_widget_hide(dac_buffer);

#define IIO_SPIN_SIGNAL "value-changed"
#define IIO_COMBO_SIGNAL "changed"

		if (!dds1_scale_hid)
			dds1_scale_hid = g_signal_connect(dds1_scale , IIO_COMBO_SIGNAL,
					G_CALLBACK(dds_locked_scale_cb), NULL);

		if (!dds1_freq_hid)
			dds1_freq_hid = g_signal_connect(dds1_freq , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_freq_cb), NULL);

		if (!dds1_phase_hid)
			dds1_freq_hid = g_signal_connect(dds1_freq , IIO_SPIN_SIGNAL,
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

		dds_locked_phase_cb(NULL, NULL);
		dds_locked_scale_cb(NULL, NULL);
		dds_locked_freq_cb(NULL, NULL);

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
		gtk_label_set_markup(GTK_LABEL(dds_I_l), "<b>Two Tones</b>");
		gtk_widget_show(dds_I1_l);
		gtk_widget_show(dds_I2_l);
		gtk_widget_hide(dds_Q_l);
		gtk_widget_hide(dds_Q1_l);
		gtk_widget_hide(dds_Q2_l);
		gtk_widget_hide(dac_buffer);

		if (!dds1_scale_hid)
			dds1_scale_hid = g_signal_connect(dds1_scale , IIO_COMBO_SIGNAL,
					G_CALLBACK(dds_locked_scale_cb), NULL);
		if (!dds2_scale_hid)
			dds2_scale_hid = g_signal_connect(dds2_scale , IIO_COMBO_SIGNAL,
					G_CALLBACK(dds_locked_scale_cb), NULL);

		if (!dds1_freq_hid)
			dds1_freq_hid = g_signal_connect(dds1_freq , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_freq_cb), NULL);
		if (!dds2_freq_hid)
			dds2_freq_hid = g_signal_connect(dds2_freq , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_freq_cb), NULL);

		if (!dds1_phase_hid)
			dds1_phase_hid = g_signal_connect(dds1_phase , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_phase_cb), NULL);
		if (!dds2_phase_hid)
			dds2_phase_hid = g_signal_connect(dds2_phase , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_phase_cb), NULL);

		dds_locked_phase_cb(NULL, NULL);
		dds_locked_scale_cb(NULL, NULL);
		dds_locked_freq_cb(NULL, NULL);

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
		gtk_label_set_markup(GTK_LABEL(dds_I_l), "<b>Channel I</b>");
		gtk_widget_show(dds_I1_l);
		gtk_widget_show(dds_I2_l);
		gtk_widget_show(dds_Q_l);
		gtk_widget_show(dds_Q1_l);
		gtk_widget_show(dds_Q2_l);
		gtk_widget_hide(dac_buffer);

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

		dds_locked_phase_cb(NULL, NULL);
		dds_locked_scale_cb(NULL, NULL);
		dds_locked_freq_cb(NULL, NULL);

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
		gtk_widget_show(dac_buffer);
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

	if (!cal_data)
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

static void adc_cal_spin(GtkRange *range, gpointer user_data)
{
	gdouble val, inc;

	val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(range));
	gtk_spin_button_get_increments(GTK_SPIN_BUTTON(range), &inc, NULL);

	set_dev_paths("cf-ad9643-core-lpc");
	if (inc == 1.0)
		write_devattr_int((char *)user_data, (int)val);
	else
		write_devattr_double((char *)user_data, val);

}


static int fmcomms1_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *fmcomms1_panel;

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "fmcomms1.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "fmcomms1.glade", NULL);

	rx_lo_freq = GTK_WIDGET(gtk_builder_get_object(builder, "rx_lo_freq"));
	tx_lo_freq = GTK_WIDGET(gtk_builder_get_object(builder, "tx_lo_freq"));

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

	dac_buffer = GTK_WIDGET(gtk_builder_get_object(builder, "dac_buffer"));

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
	dialogs.filechooser = GTK_WIDGET(gtk_builder_get_object(builder, "filechooser"));

	cal_save = GTK_WIDGET(gtk_builder_get_object(builder, "Save"));
	cal_open = GTK_WIDGET(gtk_builder_get_object(builder, "Open"));
	cal_rx = GTK_WIDGET(gtk_builder_get_object(builder, "Cal_Rx"));
	cal_tx = GTK_WIDGET(gtk_builder_get_object(builder, "Cal_Tx"));

	I_dac_pha_adj = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibphase0"));
	I_dac_offs = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibbias0"));
	I_dac_fs_adj = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibscale0"));

	Q_dac_pha_adj = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibphase1"));
	Q_dac_offs = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibbias1"));
	Q_dac_fs_adj = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibscale1"));

	I_adc_offset_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibbias0"));
	I_adc_gain_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibscale0"));
	I_adc_phase_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibphase0"));

	Q_adc_offset_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibbias1"));
	Q_adc_gain_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibscale1"));
	Q_adc_phase_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibphase1"));

	ad9122_temp = GTK_WIDGET(gtk_builder_get_object(builder, "dac_temp"));

	rf_out =  GTK_WIDGET(gtk_builder_get_object(builder, "RF_out"));
	dac_shift = GTK_WIDGET(gtk_builder_get_object(builder, "dac_fcenter_shift"));

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
			dds1_freq, &mhz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage1_1B_frequency",
			dds4_freq, &mhz_scale);
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
			dds1_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage1_1B_scale",
			"out_altvoltage_1B_scale_available",
			dds4_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage3_2B_scale",
			"out_altvoltage_2B_scale_available",
			dds2_scale, compare_gain);

	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage0_1A_phase",
			dds3_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage2_2A_phase",
			dds1_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage1_1B_phase",
			dds4_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage3_2B_phase",
			dds2_phase, &khz_scale);

	num_tx_pll = num_tx;
	iio_spin_button_int_init(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_frequency",
			tx_lo_freq, &mhz_scale);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_powerdown",
			builder, "tx_lo_powerdown", 1);

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_frequency_resolution",
			builder, "tx_lo_spacing", NULL);

	/* Calibration */
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage0_calibbias",
			I_dac_offs, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage0_calibscale",
			I_dac_fs_adj, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage0_phase",
			I_dac_pha_adj, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage1_calibbias",
			Q_dac_offs, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage1_calibscale",
			Q_dac_fs_adj, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			"cf-ad9122-core-lpc", "out_voltage1_phase",
			Q_dac_pha_adj, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			"cf-ad9643-core-lpc", "in_voltage0_calibbias",
			I_adc_offset_adj, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			"cf-ad9643-core-lpc", "in_voltage1_calibbias",
			Q_adc_offset_adj, NULL);
	iio_spin_button_init(&cal_widgets[num_cal++],
			"cf-ad9643-core-lpc", "in_voltage0_calibscale",
			I_adc_gain_adj, NULL);
	iio_spin_button_init(&cal_widgets[num_cal++],
			"cf-ad9643-core-lpc", "in_voltage1_calibscale",
			Q_adc_gain_adj, NULL);
	iio_spin_button_init(&cal_widgets[num_cal++],
			"cf-ad9643-core-lpc", "in_voltage0_calibphase",
			I_adc_phase_adj, NULL);
	iio_spin_button_init(&cal_widgets[num_cal++],
			"cf-ad9643-core-lpc", "in_voltage1_calibphase",
			Q_adc_phase_adj, NULL);

	g_signal_connect(I_adc_gain_adj  , "value-changed",
			G_CALLBACK(adc_cal_spin), "in_voltage0_calibscale");
	g_signal_connect(I_adc_offset_adj, "value-changed",
			G_CALLBACK(adc_cal_spin), "in_voltage0_calibbias");
	g_signal_connect(I_adc_phase_adj , "value-changed",
			G_CALLBACK(adc_cal_spin), "in_voltage0_calibphase");
	g_signal_connect(Q_adc_gain_adj  , "value-changed",
			G_CALLBACK(adc_cal_spin), "in_voltage1_calibscale");
	g_signal_connect(Q_adc_offset_adj, "value-changed",
			G_CALLBACK(adc_cal_spin), "in_voltage1_calibbias");
	g_signal_connect(Q_adc_phase_adj , "value-changed",
			G_CALLBACK(adc_cal_spin), "in_voltage1_calibphase");

	/* Rx Widgets */
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_frequency_resolution",
			builder, "rx_lo_spacing", NULL);
	num_rx_pll = num_rx;

	iio_spin_button_int_init(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_frequency",
			rx_lo_freq, &mhz_scale);
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

	g_builder_connect_signal(builder, "dac_buffer", "file-set",
		G_CALLBACK(dac_buffer_config_file_set_cb), NULL);

	g_signal_connect(
		GTK_WIDGET(gtk_builder_get_object(builder, "gain_amp_together")),
		"toggled", G_CALLBACK(gain_amp_locked_cb), NULL);

	g_signal_connect(cal_rx, "clicked", G_CALLBACK(cal_rx_button_clicked), NULL);

	fmcomms1_cal_eeprom();
	tx_update_values();
	rx_update_values();
	cal_update_values();

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), fmcomms1_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), fmcomms1_panel, "FMComms1");
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dac_buffer), OSC_WAVEFORM_FILE_PATH);

	return 0;
}

#define SYNC_RELOAD "SYNC_RELOAD"

static char *handle_item(struct osc_plugin *plugin, const char *attrib,
			 const char *value)
{
	char *buf;
	GThread *thr = NULL;

	if (MATCH_ATTRIB(SYNC_RELOAD)) {
		if (value) {
			tx_update_values();
			rx_update_values();
		} else {
			return "1";
		}
	} else if (MATCH_ATTRIB("dds_mode")) {
		if (value) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode), atoi(value));
		} else {
			buf = malloc (10);
			sprintf(buf, "%i", gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode)));
			return buf;
		}
	} else if (MATCH_ATTRIB("calibrate_rx_level")) {
		if (value)
			cal_rx_level = atof(value);
	} else if (MATCH_ATTRIB("calibrate_rx")) {
		if (value && atoi(value) == 1) {
			gtk_widget_show(dialogs.calibrate);
			cal_rx_button_clicked(NULL, NULL);
			kill_thread = 0;
			thr = g_thread_new("Display_thread", (void *) &display_cal, (gpointer *)1);
			while (kill_thread == 0) {
				gtk_main_iteration();
			}
			iio_thread_clear(thr);
			gtk_widget_hide(dialogs.calibrate);
		}
	} else {
		printf("Unhandled tokens in ini file,\n"
				"\tSection %s\n\tAtttribute : %s\n\tValue: %s\n",
				"FMComms1", attrib, value);
		if (value)
			return "FAIL";
	}

	return NULL;
}

static const char *fmcomms1_sr_attribs[] = {
	"cf-ad9122-core-lpc.out_altvoltage_1A_sampling_frequency",
	"cf-ad9122-core-lpc.out_altvoltage_interpolation_frequency",
	"cf-ad9122-core-lpc.out_altvoltage_interpolation_center_shift_frequency",
	"dds_mode",
	"cf-ad9122-core-lpc.out_altvoltage0_1A_frequency",
	"cf-ad9122-core-lpc.out_altvoltage2_2A_frequency",
	"cf-ad9122-core-lpc.out_altvoltage1_1B_frequency",
	"cf-ad9122-core-lpc.out_altvoltage3_2B_frequency",
	"cf-ad9122-core-lpc.out_altvoltage0_1A_scale",
	"cf-ad9122-core-lpc.out_altvoltage2_2A_scale",
	"cf-ad9122-core-lpc.out_altvoltage1_1B_scale",
	"cf-ad9122-core-lpc.out_altvoltage3_2B_scale",
	"cf-ad9122-core-lpc.out_altvoltage0_1A_phase",
	"cf-ad9122-core-lpc.out_altvoltage1_1B_phase",
	"cf-ad9122-core-lpc.out_altvoltage2_2A_phase",
	"cf-ad9122-core-lpc.out_altvoltage3_2B_phase",
	"adf4351-tx-lpc.out_altvoltage0_frequency",
	"adf4351-tx-lpc.out_altvoltage0_powerdown",
	"adf4351-tx-lpc.out_altvoltage0_frequency_resolution",
	"cf-ad9122-core-lpc.out_voltage0_calibbias",
	"cf-ad9122-core-lpc.out_voltage0_calibscale",
	"cf-ad9122-core-lpc.out_voltage0_phase",
	"cf-ad9122-core-lpc.out_voltage1_calibbias",
	"cf-ad9122-core-lpc.out_voltage1_calibscale",
	"cf-ad9122-core-lpc.out_voltage1_phase",
	"cf-ad9643-core-lpc.in_voltage0_calibbias",
	"cf-ad9643-core-lpc.in_voltage1_calibbias",
	"cf-ad9643-core-lpc.in_voltage0_calibscale",
	"cf-ad9643-core-lpc.in_voltage1_calibscale",
	"cf-ad9643-core-lpc.in_voltage0_calibphase",
	"cf-ad9643-core-lpc.in_voltage1_calibphase",
	"adf4351-rx-lpc.out_altvoltage0_frequency_resolution",
	"adf4351-rx-lpc.out_altvoltage0_frequency",
	"adf4351-rx-lpc.out_altvoltage0_powerdown",
	"ad8366-lpc.out_voltage0_hardwaregain",
	"ad8366-lpc.out_voltage1_hardwaregain",
	SYNC_RELOAD,
	NULL,
};

static bool fmcomms1_identify(void)
{
	return !set_dev_paths("cf-ad9122-core-lpc");
}

const struct osc_plugin plugin = {
	.name = "FMComms1",
	.identify = fmcomms1_identify,
	.init = fmcomms1_init,
	.save_restore_attribs = fmcomms1_sr_attribs,
	.handle_item = handle_item,

};
