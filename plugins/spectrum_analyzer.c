/**
 * Copyright (C) 2012-2015 Analog Devices, Inc.
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
#include <time.h>

#include <ad9361.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../libini2.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "dac_data_manager.h"

#define THIS_DRIVER "Spectrum Analyzer"
#define PHY_DEVICE "ad9361-phy"
#define CAP_DEVICE "cf-ad9361-lpc"
#define FIR_FILTER "61_44_28MHz.ftr"
#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))
#define MHZ_TO_HZ(x) ((x) * 1000000)
#define MHZ_TO_KHZ(x) ((x) * 1000)
#define HZ_TO_MHZ(x) ((x) / 1E6)

#define HANNING_ENBW 1.50

enum receivers {
	RX1,
	RX2
};

typedef struct _plugin_setup {
	double start_freq;
	double stop_freq;
	double resolution_bw;
	unsigned int fft_size;
	enum receivers rx;
	GSList *rx_profiles;
	unsigned int profile_count;
	unsigned int profile_slot;
} plugin_setup;

typedef struct _fastlock_profile {
	unsigned index;
	long long frequency;
	char data[66];
} fastlock_profile;

/* Plugin Global Variables */
static const double sweep_freq_step = 56; /* 56 MHz */
static const long long sampling_rate = 61440000; /* 61.44 MSPS */

static struct iio_context *ctx;
static struct iio_device *dev, *cap;
static struct iio_channel *alt_ch0;
static struct iio_buffer *capture_buffer;
static bool is_2rx_2tx;
static char *rx_fastlock_store_name;
static char *rx_fastlock_save_name;
static GtkWidget *spectrum_window;
static plugin_setup psetup;

/* Plugin Threads */
static GThread *freq_sweep_thread;
static GThread *capture_thread;
static GThread *fft_thread;

/* Threads Synchronization */
static GCond profile_applied_cond,
		capture_done_cond,
		demux_done_cond,
		fft_done_cond;
static GMutex profile_applied_mutex,
		capture_done_mutex,
		demux_done_mutex,
		fft_done_mutex;
static bool profile_applied,
		capture_done,
		demux_done,
		fft_done;
static bool kill_sweep_thread,
		kill_capture_thread,
		kill_fft_thread;

/* Control Widgets */
static GtkWidget *center_freq;
static GtkWidget *freq_bw;
static GtkWidget *available_RBWs;
static GtkWidget *receiver1;
static GtkWidget *start_button;
static GtkWidget *stop_button;

/* Default Plugin Variables */
static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *analyzer_panel;
static gboolean plugin_detached;

#if DEBUG
GTimer *gtimer;
double loop_durations_sum;
unsigned long long loop_count;
#endif

static ssize_t demux_sample(const struct iio_channel *chn,
		void *sample, size_t size, void *d)
{
	struct extra_info *info = iio_channel_get_data(chn);
	struct extra_dev_info *dev_info = iio_device_get_data(info->dev);
	const struct iio_data_format *format = iio_channel_get_data_format(chn);

	/* Prevent buffer overflow */
	if ((unsigned long) info->offset == (unsigned long) dev_info->sample_count)
		return 0;

	if (size == 1) {
		int8_t val;
		iio_channel_convert(chn, &val, sample);
		if (format->is_signed)
			*(info->data_ref + info->offset++) = (gfloat) val;
		else
			*(info->data_ref + info->offset++) = (gfloat) (uint8_t)val;
	} else if (size == 2) {
		int16_t val;
		iio_channel_convert(chn, &val, sample);
		if (format->is_signed)
			*(info->data_ref + info->offset++) = (gfloat) val;
		else
			*(info->data_ref + info->offset++) = (gfloat) (uint16_t)val;
	} else {
		int32_t val;
		iio_channel_convert(chn, &val, sample);
		if (format->is_signed)
			*(info->data_ref + info->offset++) = (gfloat) val;
		else
			*(info->data_ref + info->offset++) = (gfloat) (uint32_t)val;
	}

	return size;
}

static void device_set_rx_sampling_freq(struct iio_device *dev, long long freq_hz)
{
	struct iio_channel *ch0;

	ch0 = iio_device_find_channel(dev, "voltage0", false);
	if (ch0)
		iio_channel_attr_write_longlong(ch0, "sampling_frequency", freq_hz);
	else
		fprintf(stderr, "Failed to retrieve iio channel in %s\n", __func__);
}

static long long device_get_rx_sampling_freq(struct iio_device *dev)
{
	long long freq_hz = 0;
	struct iio_channel *ch0;

	ch0 = iio_device_find_channel(dev, "voltage0", false);
	if (ch0)
		iio_channel_attr_read_longlong(ch0, "sampling_frequency", &freq_hz);
	else
		fprintf(stderr, "Failed to retrieve iio channel in %s\n", __func__);

	return freq_hz;
}

/* Generate available values for the Resolution Bandwidth.
 * RBW = Sampling Rate / N FFT Bins
 * In oscplot.c FFT sizes are: 32 <= N <= 65536
 */
static void comboboxtext_rbw_fill(GtkComboBoxText *box, double sampling_freq)
{
	GtkListStore *liststore;
	unsigned fft_size;
	char buf[64];

	g_return_if_fail(box);

	liststore = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(box)));
	gtk_list_store_clear(liststore);

	for (fft_size = 65536; fft_size >= 32; fft_size >>= 1) {
		snprintf(buf, sizeof(buf), "%.3f",
			MHZ_TO_KHZ(sampling_freq) / (double)fft_size);
		gtk_combo_box_text_append_text(box, buf);
	}
}

#if DEBUG
static void log_before_sweep_starts(plugin_setup *setup)
{
	FILE *fp;
	GSList *node;

	fp = fopen("spectrum_setup_log.txt", "w");
	if (!fp) {
		fprintf(stderr, "Could not open/create spectrum_setup_log.txt "
				"file for writing\n");
		return;
	}

	fprintf(fp, "Spectrum Setup Log File\n\n");
	fprintf(fp, "Profile count: %d\n", g_slist_length(setup->rx_profiles));
	for (node = setup->rx_profiles; node; node = g_slist_next(node)) {
		fastlock_profile *profile = node->data;
		fprintf(fp, "Index: %u\n", profile->index);
		fprintf(fp, "Frequency: %lld\n", profile->frequency);
		fprintf(fp, "Raw Data: %s\n", profile->data);
	}

	fclose(fp);
}
#endif

static void init_device_list(struct iio_context *ctx)
{
	unsigned int i, j, num_devices;

	num_devices = iio_context_get_devices_count(ctx);

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		unsigned int nb_channels = iio_device_get_channels_count(dev);
		struct extra_dev_info *dev_info = calloc(1, sizeof(*dev_info));
		iio_device_set_data(dev, dev_info);
		dev_info->input_device = is_input_device(dev);
		dev_info->plugin_fft_corr = 20 * log10(1/sqrt(HANNING_ENBW));

		for (j = 0; j < nb_channels; j++) {
			struct iio_channel *ch = iio_device_get_channel(dev, j);
			struct extra_info *info = calloc(1, sizeof(*info));
			info->dev = dev;
			iio_channel_set_data(ch, info);
		}
	}
}

static bool plugin_gather_user_setup(plugin_setup *setup)
{
	double center, bw, start_freq, stop_freq;
	int rbw_index;
	bool data_is_new = false;

	g_return_val_if_fail(setup, false);

	center = gtk_spin_button_get_value(GTK_SPIN_BUTTON(center_freq));
	bw = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freq_bw));
	rbw_index = gtk_combo_box_get_active(GTK_COMBO_BOX(available_RBWs));
	start_freq = center - bw / 2;
	stop_freq = center + bw / 2;
	setup->fft_size = 65536 >> rbw_index;

	if ((setup->start_freq != start_freq) || (setup->stop_freq != stop_freq)) {
		setup->start_freq = start_freq;
		setup->stop_freq = stop_freq;
		data_is_new = true;
	}

	if (!is_2rx_2tx) {
		setup->rx = RX1;
	} else {
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(receiver1)))
			setup->rx = RX1;
		else
			setup->rx = RX2;
	}
	setup->profile_slot = 1;

	return data_is_new;
}

static void build_profiles_for_entire_sweep(plugin_setup *setup)
{
	double start, stop, step, f;
	fastlock_profile *profile;
	static unsigned char prev_alc = 0;
	unsigned char alc;
	char *last_byte;
	unsigned int i = 0;

	g_return_if_fail(setup);

	/* Clear any previous profiles */
	g_slist_free_full(setup->rx_profiles, (GDestroyNotify)free);
	setup->rx_profiles = NULL;

	start = setup->start_freq + sweep_freq_step / 2;
	stop = setup->stop_freq;
	step = sweep_freq_step;

	for (f = start; (f - sweep_freq_step / 2) < stop; f += step) {
		iio_channel_attr_write_longlong(alt_ch0, "frequency",
				(long long)MHZ_TO_HZ(f));
		iio_channel_attr_write_longlong(alt_ch0,
					rx_fastlock_store_name, 0);
		profile = malloc(sizeof(fastlock_profile));
		if (!profile)
			return;
		iio_channel_attr_read(alt_ch0, rx_fastlock_save_name,
					profile->data, sizeof(profile->data));
		profile->frequency = (long long)MHZ_TO_HZ(f);
		profile->index = i++;
		setup->rx_profiles = g_slist_prepend(setup->rx_profiles, profile);

		/* Make sure two consecutive profiles do not have the same ALC.
		 * Disregard the LBS of the ALC when comparing.
		   More on: https://ez.analog.com/message/151702#151702 */
		last_byte = g_strrstr(profile->data, ",") + 1;
		alc = atoi(last_byte);
		if (abs(alc - prev_alc) < 2)
			alc += 2;
		prev_alc = alc;
		sprintf(last_byte, "%d", alc);

	}
	setup->rx_profiles = g_slist_reverse(setup->rx_profiles);
	setup->profile_count = g_slist_length(setup->rx_profiles);
	#if DEBUG
	log_before_sweep_starts(setup);
	#endif
}

static void configure_spectrum_window(plugin_setup *setup)
{
	unsigned int i;
	bool enable;

	g_return_if_fail(setup);

	osc_plot_spect_mode(OSC_PLOT(spectrum_window), true);
	osc_plot_set_domain(OSC_PLOT(spectrum_window), SPECTRUM_PLOT);
	osc_plot_set_sample_count(OSC_PLOT(spectrum_window), setup->fft_size);
	for (i = 0; i < iio_device_get_channels_count(cap); i++) {
		enable = i / 2 == setup->rx;
		osc_plot_set_channel_state(OSC_PLOT(spectrum_window), CAP_DEVICE,
			i, enable);
	}
	osc_plot_spect_set_len(OSC_PLOT(spectrum_window), setup->profile_count);
	osc_plot_spect_set_start_f(OSC_PLOT(spectrum_window), setup->start_freq);
	osc_plot_spect_set_filter_bw(OSC_PLOT(spectrum_window), sweep_freq_step);
	osc_plot_set_visible(OSC_PLOT(spectrum_window), true);
}

static void spectrum_window_destroyed_cb(OscPlot *plot);
static void build_spectrum_window(plugin_setup *setup)
{
	g_return_if_fail(setup);

	spectrum_window = osc_plot_new(ctx);
	configure_spectrum_window(setup);
	g_signal_connect(spectrum_window, "osc-destroy-event",
			G_CALLBACK(spectrum_window_destroyed_cb), NULL);
}

static bool configure_data_capture(plugin_setup *setup)
{
	struct iio_channel *chn;
	struct extra_info *info;
	struct extra_dev_info *dev_info;
	unsigned int i;
	long long rate;

	g_return_val_if_fail(setup, false);

	device_set_rx_sampling_freq(dev, sampling_rate);
	rate = device_get_rx_sampling_freq(cap);
	if (rate != sampling_rate) {
		fprintf(stderr, "Failed to set the rx sampling rate to %lld"
			"in %s\n", sampling_rate, __func__);
		return false;
	}

	dev_info = iio_device_get_data(cap);
	dev_info->sample_count = setup->fft_size;
	dev_info->adc_freq = rate;
	if (dev_info->adc_freq >= 1000000) {
		dev_info->adc_scale = 'M';
		dev_info->adc_freq /= 1000000.0;
	} else if (dev_info->adc_freq >= 1000) {
		dev_info->adc_scale = 'k';
		dev_info->adc_freq /= 1000.0;
	} else if (dev_info->adc_freq >= 0) {
		dev_info->adc_scale = ' ';
	} else {
		dev_info->adc_scale = '?';
		dev_info->adc_freq = 0.0;
	}

	for (i = 0; i < iio_device_get_channels_count(cap); i++) {
		chn = iio_device_get_channel(cap, i);
		info = iio_channel_get_data(chn);
		if (info->data_ref) {
			g_free(info->data_ref);
			info->data_ref = NULL;
		}
		if (i / 2 == setup->rx) {
			iio_channel_enable(chn);
			info->data_ref = (gfloat *) g_new0(gfloat, setup->fft_size);
		} else {
			iio_channel_disable(chn);
		}
	}

	return true;
}

static gpointer capture_data_thread_func(plugin_setup *setup)
{
	while (!kill_capture_thread) {

		/* Clean iio buffer */
		if (capture_buffer) {
			iio_buffer_destroy(capture_buffer);
			capture_buffer = NULL;
		}

		/* Capture new data */
		capture_buffer = iio_device_create_buffer(cap, setup->fft_size, false);
		if (!capture_buffer) {
			fprintf(stderr, "Could not create iio buffer in %s\n", __func__);
			break;
		}

		/* Reset the data offset for all channels */
		unsigned int i;
		for (i = 0; i < iio_device_get_channels_count(cap); i++) {
			struct iio_channel *ch = iio_device_get_channel(cap, i);
			struct extra_info *info = iio_channel_get_data(ch);
			info->offset = 0;
		}

		/* Get captured data */
		ssize_t ret = iio_buffer_refill(capture_buffer);
		if (ret < 0) {
			fprintf(stderr, "Error while refilling iio buffer: %s\n", strerror(-ret));
			break;
		}

		/* Signal the "Frequency Sweep" thread that data capture has completed */
		g_mutex_lock(&capture_done_mutex);
		capture_done = true;
		g_cond_signal(&capture_done_cond);
		g_mutex_unlock(&capture_done_mutex);

		/* Block until the "Do FFT" thread has finished doing a FFT*/
		g_mutex_lock(&fft_done_mutex);
		while (!fft_done)
			g_cond_wait(&fft_done_cond, &fft_done_mutex);
		fft_done = false;
		g_mutex_unlock(&fft_done_mutex);
		if (kill_capture_thread)
			break;

		/* Demux captured data */
		ret /= iio_buffer_step(capture_buffer);
		if ((unsigned)ret >= setup->fft_size)
			iio_buffer_foreach_sample(capture_buffer, demux_sample, NULL);

		/* Signal the "Do FFT" thread that data demux has completed */
		g_mutex_lock(&demux_done_mutex);
		demux_done = true;
		g_cond_signal(&demux_done_cond);
		g_mutex_unlock(&demux_done_mutex);

		/* Block until the "Data Capture" thread has recalled a new profile */
		g_mutex_lock(&profile_applied_mutex);
		while (!profile_applied)
			g_cond_wait(&profile_applied_cond, &profile_applied_mutex);
		profile_applied = false;
		g_mutex_unlock(&profile_applied_mutex);
		if (kill_capture_thread)
			break;
	}

	/* Wake-up the "Frequency Sweep" thread and kill it */
	kill_sweep_thread = true;
	g_mutex_lock(&capture_done_mutex);
	capture_done = true;
	g_cond_signal(&capture_done_cond);
	g_mutex_unlock(&capture_done_mutex);

	g_thread_join(freq_sweep_thread);

	return NULL;
}

static gpointer profile_load_thread_func(plugin_setup *setup)
{
	GSList *node = g_slist_nth(setup->rx_profiles, 1);
	fastlock_profile *profile;
	ssize_t ret;

	while (!kill_sweep_thread) {

		/* Block until the "Capture" thread has finished a data capture */
		g_mutex_lock(&capture_done_mutex);
		while (!capture_done)
			g_cond_wait(&capture_done_cond, &capture_done_mutex);
		capture_done = false;
		g_mutex_unlock(&capture_done_mutex);
		if (kill_sweep_thread)
			break;

		/* Recall profile at slot 0 or 1 (alternative) */
		ret = iio_channel_attr_write_longlong(alt_ch0,
			"fastlock_recall", setup->profile_slot);
		setup->profile_slot = (setup->profile_slot + 1) % 2;
		if (setup->profile_count == 1)
			setup->profile_slot = 0;
		if (ret < 0)
		fprintf(stderr, "Could not write to fastlock_recall"
			"attribute in %s\n", __func__);

		/* Signal the "Data Capture" thread that a new profile has been applied */
		g_mutex_lock(&profile_applied_mutex);
		profile_applied = true;
		g_cond_signal(&profile_applied_cond);
		g_mutex_unlock(&profile_applied_mutex);

		/* Move to the next fastlock profile */
		node = g_slist_next(node);
		if (!node) {
			node = setup->rx_profiles;
			#if DEBUG
			g_timer_stop(gtimer);
			loop_durations_sum += g_timer_elapsed(gtimer, NULL);
			loop_count++;
			g_timer_start(gtimer);
			#endif
		}
		profile = node->data;
		profile->data[0] = '0' + setup->profile_slot;
		ret = iio_channel_attr_write(alt_ch0, "fastlock_load",
				profile->data);
		if (ret < 0)
			fprintf(stderr, "Could not write to fastlock_load"
				"attribute in %s\n", __func__);
	}

	/* Wake-up and kill "Do FFT" thread */
	kill_fft_thread = true;
	g_mutex_lock(&demux_done_mutex);
	demux_done = true;
	g_cond_signal(&demux_done_cond);
	g_mutex_unlock(&demux_done_mutex);

	g_thread_join(fft_thread);

	return NULL;
}

static gpointer do_fft_thread_func(plugin_setup *setup)
{
	while (!kill_fft_thread) {

		/* Block until the "Data Capture" thread finishes to demux data */
		g_mutex_lock(&demux_done_mutex);
		while (!demux_done)
			g_cond_wait(&demux_done_cond, &demux_done_mutex);
		demux_done = false;
		g_mutex_unlock(&demux_done_mutex);
		if (kill_fft_thread)
			break;

		/* Tell the oscplot object to process the captured data, perform FFT
		 * and concatenate with the rest of the FFTs in order to build the spectrum */
		if (spectrum_window)
			osc_plot_data_update(OSC_PLOT(spectrum_window));

		/* Signal the "Data Capture" thread that the FFT has finished */
		g_mutex_lock(&fft_done_mutex);
		fft_done = true;
		g_cond_signal(&fft_done_cond);
		g_mutex_unlock(&fft_done_mutex);
	}

	return NULL;
}

static bool setup_before_sweep_start(plugin_setup *setup)
{
	GSList *node;
	fastlock_profile *profile;
	ssize_t ret;
	int i;

	g_return_val_if_fail(setup, false);

	/* Configure the FIR filter */
	FILE *fp;
	char *buf;
	ssize_t len;

	fp = fopen("filters/"FIR_FILTER, "r");
	if (!fp)
		fp = fopen(OSC_FILTER_FILE_PATH"/"FIR_FILTER, "r");
	if (!fp) {
		fprintf(stderr, "Could not open file %s for reading in %s. %s\n",
			FIR_FILTER, __func__, strerror(errno));
		goto fail;
	}

	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	buf = malloc(len);
	fseek(fp, 0, SEEK_SET);
	len = fread(buf, 1, len, fp);
	fclose(fp);

	ret = iio_device_attr_write_raw(dev,
			"filter_fir_config", buf, len);
	if (ret < 0) {
		fprintf(stderr, "FIR filter config failed in %s. %s\n",
			__func__, strerror(ret));
		goto fail;
	}
	free(buf);

	ret = ad9361_set_trx_fir_enable(dev, true);
	if (ret < 0) {
		fprintf(stderr, "a write to in_out_voltage_filter_fir_en failed"
			"in %s. %s\n", __func__, strerror(ret));
		goto fail;
	}
	/* Fill fastlock slots 0 and 1 */
	for (i = 0, node = setup->rx_profiles; i < 2 && node;
					i++, node = g_slist_next(node)) {
		profile = node->data;
		profile->data[0] = '0' + i;

		ret = iio_channel_attr_write(alt_ch0, "fastlock_load",
				profile->data);
		if (ret < 0) {
			fprintf(stderr, "Could not write to fastlock_load"
				"attribute in %s. %s\n", __func__, strerror(ret));
			goto fail;
		}
	}

	/* Recall profile at slot 0 */
	ret = iio_channel_attr_write_longlong(alt_ch0,
			"fastlock_recall", 0);
	if (ret < 0) {
		fprintf(stderr, "Could not write to fastlock_recall"
			"attribute in %s. %s\n", __func__, strerror(ret));
		goto fail;
	}

	kill_capture_thread = false;
	kill_sweep_thread = false;
	kill_fft_thread = false;

	capture_done = false;
	profile_applied = false;
	demux_done = false;
	fft_done = true;

	return true;

fail:
	return false;
}

static void start_sweep_clicked(GtkButton *btn, gpointer data)
{
	gtk_widget_set_sensitive(GTK_WIDGET(btn), false);

#if DEBUG
	gtimer = g_timer_new();
	loop_durations_sum = 0;
	loop_count = 0;
#endif

	/* This capture process and the capture process from osc.c are designed
	 * to access the same iio devices but they do it from different threads,
	 * thus should not run simultaneously. */
	plugin_osc_stop_all_plots();

	if (plugin_gather_user_setup(&psetup))
		build_profiles_for_entire_sweep(&psetup);
	if (!configure_data_capture(&psetup))
		goto abort;
	if (!spectrum_window)
		build_spectrum_window(&psetup);
	else
		configure_spectrum_window(&psetup);
	osc_plot_draw_start(OSC_PLOT(spectrum_window));

	if (!setup_before_sweep_start(&psetup))
		goto abort;

	capture_thread = g_thread_new("Data Capture",
				(GThreadFunc)capture_data_thread_func, &psetup);
	freq_sweep_thread = g_thread_new("Frequency Sweep",
				(GThreadFunc)profile_load_thread_func, &psetup);
	fft_thread = g_thread_new("Do FFT",
				(GThreadFunc)do_fft_thread_func, &psetup);

	gtk_widget_set_sensitive(GTK_WIDGET(stop_button), true);

	return;

abort:
	g_signal_emit_by_name(stop_button, "clicked", NULL);
	return;
}

static void stop_sweep_clicked(GtkButton *btn, gpointer data)
{
	gtk_widget_set_sensitive(GTK_WIDGET(btn), false);

	if (spectrum_window)
		osc_plot_draw_stop(OSC_PLOT(spectrum_window));
	if (capture_thread) {
		kill_capture_thread = true;
		g_thread_join(capture_thread);
		capture_thread = NULL;
	}
	if (capture_buffer) {
		iio_buffer_destroy(capture_buffer);
		capture_buffer = NULL;
	}

	gtk_widget_set_sensitive(GTK_WIDGET(start_button), true);
#if DEBUG
fprintf(stderr, "Average Sweep Duration: %f\n", loop_durations_sum / loop_count);
#endif
}

static void center_freq_changed(GtkSpinButton *btn, gpointer data)
{
	GtkSpinButton *bw_spin = GTK_SPIN_BUTTON(freq_bw);
	GtkAdjustment *bw_adj = gtk_spin_button_get_adjustment(bw_spin);
	double center = gtk_spin_button_get_value(btn);
	double bw = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freq_bw));
	double upper, upper1, upper2 = 0;

	upper1 = (center - 70) * 2;
	upper2 = (6000 - center) * 2;
	upper = (upper1 < upper2) ? upper1 : upper2;
	gtk_adjustment_set_upper(bw_adj, upper);
	if (bw > upper)
		gtk_spin_button_set_value(bw_spin, upper);
}

static void spectrum_window_destroyed_cb(OscPlot *plot)
{
	stop_sweep_clicked(GTK_BUTTON(stop_button), NULL);
	spectrum_window = NULL;
}

static int handle_external_request (struct osc_plugin *plugin, const char *request)
{
	int ret = 0;

	if (!strcmp(request, "Stop")) {
		gtk_button_clicked(GTK_BUTTON(stop_button));
		ret = 1;
	}

	return ret;
}

static GtkWidget * analyzer_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	struct iio_channel *ch1;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	dev = iio_context_find_device(ctx, PHY_DEVICE);
	if (!dev)
		return NULL;
	cap = iio_context_find_device(ctx, CAP_DEVICE);
	if (!cap)
		return NULL;
	alt_ch0 = iio_device_find_channel(dev, "altvoltage0", true);
	if (!alt_ch0)
		return NULL;

	ch1 = iio_device_find_channel(dev, "voltage1", false);
	is_2rx_2tx = ch1 && iio_channel_find_attr(ch1, "hardwaregain");

	init_device_list(ctx);

	if (iio_channel_find_attr(alt_ch0, "fastlock_store"))
		rx_fastlock_store_name = "fastlock_store";
	else
		rx_fastlock_store_name = "RX_LO_fastlock_store";
	if (iio_channel_find_attr(alt_ch0, "fastlock_save"))
		rx_fastlock_save_name = "fastlock_save";
	else
		rx_fastlock_save_name = "RX_LO_fastlock_save";

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (osc_load_glade_file(builder, "spectrum_analyzer") < 0)
		return NULL;

	analyzer_panel = GTK_WIDGET(gtk_builder_get_object(builder,
				"spectrum_analyzer_panel"));
	center_freq = GTK_WIDGET(gtk_builder_get_object(builder,
				"spin_center_freq"));
	freq_bw = GTK_WIDGET(gtk_builder_get_object(builder,
				"spin_freq_bw"));
	available_RBWs = GTK_WIDGET(gtk_builder_get_object(builder,
				"cmb_available_rbw"));
	receiver1 = GTK_WIDGET(gtk_builder_get_object(builder,
				"radiobutton_rx1"));
	start_button = GTK_WIDGET(gtk_builder_get_object(builder,
				"start_sweep_btn"));
	stop_button = GTK_WIDGET(gtk_builder_get_object(builder,
				"stop_sweep_btn"));

	/* Widgets initialization */
	gtk_spin_button_set_range(GTK_SPIN_BUTTON(center_freq),
		70 + sweep_freq_step / 2, 6000 - sweep_freq_step / 2);
	gtk_adjustment_set_lower(gtk_spin_button_get_adjustment(
		GTK_SPIN_BUTTON(freq_bw)), sweep_freq_step);
	comboboxtext_rbw_fill(GTK_COMBO_BOX_TEXT(available_RBWs),
				HZ_TO_MHZ(sampling_rate));
	gtk_combo_box_set_active(GTK_COMBO_BOX(available_RBWs), 6);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(receiver1), true);
	if (!is_2rx_2tx)
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder,
				"frame_receiver_selection")));

	gtk_widget_set_sensitive(GTK_WIDGET(stop_button), false);

	/* Connect signals */
	g_builder_connect_signal(builder, "start_sweep_btn", "clicked",
			G_CALLBACK(start_sweep_clicked), NULL);
	g_builder_connect_signal(builder, "stop_sweep_btn", "clicked",
			G_CALLBACK(stop_sweep_clicked), NULL);
	g_builder_connect_signal(builder, "spin_center_freq", "value-changed",
			G_CALLBACK(center_freq_changed), NULL);
	g_signal_connect_swapped(freq_bw, "value-changed",
			G_CALLBACK(center_freq_changed), center_freq);

	return analyzer_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void analyzer_get_preferred_size(const struct osc_plugin *plugin, int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	if (capture_buffer) {
		iio_buffer_destroy(capture_buffer);
		capture_buffer = NULL;
	}
	g_source_remove_by_user_data(ctx);

	osc_destroy_context(ctx);
}

struct osc_plugin plugin;

static bool analyzer_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	return !!iio_context_find_device(osc_ctx, PHY_DEVICE);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = analyzer_identify,
	.init = analyzer_init,
	.handle_external_request = handle_external_request,
	.update_active_page = update_active_page,
	.get_preferred_size = analyzer_get_preferred_size,
	.destroy = context_destroy,
};
