/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <glib.h>
#include <gtkdatabox.h>
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

#include "../libini2.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "scpi.h"
#include "dac_data_manager.h"

#ifndef MAXFLOAT
#define MAXFLOAT HUGE
#endif

#define THIS_DRIVER "FMComms1"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

static const gdouble mhz_scale = 1000000.0;

static OscPlot *plot_fft_2ch;
static struct dac_data_manager *dac_tx_manager;

#define VERSION_SUPPORTED 1
static struct fmcomms1_calib_data_v1 *cal_data = NULL;
static struct fmcomms1_calib_header_v1 *cal_header = NULL;

static GtkWidget *vga_gain0, *vga_gain1;
static GtkAdjustment *adj_gain0, *adj_gain1;
static GtkWidget *rf_out;
static GtkWidget *gain_locked;

static GtkWidget *dds1_freq, *dds2_freq, *dds3_freq, *dds4_freq;
static GtkWidget *dds1_scale, *dds2_scale, *dds3_scale, *dds4_scale;
static GtkWidget *dds1_phase, *dds2_phase, *dds3_phase, *dds4_phase;
static int rx_lo_powerdown, tx_lo_powerdown;

static GtkWidget *dac_data_clock;
static GtkWidget *dac_interpolation;
static GtkWidget *dac_shift;

static GtkWidget *rx_lo_freq, *tx_lo_freq;

static GtkWidget *avg_I, *avg_Q;
static GtkWidget *span_I, *span_Q;
static GtkWidget *radius_IQ, *angle_IQ;

static GtkWidget *load_eeprom;

static struct iio_context *ctx;
static struct iio_device *dac, *adc, *txpll, *rxpll, *vga;

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static struct iio_widget cal_widgets[100];
static unsigned int num_tx, num_rx, num_cal,
		num_adc_freq, num_dac_freq, num_dac_shift,
		num_dac_interpolation;

static struct iio_device *adc_freq_device;
static struct iio_channel *adc_freq_channel;
static const char *adc_freq_file;

static int num_tx_pll, num_rx_pll;

static bool can_update_widgets;

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
static struct marker_type *rx_marker = NULL;

static GtkWidget *ad9122_temp;

static const char *fmcomms1_sr_attribs[] = {
	"cf-ad9122-core-lpc.out_altvoltage_1A_sampling_frequency",
	"cf-ad9122-core-lpc.out_altvoltage_sampling_frequency",
	"cf-ad9122-core-lpc.out_altvoltage_interpolation_frequency",
	"cf-ad9122-core-lpc.out_altvoltage_interpolation_center_shift_frequency",
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
	"cf-ad9643-core-lpc.in_voltage_sampling_frequency",
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
};

static const char *fmcomms1_driver_attribs[] = {
	"dds_mode",
	"tx_channel_0",
	"tx_channel_1",
	"dac_buf_filename",
	"calibrate_rx_level",
	"cal_clear",
	"cal_add",
	"cal_save",
	"calibrate_rx",
	"calibrate_tx",
	"gain_locked",
};

static int kill_thread;
static int fmcomms1_cal_eeprom(void);

static struct s_cal_eeprom_v1 {
	struct fmcomms1_calib_header_v1 header;
	struct fmcomms1_calib_data_v1 data[
		(MAX_SIZE_CAL_EEPROM - sizeof(struct fmcomms1_calib_header_v1)) /
		sizeof(struct fmcomms1_calib_data_v1)];
} __attribute__((packed)) cal_eeprom_v1;

static unsigned short temp_calibbias;

static int oneover(const gchar *num)
{
	float close;

	close = powf(2.0, roundf(log2f(1.0 / atof(num))));
	return (int)close;

}

static void dac_interpolation_update(void)
{
	tx_widgets[num_dac_interpolation].update(&tx_widgets[num_dac_interpolation]);
}

static void dac_shift_update(void)
{
	tx_widgets[num_dac_shift].update(&tx_widgets[num_dac_shift]);
}

static void rf_out_update(void)
{
	char buf[1024], dds1_m[16], dds2_m[16];
	static GtkTextBuffer *tbuf = NULL;
	GtkTextIter iter;
	float dac_shft = 0, dds1, dds2, tx_lo;
	unsigned dds_mode;
	int val;

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

	val = -1;
	if (GTK_IS_COMBO_BOX_TEXT(dds1_scale)) {
		if(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds1_scale)))
			val = oneover(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds1_scale)));
	} else if (GTK_IS_SPIN_BUTTON(dds1_scale)) {
		val = oneover(gtk_entry_get_text(GTK_ENTRY(dds1_scale)));
	}

	if (!(val < 0))
		sprintf(dds1_m, "1/%i", val);
	else
		sprintf(dds1_m, "?");

	val = -1;
	if (GTK_IS_COMBO_BOX_TEXT(dds2_scale)) {
		if(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds2_scale)))
			val = oneover(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(dds2_scale)));
	} else if (GTK_IS_SPIN_BUTTON(dds2_scale)) {
		val = oneover(gtk_entry_get_text(GTK_ENTRY(dds2_scale)));
	}

	if (!(val < 0))
		sprintf(dds2_m, "1/%i", val);
	else
		sprintf(dds2_m, "?");


	dds_mode = dac_data_manager_get_dds_mode(dac_tx_manager, "cf-ad9122-core-lpc", 1);
	if (dds_mode == DDS_ONE_TONE ||
			dds_mode == DDS_TWO_TONE) {
		sprintf(buf, "%4.3f MHz : Image\n", tx_lo - dds1 - dac_shft);
		gtk_text_buffer_insert(tbuf, &iter, buf, -1);
	}
	if (dds_mode == 2) {
		sprintf(buf, "%4.3f MHz : Image\n", tx_lo - dds2 - dac_shft);
		gtk_text_buffer_insert(tbuf, &iter, buf, -1);
	}

	sprintf(buf, "%4.3f MHz : LO Leakage\n", tx_lo);
	gtk_text_buffer_insert(tbuf, &iter, buf, -1);

	switch(dds_mode) {
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

static void rx_freq_info_update(void)
{
	double lo_freq;

	if (!adc)
		return;

	rx_update_device_sampling_freq("cf-ad9643-core-lpc",
		USE_INTERN_SAMPLING_FREQ);
	lo_freq = mhz_scale * gtk_spin_button_get_value(
				GTK_SPIN_BUTTON(rx_lo_freq));
	rx_update_channel_lo_freq("cf-ad9643-core-lpc", "all", lo_freq);
}

static void rf_out_update_on_complete(void *data)
{
	rf_out_update();
}

static void rx_update_labels_on_complete(void *data)
{
	rx_freq_info_update();
}

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
	rf_out_update();
}

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_freq_info_update();
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

static void tx_sample_rate_changed(void *data)
{
	GtkSpinButton *dac_freq_spin;
	gdouble rate;
	GtkWidget *tone_freq;
	unsigned tone;

	dac_freq_spin = GTK_SPIN_BUTTON(tx_widgets[num_dac_freq].widget);
	rate = gtk_spin_button_get_value(dac_freq_spin) / 2.0;
	dac_data_manager_freq_widgets_range_update(dac_tx_manager, rate);

	for (tone = dac_data_manager_dds_tone(0, TONE_1, TONE_I);
		tone <= dac_data_manager_dds_tone(0, TONE_2, TONE_Q); tone++) {
		tone_freq = dac_data_manager_get_widget(dac_tx_manager,
				tone, WIDGET_FREQUENCY);
		g_signal_emit_by_name(tone_freq, "value-changed", NULL);
	}
}

static struct fmcomms1_calib_data_v1 *find_entry(struct fmcomms1_calib_data_v1 *data,
					 struct fmcomms1_calib_header_v1 *header,
					 unsigned f)
{
	int ind = 0;
	int delta, gindex = 0;
	int min_delta = 2147483647;

	if (!header && !data)
		return NULL;

	for (ind = 0; ind < header->num_entries; ind++) {
			if (f > data->cal_frequency_MHz)
				delta = f - data->cal_frequency_MHz;
			else
				delta = data->cal_frequency_MHz - f;
			if (delta < min_delta) {
				gindex = ind;
				min_delta = delta;
			}
	}

	return &data[gindex];
}

static void store_entry_hw(struct fmcomms1_calib_data_v1 *data, unsigned tx, unsigned rx)
{
	if (!data)
		return;

	if (tx) {
		struct iio_channel *ch0 = iio_device_find_channel(dac, "voltage0", true),
				   *ch1 = iio_device_find_channel(dac, "voltage1", true);
		iio_channel_attr_write_longlong(ch0, "calibbias", data->i_dac_offset);
		iio_channel_attr_write_longlong(ch0, "calibscale", data->i_dac_fs_adj);
		iio_channel_attr_write_longlong(ch0, "phase", data->i_phase_adj);
		iio_channel_attr_write_longlong(ch1, "calibbias", data->q_dac_offset);
		iio_channel_attr_write_longlong(ch1, "calibscale", data->q_dac_fs_adj);
		iio_channel_attr_write_longlong(ch1, "phase", data->q_phase_adj);
		cal_update_values();
	}

	if (rx) {
		struct iio_channel *ch0 = iio_device_find_channel(adc, "voltage0", false),
				   *ch1 = iio_device_find_channel(adc, "voltage1", false);
		iio_channel_attr_write_longlong(ch0, "calibbias", data->i_adc_offset_adj);
		iio_channel_attr_write_double(ch0, "calibscale", fract1_1_14_to_float(data->i_adc_gain_adj));
		iio_channel_attr_write_longlong(ch1, "calibbias", data->q_adc_offset_adj);
		iio_channel_attr_write_double(ch1, "calibscale", fract1_1_14_to_float(data->q_adc_gain_adj));
		iio_channel_attr_write_double(ch0, "calibphase", fract1_1_14_to_float(data->i_adc_phase_adj));
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

static void gain_amp_locked_cb(GtkToggleButton *btn, gpointer data)
{
	gdouble tmp;

	if(gtk_toggle_button_get_active(btn)) {
		tmp = gtk_spin_button_get_value(GTK_SPIN_BUTTON(vga_gain0));
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(vga_gain1), tmp);
		gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(vga_gain1), adj_gain0);
	} else {
		tmp = gtk_spin_button_get_value(GTK_SPIN_BUTTON(vga_gain0));
		gtk_adjustment_set_value(adj_gain1, tmp);
		gtk_spin_button_set_adjustment(GTK_SPIN_BUTTON(vga_gain1), adj_gain1);
	}
}

static void load_cal_eeprom()
{
	gdouble freq;
	freq = pll_get_freq(&tx_widgets[num_tx_pll]);
	store_entry_hw(find_entry(cal_data, cal_header,
			(unsigned) (freq / mhz_scale)), 1, 0);

	freq = pll_get_freq(&rx_widgets[num_rx_pll]);
	store_entry_hw(find_entry(cal_data, cal_header,
			(unsigned) (freq / mhz_scale)), 0, 1);
}

static bool cal_rx_flag = false;
static gfloat knob_max, knob_min, knob_steps;
static int delay;

static double find_min(GtkSpinButton *spin_button, int marker, gfloat min, gfloat max, gfloat step, gfloat noise_floor)
{
	gdouble i, level, min_level = FLT_MAX, min_value = 0;
	gdouble nfloor_min = max, nfloor_max = min;
	int bigger = -1;
	gdouble last_val = FLT_MAX;

	for (i = min; i <= max; i += (max - min)/step) {
		gdk_threads_enter();
		gtk_spin_button_set_value(spin_button, i);
		gdk_threads_leave();

		scpi_rx_trigger_sweep();
		scpi_rx_get_marker_level(1, true, &level);
		if (level <= min_level) {
			if (min_level != FLT_MAX)
				bigger = 0;
			min_level = level;
			min_value = i;
		}

		if (min_value != 0 && bigger != -1) {
			if (level > last_val)
				bigger++;
			else
				bigger = 0;
		}

		if (bigger == 5)
			break;

		if (level <= noise_floor) {
			if (nfloor_min > i)
				nfloor_min = i;
			if (nfloor_max < i)
				nfloor_max = i;
		}
		last_val = level;
	}

	/* wandering around the noise floor - pick the middle */
	if (nfloor_min != max && nfloor_max != min)
		min_value = (nfloor_min + nfloor_max) / 2;

	gdk_threads_enter();
	gtk_spin_button_set_value(spin_button, min_value);
	gdk_threads_leave();

	return min_value;
}

static void tx_thread_cal(void *ptr)
{
	gdouble min_i, min_q, tmp, min_fsi, min_fsq, noise;
	unsigned long long lo, sig;

	gdk_threads_enter();
	/* LO Leakage */
	lo = (unsigned long long)gtk_spin_button_get_value (GTK_SPIN_BUTTON(tx_lo_freq)) * 1000000;
	sig = (unsigned long long)gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_freq)) * 1000000;

	/* set some default values */
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_dac_offs), 0.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_dac_offs), 0.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_dac_pha_adj), 0.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_dac_pha_adj), 0.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_dac_fs_adj), 512.0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_dac_fs_adj), 512.0);

	gdk_threads_leave();

	scpi_rx_trigger_sweep();
	scpi_rx_trigger_sweep();

	/* find the noise floor */
	scpi_rx_trigger_sweep();
	scpi_rx_set_center_frequency(lo);
	scpi_rx_set_span_frequency(sig * 2);
	scpi_rx_set_marker_freq(1, lo + sig / 2);
	scpi_rx_get_marker_level(1, true, &noise);
	/* noise thresold is 2dB up */
	noise += 2;

	/* rough approximation of carrier supression */
	scpi_rx_set_center_frequency(lo);
	scpi_rx_set_span_frequency(2000000);
	scpi_rx_set_marker_freq(1, lo);

	min_i = find_min(GTK_SPIN_BUTTON(I_dac_offs), 1, -500, 500, 25, noise);
	min_q = find_min(GTK_SPIN_BUTTON(Q_dac_offs), 1, -500, 500, 25, noise);

	/* side band supression */
	scpi_rx_set_center_frequency(lo - sig);
	scpi_rx_set_marker_freq(1, lo - sig);

	tmp = find_min(GTK_SPIN_BUTTON(I_dac_pha_adj), 1, -512, 512, 25, noise);
	tmp += find_min(GTK_SPIN_BUTTON(Q_dac_pha_adj), 1, -512, 512, 25, noise);

	min_fsi = find_min(GTK_SPIN_BUTTON(I_dac_fs_adj), 1, 400, 600, 10, noise);
	min_fsq = find_min(GTK_SPIN_BUTTON(Q_dac_fs_adj), 1, 400, 600, 10, noise);

	find_min(GTK_SPIN_BUTTON(I_dac_fs_adj), 1, min_fsi - 10, min_fsi + 10, 20, noise);
	find_min(GTK_SPIN_BUTTON(Q_dac_fs_adj), 1, min_fsq - 10, min_fsq + 10, 20, noise);

	if (tmp != -1024) {
		gdk_threads_enter();
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_dac_pha_adj), tmp / 2);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_dac_pha_adj), tmp / 2);
		gdk_threads_leave();

		find_min(GTK_SPIN_BUTTON(I_dac_pha_adj), 1, tmp / 2 - 20, tmp / 2 + 20, 40, noise);
		find_min(GTK_SPIN_BUTTON(Q_dac_pha_adj), 1, tmp / 2 - 20, tmp / 2 + 20, 40, noise);
	}

	/* go back to carrier, and do it in smaller steps */
	scpi_rx_set_center_frequency(lo);
	scpi_rx_set_marker_freq(1, lo);

	min_i = find_min(GTK_SPIN_BUTTON(I_dac_offs), 1, min_i - 40, min_i + 40, 20.0, noise);
	min_q = find_min(GTK_SPIN_BUTTON(Q_dac_offs), 1, min_q - 40, min_q + 40, 20.0, noise);

	find_min(GTK_SPIN_BUTTON(I_dac_offs), 1, min_i - 10, min_i + 10, 20.0, noise);
	find_min(GTK_SPIN_BUTTON(Q_dac_offs), 1, min_q - 10, min_q + 10, 20.0, noise);

	scpi_rx_set_span_frequency(3 * sig);
	scpi_rx_trigger_sweep();
	scpi_rx_trigger_sweep();

	kill_thread = 1;
}

static GThread * cal_tx_button_clicked(void)
{
	if (!scpi_rx_connected()) {
		printf("not connected\n");
		return NULL;
	}

	/* make sure it's a single tone */
	if ((gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_freq)) !=
				gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds2_freq))) ||
			(gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_freq)) !=
				gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds3_freq))) ||
			(gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_freq)) !=
				gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds4_freq)))) {
		printf("not a tone\n");
		return NULL;
	}

	scpi_rx_setup();
	scpi_rx_set_center_frequency(gtk_spin_button_get_value (GTK_SPIN_BUTTON(tx_lo_freq)) * 1000000 + 1000000);
	scpi_rx_set_span_frequency(3 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_freq)) * 1000000);
	scpi_rx_set_bandwith(200, 10);
	scpi_rx_set_averaging(2);
	scpi_rx_trigger_sweep();

	return g_thread_new("Tx calibration thread", (void *) &tx_thread_cal, NULL);
}

static void cal_rx_button_clicked(void)
{
	OscPlot *fft_plot = plugin_find_plot_with_domain(FFT_PLOT);
	cal_rx_flag = true;

	delay = 2 * plugin_data_capture_size(NULL) *
		plugin_get_plot_fft_avg(fft_plot, NULL);

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_adc_phase_adj), 0);

	knob_steps = 10;
	knob_max = 1;
	knob_min = -1;
	gtk_widget_hide(cal_rx);
}

static gboolean display_temp(void *ptr)
{
	double temp, tmp;
	struct iio_channel *chn = iio_device_find_channel(dac, "temp0", false);

	if (kill_thread)
		return FALSE;

	if (iio_channel_attr_read_double(chn, "input", &temp) < 0) {
		/* Just assume it's 25C, units are in milli-degrees C */
		temp = 25 * 1000;
		iio_channel_attr_write_double(chn, "input", temp);
		iio_channel_attr_read_double(chn, "calibbias", &tmp);
		/* This will eventually be stored in the EEPROM */
		temp_calibbias = (unsigned short) tmp;
		printf("AD9122 temp cal value : %hu\n", temp_calibbias);
	} else {
		char buf[25];
		sprintf(buf, "%2.1f", temp/1000);
		gtk_label_set_text(GTK_LABEL(ad9122_temp), buf);
	}

	return TRUE;
}

#define RX_CAL_THRESHOLD -75

static void display_cal(void *ptr)
{
	int size, channels, num_samples, i;
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
	OscPlot *fft_plot = NULL;
	double ln10 = log(10.0);

	device_ref = plugin_get_device_by_reference("cf-ad9643-core-lpc");
	if (!device_ref)
		goto display_call_ret;

	if (!rx_marker) {
		rx_marker = g_new(struct marker_type, 3);
		rx_marker[0].active = false;
	}

	while (!kill_thread) {
		size = plugin_data_capture_size(device_ref);
		channels = plugin_data_capture_num_active_channels(device_ref);
		i = plugin_data_capture_bytes_per_sample(device_ref);
		if (i)
			num_samples = size / i;
		else
			num_samples = 0;

		fft_plot = plot_fft_2ch;

		if (size != 0 && channels == 2) {
			gdk_threads_enter();
			if (show && !cal_rx_flag)
				gtk_widget_show(cal_rx);
			else
				gtk_widget_hide(cal_rx);
			gdk_threads_leave();

			/* grab the data */
			if (cal_rx_flag && cal_rx_level &&
					plugin_get_plot_marker_type(fft_plot, device_ref) == MARKER_IMAGE) {
				do {
					ret = plugin_data_capture_of_plot(fft_plot, device_ref, &cooked_data, &markers);
				} while ((ret == -EBUSY) && !kill_thread);
			} else {
				do {
					ret = plugin_data_capture_of_plot(fft_plot, device_ref, &cooked_data, NULL);
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
					gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_offset_adj)) -
					avg_y);
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_adc_offset_adj),
					gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_offset_adj)) -
					avg_x);

				/* Scale connection */
				span_I_set = gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_gain_adj));
				span_Q_set = gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_gain_adj));
				span_I_val = (max_y - min_y) / span_I_set;
				span_Q_val = (max_x - min_x) / span_Q_set;

				if (cal_rx_level &&
						plugin_get_plot_marker_type(fft_plot, device_ref) == MARKER_IMAGE) {
					if (attempt == 0)
						gain = (span_I_set + span_I_set) / 2;
					gain *= 1.0 / exp(ln10 * (double) ((markers[0].y - cal_rx_level) / 20));
				}

				gtk_spin_button_set_value(GTK_SPIN_BUTTON(I_adc_gain_adj),
					(span_I_val + span_Q_val)/(2.0 * span_I_val) * gain);
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(Q_adc_gain_adj),
					(span_I_val + span_Q_val)/(2.0 * span_Q_val) * gain);
				cal_save_values();
				gdk_threads_leave ();

				usleep(delay);
				if (plugin_get_plot_marker_type(fft_plot, device_ref) != MARKER_IMAGE)
					cal_rx_flag = false;
			}

			if (cal_rx_flag) {
				int bigger = 0;
				gfloat last_val = FLT_MAX;

				if (attempt == 0) {
					/* if the current value is OK, we leave it alone */
					do {
						ret = plugin_data_capture_of_plot(fft_plot, device_ref, NULL, &markers);
					} while ((ret == -EBUSY) && !kill_thread);

					/* If the lock is broken, then die nicely */
					if (kill_thread || ret != 0) {
						size = 0;
						kill_thread = 1;
						break;
					}

					/* make sure image, and DC are below */
					if ((markers[2].y <= RX_CAL_THRESHOLD) && (markers[1].y <= RX_CAL_THRESHOLD)) {
						if (rx_marker)
							memcpy(rx_marker, markers, sizeof(struct marker_type) * 3);
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
						ret = plugin_data_capture_of_plot(fft_plot, device_ref, NULL, &markers);
					} while ((ret == -EBUSY) && !kill_thread);

					/* If the lock is broken, then die nicely */
					if (kill_thread || ret != 0) {
						size = 0;
						kill_thread = 1;
						break;
					}

					if (markers[2].y <= knob_min_value) {
						if (rx_marker)
							memcpy(rx_marker, markers, sizeof(struct marker_type) * 3);
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
		} else {
			/* wait 10 ms */
			usleep(10000);
		}
	}

display_call_ret:
	/* free the buffers */
	if (cooked_data || markers)
		plugin_data_capture_of_plot(fft_plot, NULL, &cooked_data, &markers);
	kill_thread = 1;
	g_thread_exit(NULL);
}


static char * get_filename(char *name, bool load)
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

static void dds_scale_set_string_value(GtkWidget *scale, const char *value)
{
	if (GTK_IS_COMBO_BOX_TEXT(scale)) {
		combo_box_set_active_text(scale, value);
	} else if (GTK_IS_SPIN_BUTTON(scale)){
		gtk_entry_set_text(GTK_ENTRY(scale), value);
	}
}

static const char *dds_scale_get_string_value(GtkWidget *scale)
{
	if (GTK_IS_COMBO_BOX_TEXT(scale)) {
		return gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(scale));
	} else if (GTK_IS_SPIN_BUTTON(scale)) {
		return gtk_entry_get_text(GTK_ENTRY(scale));
	}

	return NULL;
}

static int parse_cal_handler(int line, const char* section,
		const char* name, const char* value)
{

	if (MATCH_SECT("SYS_SETTINGS")) {
		if(MATCH_NAME(RX_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(rx_lo_freq), atof(value));
		else if (MATCH_NAME(TX_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(tx_lo_freq), atof(value));

		else if (MATCH_NAME(DDS1_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds1_freq), atof(value));
		else if (MATCH_NAME(DDS1_S))
			dds_scale_set_string_value(dds1_scale, value);
		else if (MATCH_NAME(DDS1_P))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds1_phase), atof(value));
		else if (MATCH_NAME(DDS2_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds2_freq), atof(value));
		else if (MATCH_NAME(DDS2_S))
			dds_scale_set_string_value(dds2_scale, value);
		else if (MATCH_NAME(DDS2_P))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds2_phase), atof(value));

		else if (MATCH_NAME(DDS3_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_freq), atof(value));
		else if (MATCH_NAME(DDS3_S))
			dds_scale_set_string_value(dds3_scale, value);
		else if (MATCH_NAME(DDS3_P))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_phase), atof(value));

		else if (MATCH_NAME(DDS4_F))
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_freq), atof(value));
		else if (MATCH_NAME(DDS4_S))
			dds_scale_set_string_value(dds4_scale, value);
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
	return 0;
}

static void load_cal(char * resfile)
{
	foreach_in_ini(resfile, parse_cal_handler);
}

static int cal_entry_add(struct s_cal_eeprom_v1 *eeprom)
{
	int i = eeprom->header.num_entries;

	eeprom->data[i].cal_frequency_MHz = (short) gtk_spin_button_get_value (GTK_SPIN_BUTTON(rx_lo_freq));

	eeprom->data[i].i_phase_adj       = (short) gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_dac_pha_adj));
	eeprom->data[i].q_phase_adj       = (short) gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_dac_pha_adj));
	eeprom->data[i].i_dac_offset      = (short) gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_dac_offs));
	eeprom->data[i].q_dac_offset      = (short) gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_dac_offs));
	eeprom->data[i].i_dac_fs_adj      = (unsigned short) gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_dac_fs_adj));
	eeprom->data[i].q_dac_fs_adj      = (unsigned short) gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_dac_fs_adj));

	eeprom->data[i].i_adc_offset_adj  = (short) gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_offset_adj));
	eeprom->data[i].q_adc_offset_adj  = (short) gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_offset_adj));
	eeprom->data[i].i_adc_gain_adj    = float_to_fract1_1_14(gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_gain_adj)));
	eeprom->data[i].q_adc_gain_adj    = float_to_fract1_1_14(gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_gain_adj)));
	eeprom->data[i].i_adc_phase_adj   = float_to_fract1_1_14(gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_phase_adj)));

	eeprom->header.num_entries++;

	eeprom->header.adi_magic0 = ADI_MAGIC_0;
	eeprom->header.adi_magic1 = ADI_MAGIC_1;
	eeprom->header.version = ADI_VERSION(VERSION_SUPPORTED);
	eeprom->header.temp_calibbias = temp_calibbias;

	return 0;
}

static int cal_save_to_eeprom(struct s_cal_eeprom_v1 *eeprom)
{
	FILE* file;
	size_t num;

	file = fopen("/sys/bus/i2c/devices/1-0054/eeprom", "w"); /* FIXME */
	if (!file)
		return -errno;

	num = fwrite(eeprom, sizeof(*eeprom), 1, file);

	fclose(file);

	if (num != sizeof(*eeprom))
		return -EIO;

	return 0;
}

static void save_cal(char * resfile)
{
	FILE* file;
	time_t clock = time(NULL);

	file = fopen(resfile, "w");
	if (!file)
		return;

	fprintf(file, ";Calibration time: %s\n", ctime(&clock));

	fprintf(file, "[SYS_SETTINGS]\n");
	fprintf(file, "%s = %f\n", RX_F, gtk_spin_button_get_value (GTK_SPIN_BUTTON(rx_lo_freq)));
	fprintf(file, "%s = %f\n", TX_F, gtk_spin_button_get_value (GTK_SPIN_BUTTON(tx_lo_freq)));
	fprintf(file, "dds_mode = %i", dac_data_manager_get_dds_mode(dac_tx_manager, "cf-ad9122-core-lpc", 1));
	fprintf(file, "%s = %f\n", DDS1_F, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_freq)));
	fprintf(file, "%s = %s\n", DDS1_S, dds_scale_get_string_value(dds1_scale));
	fprintf(file, "%s = %f\n", DDS1_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_phase)));
	fprintf(file, "%s = %f\n", DDS2_F, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds2_freq)));
	fprintf(file, "%s = %s\n", DDS2_S, dds_scale_get_string_value(dds2_scale));
	fprintf(file, "%s = %f\n", DDS2_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds2_phase)));
	fprintf(file, "%s = %f\n", DDS3_F, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds3_freq)));
	fprintf(file, "%s = %s\n", DDS3_S, dds_scale_get_string_value(dds3_scale));
	fprintf(file, "%s = %f\n", DDS3_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds3_phase)));
	fprintf(file, "%s = %f\n", DDS4_F, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds4_freq)));
	fprintf(file, "%s = %s\n", DDS4_S, dds_scale_get_string_value(dds4_scale));
	fprintf(file, "%s = %f\n", DDS4_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds4_phase)));

	fprintf(file, "\n[DAC_SETTINGS]\n");
	fprintf(file, "%s = %f\n", DAC_I_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_dac_pha_adj)));
	fprintf(file, "%s = %f\n", DAC_Q_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_dac_pha_adj)));
	fprintf(file, "%s = %f\n", DAC_I_O, gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_dac_offs)));
	fprintf(file, "%s = %f\n", DAC_Q_O, gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_dac_offs)));
	fprintf(file, "%s = %f\n", DAC_I_G, gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_dac_fs_adj)));
	fprintf(file, "%s = %f\n", DAC_Q_G, gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_dac_fs_adj)));

	if (0) {
		 fprintf(file, "\n[TX_RESULTS]\n");
	}

	fprintf(file, "\n[ADC_SETTINGS]\n");
	fprintf(file, "%s = %i\n", ADC_I_O, (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_offset_adj)));
	fprintf(file, "%s = %i\n", ADC_Q_O, (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_offset_adj)));
	fprintf(file, "%s = %f #0x%x\n", ADC_I_G, gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_gain_adj)),
				float_to_fract1_1_14(gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_gain_adj))));
	fprintf(file, "%s = %f #0x%x\n", ADC_Q_G, gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_gain_adj)),
				float_to_fract1_1_14(gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_gain_adj))));
	fprintf(file, "%s = %f #0x%x\n", ADC_I_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_phase_adj)),
				float_to_fract1_1_14(gtk_spin_button_get_value(GTK_SPIN_BUTTON(I_adc_phase_adj))));
	fprintf(file, "%s = %f #0x%x\n", ADC_Q_P, gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_phase_adj)),
				float_to_fract1_1_14(gtk_spin_button_get_value(GTK_SPIN_BUTTON(Q_adc_phase_adj))));

	if (rx_marker && rx_marker[0].active) {
		fprintf(file, "\n[RX_RESULTS]\n");
		fprintf(file, "Signal = %lf dBFS @ %lf MHz from carrier\n", rx_marker[0].y, rx_marker[0].x);
		fprintf(file, "Carrier = %lf dBFS\n", rx_marker[1].y);
		fprintf(file, "Sideband = %lf dBFS\n", rx_marker[2].y);
		fprintf(file, "Carrier Suppression = %lf dBc\n", rx_marker[1].y - rx_marker[0].y);
		fprintf(file, "Sideband Suppression = %lf dBc\n", rx_marker[2].y - rx_marker[0].y);
	}

	/* Don't have this info yet
	 * fprintf(file, "\n[SAVE]\n");
	 * fprintf(file, "PlotFile = %s\n", plotfile);
	 */

	fclose(file);
	return;

}

static bool calib_plot_exists;

static void calib_plot_destroyed_cb(OscPlot *plot)
{
	calib_plot_exists = false;
	g_signal_emit_by_name(GTK_DIALOG(dialogs.calibrate), "response", -7);
}

static void cal_dialog(GtkButton *btn, Dialogs *data)
{
	gint ret;
	char *filename = NULL;
	GThread *thid_rx = NULL;

	kill_thread = 0;

	/* Create a fft plot to run in background while calibrating */
	plot_fft_2ch = plugin_get_new_plot();
	if (!plot_fft_2ch) {
		printf("Could not open a new plot\n");
		goto hide_calib;
	}
	calib_plot_exists = true;
	g_signal_connect(plot_fft_2ch, "osc-destroy-event", G_CALLBACK(calib_plot_destroyed_cb), NULL);

	osc_plot_set_channel_state(plot_fft_2ch, "cf-ad9643-core-lpc", 0, true);
	osc_plot_set_channel_state(plot_fft_2ch, "cf-ad9643-core-lpc", 1, true);
	osc_plot_set_domain(plot_fft_2ch, FFT_PLOT);
	osc_plot_set_marker_type(plot_fft_2ch, MARKER_IMAGE);
	osc_plot_draw_start(plot_fft_2ch);

	/* Only start the thread if the LO is set */
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rx_widgets[rx_lo_powerdown].widget)))
		thid_rx = g_thread_new("Display_thread", (void *) &display_cal, NULL);

	gtk_widget_show(dialogs.calibrate);

	gtk_widget_hide(cal_rx);

	if (!scpi_connect_functions())
		gtk_widget_hide(cal_tx);

	if (fmcomms1_cal_eeprom() < 0)
		gtk_widget_hide(load_eeprom);

	g_timeout_add(500, &display_temp, data);

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
				cal_tx_button_clicked();
				break;
			case 6: /* Cal Rx side */
				cal_rx_button_clicked();
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
	/* Stop capturing in order to unlock the buffer_full mutex otherwise
	 this thread will suspend the capture while waiting for the display_cal
	 to die which won't die until it will get one last batch of data. */
	if (calib_plot_exists)
		osc_plot_draw_stop(plot_fft_2ch);
	g_source_remove_by_user_data(data);

	if (thid_rx)
		g_thread_join(thid_rx);

	if (filename)
		g_free(filename);
	if (calib_plot_exists) {
		osc_plot_destroy(plot_fft_2ch);
	}

hide_calib:
	gtk_widget_hide(dialogs.calibrate);
}

static int fmcomms1_cal_eeprom_v0_convert(char *ptr)
{
	char tmp[FAB_SIZE_CAL_EEPROM];
	struct fmcomms1_calib_data *data;
	struct fmcomms1_calib_header_v1 *header =
		(struct fmcomms1_calib_header_v1 *) ptr;
	struct fmcomms1_calib_data_v1 *data_v1 =
		(struct fmcomms1_calib_data_v1 *)(ptr + sizeof(*header));
	unsigned ind = 0;

	memcpy(tmp, ptr, FAB_SIZE_CAL_EEPROM);
	memset(ptr, 0, FAB_SIZE_CAL_EEPROM);

	data = (struct fmcomms1_calib_data *) tmp;

	do {
		if (data->adi_magic0 != ADI_MAGIC_0 || data->adi_magic1 != ADI_MAGIC_1) {
			fprintf (stderr, "invalid magic detected\n");
			return -EINVAL;
		}
		if (data->version != ADI_VERSION(0)) {
			fprintf (stderr, "unsupported version detected %c\n", data->version);
			return -EINVAL;
		}

		data_v1->cal_frequency_MHz = data->cal_frequency_MHz;
		data_v1->i_phase_adj       = data->i_phase_adj;
		data_v1->q_phase_adj       = data->q_phase_adj;
		data_v1->i_dac_offset      = data->i_dac_offset;
		data_v1->q_dac_offset      = data->q_dac_offset;
		data_v1->i_dac_fs_adj      = data->i_dac_fs_adj;
		data_v1->q_dac_fs_adj      = data->q_dac_fs_adj;
		data_v1->i_adc_offset_adj  = data->i_adc_offset_adj;
		data_v1->q_adc_offset_adj  = data->q_adc_offset_adj;
		data_v1->i_adc_gain_adj    = float_to_fract1_1_14(fract1_15_to_float(data->i_adc_gain_adj));
		data_v1->q_adc_gain_adj    = float_to_fract1_1_14(fract1_15_to_float(data->q_adc_gain_adj));
		data_v1->i_adc_phase_adj   = 0.0;
		data_v1++;
		ind++;
	} while (data++->next);

	header->adi_magic0 = ADI_MAGIC_0;
	header->adi_magic1 = ADI_MAGIC_1;
	header->version = ADI_VERSION(VERSION_SUPPORTED);
	header->num_entries = ind;
	header->temp_calibbias = 0;

	return 0;
}

static int fmcomms1_cal_eeprom(void)
{
	char eprom_names[512];
	FILE *efp, *fp;
	int tmp;

	/* flushes all open output streams */
	fflush(NULL);

	if (!cal_header)
		cal_header = malloc(FAB_SIZE_CAL_EEPROM);

	if (cal_header == NULL) {
		return -ENOMEM;
	}

	fp = popen("find /sys -name eeprom 2>/dev/null", "r");

	if(fp == NULL) {
		fprintf(stderr, "can't execute find\n");
		return -errno;
	}

	while(fgets(eprom_names, sizeof(eprom_names), fp) != NULL){
		/* strip trailing new lines */
		if (eprom_names[strlen(eprom_names) - 1] == '\n')
			eprom_names[strlen(eprom_names) - 1] = '\0';

		efp = fopen(eprom_names, "rb");
		if(efp == NULL)
			return -errno;

		memset(cal_header, 0, FAB_SIZE_CAL_EEPROM);
		tmp = fread(cal_header, FAB_SIZE_CAL_EEPROM, 1, efp);
		fclose(efp);

		if (!tmp || cal_header->adi_magic0 != ADI_MAGIC_0 || cal_header->adi_magic1 != ADI_MAGIC_1) {
			continue;
		}

		if (cal_header->version != ADI_VERSION(VERSION_SUPPORTED)) {
			if (cal_header->version == ADI_VERSION(0)) {
				if (fmcomms1_cal_eeprom_v0_convert((char*) cal_header))
					continue;
			} else {
				continue;
			}
		}

		cal_data = (struct fmcomms1_calib_data_v1 *)((void *)cal_header + sizeof(*cal_header));

		fprintf (stdout, "Found Calibration EEPROM @ %s\n", eprom_names);
		pclose(fp);

		return 0;
	}

	pclose(fp);

	return -ENODEV;
}

static void dac_cal_spin_helper(GtkRange *range,
		struct iio_channel *chn, const char *attr)
{
	gdouble inc, val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(range));
	gtk_spin_button_get_increments(GTK_SPIN_BUTTON(range), &inc, NULL);

	if (inc == 1.0)
		iio_channel_attr_write_longlong(chn, attr, (long long) val);
	else
		iio_channel_attr_write_double(chn, attr, val);
}

static void dac_cal_spin0(GtkRange *range, gpointer user_data)
{
	dac_cal_spin_helper(range,
			iio_device_find_channel(dac, "voltage0", true),
			(const char *) user_data);
}

static void dac_cal_spin1(GtkRange *range, gpointer user_data)
{
	dac_cal_spin_helper(range,
			iio_device_find_channel(dac, "voltage1", true),
			(const char *) user_data);
}

static void adc_cal_spin_helper(GtkRange *range,
		struct iio_channel *chn, const char *attr)
{
	gdouble val, inc;

	val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(range));
	gtk_spin_button_get_increments(GTK_SPIN_BUTTON(range), &inc, NULL);

	if (inc == 1.0)
		iio_channel_attr_write_longlong(chn, attr, (long long) val);
	else
		iio_channel_attr_write_double(chn, attr, val);
}

static void adc_cal_spin0(GtkRange *range, gpointer user_data)
{
	adc_cal_spin_helper(range,
			iio_device_find_channel(adc, "voltage0", false),
			(const char *) user_data);
}

static void adc_cal_spin1(GtkRange *range, gpointer user_data)
{
	adc_cal_spin_helper(range,
			iio_device_find_channel(adc, "voltage1", false),
			(const char *) user_data);
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

static int fmcomms1_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	if (MATCH_ATTRIB("dds_mode")) {
		dac_data_manager_set_dds_mode(dac_tx_manager,
				"cf-ad9122-core-lpc", 1, atoi(value));
	} else if (MATCH_ATTRIB("tx_channel_0")) {
		dac_data_manager_set_tx_channel_state(dac_tx_manager,
				0, !!atoi(value));
	} else if (MATCH_ATTRIB("tx_channel_1")) {
		dac_data_manager_set_tx_channel_state(dac_tx_manager,
				1, !!atoi(value));
	} else if (MATCH_ATTRIB("dac_buf_filename")) {
		if (dac_data_manager_get_dds_mode(dac_tx_manager,
					"cf-ad9122-core-lpc", 1) == DDS_BUFFER)
			dac_data_manager_set_buffer_chooser_filename(
					dac_tx_manager, value);
	} else if (MATCH_ATTRIB("calibrate_rx_level")) {
		cal_rx_level = atof(value);
	} else if (MATCH_ATTRIB("cal_clear")) {
		memset(&cal_eeprom_v1, 0, sizeof(cal_eeprom_v1));
	} else if (MATCH_ATTRIB("cal_add")) {
		cal_entry_add(&cal_eeprom_v1);
	} else if (MATCH_ATTRIB("cal_save")) {
		cal_save_to_eeprom(&cal_eeprom_v1);
	} else if (MATCH_ATTRIB("calibrate_rx")) {
		if (atoi(value) == 1) {
			GThread *thr;
			unsigned int i = 0;

			gtk_widget_show(dialogs.calibrate);
			kill_thread = 0;
			cal_rx_button_clicked();
			thr = g_thread_new("Display_thread",
					(void *) &display_cal, (gpointer *) 1);
			while (i <= 20) {
				i += kill_thread;
				gtk_main_iteration();
			}
			g_thread_join(thr);
			cal_rx_flag = false;
			gtk_widget_hide(dialogs.calibrate);
		}
	} else if (MATCH_ATTRIB("calibrate_tx")) {
		if (atoi(value) == 1) {
			GThread *thr, *thid;
			unsigned int i = 0;

			scpi_connect_functions();
			gtk_widget_show(dialogs.calibrate);
			kill_thread = 0;
			thid = cal_tx_button_clicked();
			thr = g_thread_new("Display_thread",
					(void *) &display_cal, (gpointer *) 1);
			while (i <= 20) {
				i += kill_thread;
				gtk_main_iteration();
			}
			g_thread_join(thid);
			g_thread_join(thr);
			gtk_widget_hide(dialogs.calibrate);
		}
	} else if (MATCH_ATTRIB("gain_locked")) {
		gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(gain_locked), atoi(value));
	} else if (MATCH_ATTRIB("SYNC_RELOAD")) {
		tx_update_values();
		rx_update_values();
		dac_data_manager_update_iio_widgets(dac_tx_manager);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int fmcomms1_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			fmcomms1_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	unsigned int i;

	update_from_ini(ini_fn, THIS_DRIVER, dac, fmcomms1_sr_attribs,
			ARRAY_SIZE(fmcomms1_sr_attribs));
	if (adc)
		update_from_ini(ini_fn, THIS_DRIVER, adc, fmcomms1_sr_attribs,
				ARRAY_SIZE(fmcomms1_sr_attribs));
	if (txpll)
		update_from_ini(ini_fn, THIS_DRIVER, txpll,
				fmcomms1_sr_attribs,
				ARRAY_SIZE(fmcomms1_sr_attribs));
	if (rxpll)
		update_from_ini(ini_fn, THIS_DRIVER, rxpll,
				fmcomms1_sr_attribs,
				ARRAY_SIZE(fmcomms1_sr_attribs));
	if (vga)
		update_from_ini(ini_fn, THIS_DRIVER, vga,
				fmcomms1_sr_attribs,
				ARRAY_SIZE(fmcomms1_sr_attribs));

	for (i = 0; i < ARRAY_SIZE(fmcomms1_driver_attribs); i++) {
		char *value = read_token_from_ini(ini_fn, THIS_DRIVER,
				fmcomms1_driver_attribs[i]);
		if (value) {
			fmcomms1_handle_driver(NULL,
					fmcomms1_driver_attribs[i], value);
			free(value);
		}
	}

	if (can_update_widgets) {
		tx_update_values();
		rx_update_values();
		cal_update_values();
		dac_data_manager_update_iio_widgets(dac_tx_manager);
	}
}

static GtkWidget * fmcomms1_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *fmcomms1_panel;
	GtkWidget *dds_container;
	const char *dac_sampling_freq_file;
	struct iio_channel *ch0, *ch1;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	dac = iio_context_find_device(ctx, "cf-ad9122-core-lpc");
	adc = iio_context_find_device(ctx, "cf-ad9643-core-lpc");
	txpll = iio_context_find_device(ctx, "adf4351-tx-lpc"),
	rxpll = iio_context_find_device(ctx, "adf4351-rx-lpc"),
	vga = iio_context_find_device(ctx, "ad8366-lpc");

	dac_tx_manager = dac_data_manager_new(dac, NULL, ctx);
	if (!dac_tx_manager) {
		osc_destroy_context(ctx);
		return NULL;
	}

	builder = gtk_builder_new();

	if (osc_load_glade_file(builder, "fmcomms1") < 0)
		return NULL;

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

	gain_locked = GTK_WIDGET(gtk_builder_get_object(builder, "gain_amp_together"));

	dds_container = GTK_WIDGET(gtk_builder_get_object(builder, "dds_transmit_block"));
	gtk_container_add(GTK_CONTAINER(dds_container), dac_data_manager_get_gui_container(dac_tx_manager));
	gtk_widget_show_all(dds_container);

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
	dac_data_clock = GTK_WIDGET(gtk_builder_get_object(builder, "dac_data_clock"));
	dac_interpolation = GTK_WIDGET(gtk_builder_get_object(builder, "dac_interpolation_clock"));
	dac_shift = GTK_WIDGET(gtk_builder_get_object(builder, "dac_fcenter_shift"));

	if (!adc) {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "adc_frame")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "calibrate_dialog")));
	}
	if (!vga)
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "vga_frame")));
	if (!rxpll)
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "rx_lo_frame")));
	if (!adc && !vga && !rxpll)
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "rx_chain_frame")));
	if (!txpll)
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "tx_lo_box")));

	if (ini_fn)
		load_profile(NULL, ini_fn);

	dds1_freq = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_1, TONE_I), WIDGET_FREQUENCY);
	dds2_freq = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_2, TONE_I), WIDGET_FREQUENCY);
	dds3_freq = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_1, TONE_Q), WIDGET_FREQUENCY);
	dds4_freq = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_2, TONE_Q), WIDGET_FREQUENCY);

	dds1_scale = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_1, TONE_I), WIDGET_SCALE);
	dds2_scale = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_2, TONE_I), WIDGET_SCALE);
	dds3_scale = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_1, TONE_Q), WIDGET_SCALE);
	dds4_scale = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_2, TONE_Q), WIDGET_SCALE);

	dds1_phase = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_1, TONE_I), WIDGET_PHASE);
	dds2_phase = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_2, TONE_I), WIDGET_PHASE);
	dds3_phase = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_1, TONE_Q), WIDGET_PHASE);
	dds4_phase = dac_data_manager_get_widget(dac_tx_manager, dac_data_manager_dds_tone(0, TONE_2, TONE_Q), WIDGET_PHASE);

	ch0 = iio_device_find_channel(dac, "altvoltage0", true);
	if (!adc)
		goto dac_freq_attribs;
	ch1 = iio_device_find_channel(adc, "voltage0", false);
	if (iio_channel_find_attr(ch1, "sampling_frequency")) {
		adc_freq_device = adc;
		adc_freq_channel = ch1;
		adc_freq_file = "sampling_frequency";
	} else {
		adc_freq_device = iio_context_find_device(ctx, "ad9523-lpc");
		adc_freq_channel = iio_device_find_channel(adc_freq_device, "altvoltage2", true);
		adc_freq_file = "ADC_CLK_frequency";
	}

dac_freq_attribs:
	dac_sampling_freq_file = iio_channel_find_attr(ch0,
			"1A_sampling_frequency") ?: "sampling_frequency";

	/* Bind the IIO device files to the GUI widgets */

	/* The next free frequency related widgets - keep in this order! */
	num_dac_freq = num_tx;
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dac, ch0, dac_sampling_freq_file,
			builder, "dac_data_clock", &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);
	num_dac_interpolation = num_tx;
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			dac, ch0, "interpolation_frequency",
			"interpolation_frequency_available",
			builder, "dac_interpolation_clock", NULL);
	num_dac_shift = num_tx;
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			dac, ch0, "interpolation_center_shift_frequency",
			"interpolation_center_shift_frequency_available",
			builder, "dac_fcenter_shift", NULL);

	num_tx_pll = num_tx;

	if (!txpll)
		goto dac_calib_attribs;
	ch0 = iio_device_find_channel(txpll, "altvoltage0", true);

	iio_spin_button_int_init(&tx_widgets[num_tx++],
			txpll, ch0, "frequency", tx_lo_freq, &mhz_scale);
	iio_spin_button_add_progress(&tx_widgets[num_tx - 1]);

	tx_lo_powerdown = num_tx;
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			txpll, ch0, "powerdown", builder, "tx_lo_powerdown", 1);

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			txpll, ch0, "frequency_resolution", builder, "tx_lo_spacing", NULL);

dac_calib_attribs:
	/* Calibration */
	ch0 = iio_device_find_channel(dac, "voltage0", true);
	ch1 = iio_device_find_channel(dac, "voltage1", true);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			dac, ch0, "calibbias", I_dac_offs, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			dac, ch0, "calibscale", I_dac_fs_adj, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			dac, ch0, "phase", I_dac_pha_adj, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			dac, ch1, "calibbias", Q_dac_offs, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			dac, ch1, "calibscale", Q_dac_fs_adj, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			dac, ch1, "phase", Q_dac_pha_adj, NULL);

	g_signal_connect(I_dac_offs, "value-changed",
			G_CALLBACK(dac_cal_spin0), "calibbias");
	g_signal_connect(I_dac_fs_adj, "value-changed",
			G_CALLBACK(dac_cal_spin0), "calibscale");
	g_signal_connect(I_dac_pha_adj, "value-changed",
			G_CALLBACK(dac_cal_spin0), "phase");
	g_signal_connect(Q_dac_offs, "value-changed",
			G_CALLBACK(dac_cal_spin1), "calibbias");
	g_signal_connect(Q_dac_fs_adj, "value-changed",
			G_CALLBACK(dac_cal_spin1), "calibscale");
	g_signal_connect(Q_dac_pha_adj, "value-changed",
			G_CALLBACK(dac_cal_spin1), "phase");

	if (!adc)
		goto rx_attribs;
	ch0 = iio_device_find_channel(adc, "voltage0", false);
	ch1 = iio_device_find_channel(adc, "voltage1", false);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			adc, ch0, "calibbias", I_adc_offset_adj, NULL);
	iio_spin_button_s64_init(&cal_widgets[num_cal++],
			adc, ch1, "calibbias", Q_adc_offset_adj, NULL);
	iio_spin_button_init(&cal_widgets[num_cal++],
			adc, ch0, "calibscale", I_adc_gain_adj, NULL);
	iio_spin_button_init(&cal_widgets[num_cal++],
			adc, ch1, "calibscale", Q_adc_gain_adj, NULL);
	iio_spin_button_init(&cal_widgets[num_cal++],
			adc, ch0, "calibphase", I_adc_phase_adj, NULL);
	iio_spin_button_init(&cal_widgets[num_cal++],
			adc, ch1, "calibphase", Q_adc_phase_adj, NULL);

	g_signal_connect(I_adc_gain_adj  , "value-changed",
			G_CALLBACK(adc_cal_spin0), "calibscale");
	g_signal_connect(I_adc_offset_adj, "value-changed",
			G_CALLBACK(adc_cal_spin0), "calibbias");
	g_signal_connect(I_adc_phase_adj , "value-changed",
			G_CALLBACK(adc_cal_spin0), "calibphase");
	g_signal_connect(Q_adc_gain_adj  , "value-changed",
			G_CALLBACK(adc_cal_spin1), "calibscale");
	g_signal_connect(Q_adc_offset_adj, "value-changed",
			G_CALLBACK(adc_cal_spin1), "calibbias");
	g_signal_connect(Q_adc_phase_adj , "value-changed",
			G_CALLBACK(adc_cal_spin1), "calibphase");

rx_attribs:
	/* Rx Widgets */
	if (!rxpll)
		goto adc_freq_attribs;
	ch0 = iio_device_find_channel(rxpll, "altvoltage0", true);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			txpll, ch0, "frequency_resolution",
			builder, "rx_lo_spacing", NULL);

	num_rx_pll = num_rx;
	iio_spin_button_int_init(&rx_widgets[num_rx++],
			txpll, ch0, "frequency", rx_lo_freq, &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);
	rx_lo_powerdown = num_rx;
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
			txpll, ch0, "powerdown", builder, "rx_lo_powerdown", 1);
	num_adc_freq = num_rx;

adc_freq_attribs:
	if (!adc)
		goto vga_attribs;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			adc_freq_device, adc_freq_channel, adc_freq_file,
			builder, "adc_freq", &mhz_scale);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

vga_attribs:
	if (!vga)
		goto calib_dialog;
	ch0 = iio_device_find_channel(vga, "voltage0", true);
	ch1 = iio_device_find_channel(vga, "voltage1", true);

	iio_spin_button_init(&rx_widgets[num_rx++],
			vga, ch0, "hardwaregain", vga_gain0, NULL);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);
	iio_spin_button_init(&rx_widgets[num_rx++],
			vga, ch1, "hardwaregain", vga_gain1, NULL);
	iio_spin_button_add_progress(&rx_widgets[num_rx - 1]);

calib_dialog:
	g_builder_connect_signal(builder, "calibrate_dialog", "clicked",
		G_CALLBACK(cal_dialog), NULL);

	g_signal_connect(
		GTK_WIDGET(gtk_builder_get_object(builder, "gain_amp_together")),
		"toggled", G_CALLBACK(gain_amp_locked_cb), NULL);

	g_signal_connect_after(dac_data_clock, "changed", G_CALLBACK(dac_interpolation_update), NULL);
	g_signal_connect_after(dac_interpolation, "changed", G_CALLBACK(dac_shift_update), NULL);
	g_signal_connect_after(dac_shift, "changed", G_CALLBACK(rf_out_update), NULL);
	g_signal_connect_after(dds1_scale, "changed", G_CALLBACK(rf_out_update), NULL);
	g_signal_connect_after(dds2_scale, "changed", G_CALLBACK(rf_out_update), NULL);
	g_signal_connect_after(dds3_scale, "changed", G_CALLBACK(rf_out_update), NULL);
	g_signal_connect_after(dds4_scale, "changed", G_CALLBACK(rf_out_update), NULL);

	make_widget_update_signal_based(rx_widgets, num_rx);
	make_widget_update_signal_based(tx_widgets, num_tx);

	if (txpll)
		iio_spin_button_set_on_complete_function(&tx_widgets[num_tx_pll],
			rf_out_update_on_complete, NULL);
	if (rxpll)
		iio_spin_button_set_on_complete_function(&rx_widgets[num_rx_pll],
			rx_update_labels_on_complete, NULL);
	if (adc)
		iio_spin_button_set_on_complete_function(&rx_widgets[num_adc_freq],
			rx_update_labels_on_complete, NULL);
	iio_spin_button_set_on_complete_function(&tx_widgets[num_dac_freq],
			tx_sample_rate_changed, NULL);

	if (!rxpll)
		goto update_widgets;
	g_object_bind_property(GTK_TOGGLE_BUTTON(rx_widgets[rx_lo_powerdown].widget), "active", avg_I, "visible", 0);
	g_object_bind_property(GTK_TOGGLE_BUTTON(rx_widgets[rx_lo_powerdown].widget), "active", avg_Q, "visible", 0);
	g_object_bind_property(GTK_TOGGLE_BUTTON(rx_widgets[rx_lo_powerdown].widget), "active", span_I, "visible", 0);
	g_object_bind_property(GTK_TOGGLE_BUTTON(rx_widgets[rx_lo_powerdown].widget), "active", span_Q, "visible", 0);
	g_object_bind_property(GTK_TOGGLE_BUTTON(rx_widgets[rx_lo_powerdown].widget), "active", radius_IQ, "visible", 0);
	g_object_bind_property(GTK_TOGGLE_BUTTON(rx_widgets[rx_lo_powerdown].widget), "active", angle_IQ, "visible", 0);

update_widgets:
	fmcomms1_cal_eeprom();
	tx_update_values();
	rx_update_values();
	cal_update_values();
	tx_sample_rate_changed(NULL);
	dac_data_manager_update_iio_widgets(dac_tx_manager);

	dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager, OSC_WAVEFORM_FILE_PATH);

	can_update_widgets = true;

	return fmcomms1_panel;
}

static void save_widgets_to_ini(FILE *f)
{
	fprintf(f, "dds_mode = %i\n"
			"dac_buf_filename = %s\n"
			"tx_channel_0 = %i\n"
			"tx_channel_1 = %i\n"
			"gain_locked = %i\n",
			dac_data_manager_get_dds_mode(dac_tx_manager, "cf-ad9122-core-lpc", 1),
			dac_data_manager_get_buffer_chooser_filename(dac_tx_manager),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 0),
			dac_data_manager_get_tx_channel_state(dac_tx_manager, 1),
			!!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gain_locked)));
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		/* Write the section header */
		save_to_ini(f, THIS_DRIVER, dac, fmcomms1_sr_attribs,
				ARRAY_SIZE(fmcomms1_sr_attribs));
		if (adc)
			save_to_ini(f, NULL, adc, fmcomms1_sr_attribs,
					ARRAY_SIZE(fmcomms1_sr_attribs));
		if (txpll)
			save_to_ini(f, NULL, txpll, fmcomms1_sr_attribs,
					ARRAY_SIZE(fmcomms1_sr_attribs));
		if (rxpll)
			save_to_ini(f, NULL, rxpll, fmcomms1_sr_attribs,
					ARRAY_SIZE(fmcomms1_sr_attribs));
		if (vga)
			save_to_ini(f, NULL, vga, fmcomms1_sr_attribs,
					ARRAY_SIZE(fmcomms1_sr_attribs));
		save_widgets_to_ini(f);
		fclose(f);
	}
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	save_profile(NULL, ini_fn);

	if (dac_tx_manager) {
		dac_data_manager_free(dac_tx_manager);
		dac_tx_manager = NULL;
	}

	osc_destroy_context(ctx);
}

static bool fmcomms1_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();
	return !!iio_context_find_device(osc_ctx, "cf-ad9122-core-lpc");
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = fmcomms1_identify,
	.init = fmcomms1_init,
	.handle_item = fmcomms1_handle,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
