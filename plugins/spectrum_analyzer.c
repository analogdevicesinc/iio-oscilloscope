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
#include <malloc.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

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
} plugin_setup;

typedef struct _fastlock_profile {
	unsigned index;
	long long frequency;
	char data[66];
} fastlock_profile;

/* Plugin Global Variables */
static struct iio_context *ctx;
static struct iio_device *dev, *cap;
static struct iio_channel *alt_ch0;
static struct iio_buffer *capture_buffer;
static bool is_2rx_2tx;
static char *rx_fastlock_store_name;
static char *rx_fastlock_save_name;
static double sweep_freq_step = 18; /* 18 MHz */
static GtkWidget *spectrum_window;
static plugin_setup psetup;

/* Plugin Threads */
static GThread *freq_sweep_thread;
static GThread *capture_thread;

/* Threads Synchronization */
static GCond profile_applied_cond;
static GCond capture_done_cond;
static GMutex profile_applied_mutex;
static GMutex capture_done_mutex;
static bool profile_applied;
static bool capture_done;
static bool kill_sweep_thread;
static bool kill_capture_thread;

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

#define DEBUG 1

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

static double device_get_rx_sampling_freq(struct iio_device *dev)
{
	double freq = 0.0;
	struct iio_channel *ch0;

	ch0 = iio_device_find_channel(dev, "voltage0", false);
	if (ch0)
		iio_channel_attr_read_double(ch0, "sampling_frequency", &freq);
	else
		fprintf(stderr, "Failed to retrieve iio channel in %s\n", __func__);

	return freq;
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
		fprintf(fp, "Index: %d\n", profile->index);
		fprintf(fp, "Frequency: %lld\n", profile->frequency);
		fprintf(fp, "Raw Data: %s\n", profile->data);
	}

	fclose(fp);
}

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

static void plugin_gather_user_setup(plugin_setup *setup)
{
	double center, bw;

	g_return_if_fail(setup);

	center = gtk_spin_button_get_value(GTK_SPIN_BUTTON(center_freq));
	bw = gtk_spin_button_get_value(GTK_SPIN_BUTTON(freq_bw));
	setup->start_freq = center - bw / 2;
	setup->stop_freq = center + bw / 2;
	setup->fft_size = 65536 >> gtk_combo_box_get_active(GTK_COMBO_BOX(available_RBWs));
	if (!is_2rx_2tx) {
		setup->rx = RX1;
	} else {
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(receiver1)))
			setup->rx = RX1;
		else
			setup->rx = RX2;
	}
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

	for (f = start; f <= stop; f += step) {
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
	log_before_sweep_starts(setup);
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

static void configure_data_capture(plugin_setup *setup)
{
	struct iio_channel *chn;
	struct extra_info *info;
	struct extra_dev_info *dev_info;
	unsigned int i;

	g_return_if_fail(setup);

	dev_info = iio_device_get_data(cap);
	dev_info->sample_count = setup->fft_size;
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
}

static gpointer capture_and_display(plugin_setup *setup)
{
	g_return_val_if_fail(setup, NULL);

	while (!kill_capture_thread) {
		/* Feed the previously captured data to the plot window */
		if (capture_buffer) {
			iio_buffer_destroy(capture_buffer);
			capture_buffer = NULL;
			if (spectrum_window)
				osc_plot_data_update(OSC_PLOT(spectrum_window));
		}

		/* Capture new data */
		capture_buffer = iio_device_create_buffer(cap, setup->fft_size, false);
		if (!capture_buffer) {
			fprintf(stderr, "Could not create iio buffer in %s\n", __func__);
			goto kill_sweep_thd;
		}

		/* Reset the data offset for all channels */
		unsigned int i;
		for (i = 0; i < iio_device_get_channels_count(cap); i++) {
			struct iio_channel *ch = iio_device_get_channel(cap, i);
			struct extra_info *info = iio_channel_get_data(ch);
			info->offset = 0;
		}

		/* Block until the "RX LO Sweep" thread finishes to apply a new profile */
		g_mutex_lock(&profile_applied_mutex);
		while (!profile_applied)
			g_cond_wait(&profile_applied_cond, &profile_applied_mutex);
		profile_applied = false;
		g_mutex_unlock(&profile_applied_mutex);

		/* Get captured data */
		ssize_t ret = iio_buffer_refill(capture_buffer);
		if (ret < 0) {
			fprintf(stderr, "Error while refilling iio buffer: %s\n", strerror(-ret));
			goto kill_sweep_thd;
		}

		/* Signal the "RX LO Sweep" thread that a data capture has been performed */
		g_mutex_lock(&capture_done_mutex);
		capture_done = true;
		g_cond_signal(&capture_done_cond);
		g_mutex_unlock(&capture_done_mutex);

		/* Demux captured data */
		ret /= iio_buffer_step(capture_buffer);
		if ((unsigned)ret >= setup->fft_size)
			iio_buffer_foreach_sample(capture_buffer, demux_sample, NULL);
	}

	return NULL;

kill_sweep_thd:
	kill_sweep_thread = true;
	/* In order to kill the "RX LO Sweep" thread, make sure it is not blocked */
	g_mutex_lock(&capture_done_mutex);
	capture_done = true;
	g_cond_signal(&capture_done_cond);
	g_mutex_unlock(&capture_done_mutex);

	return NULL;
}

static gpointer rx_lo_frequency_sweep(plugin_setup *setup)
{
	GSList *node;
	fastlock_profile *profile;
	ssize_t ret;

	g_return_val_if_fail(setup, NULL);

	node = setup->rx_profiles;

	/* Here I intend to change the fastlock using only the fastlock_load
	 * call provided that I use one and the same fastlock profile slot and
	 * after the fastlock_recall gets called at least once. Also this works
	 * only if profiles do not have the same ALS - which is taken care of
	 * somewhere in this code. */
	ret = iio_channel_attr_write_longlong(alt_ch0,
			"fastlock_recall", 0);
	if (ret < 0)
		fprintf(stderr, "Could not write to fastlock_recall"
			"attribute in %s\n", __func__);

	profile_applied = false;
	capture_done = true;
	kill_capture_thread = false;
	capture_thread = g_thread_new("Capture",
				(GThreadFunc)capture_and_display, setup);
#if DEBUG
	gtimer = g_timer_new();
#endif
	/* Start the Sweep */
	while (!kill_sweep_thread) {
		profile = (fastlock_profile *)node->data;

		/* Block until the "Capture" thread has finished a data capture */
		g_mutex_lock(&capture_done_mutex);
		while (!capture_done)
			g_cond_wait(&capture_done_cond, &capture_done_mutex);
		capture_done = false;
		g_mutex_unlock(&capture_done_mutex);

		/* Apply a new profile */
		ret = iio_channel_attr_write(alt_ch0, "fastlock_load",
				profile->data);
		if (ret < 0)
			fprintf(stderr, "Could not write to fastlock_load"
				"attribute in %s\n", __func__);

		/* Signal the "Capture" thread that a new profile has been applied */
		g_mutex_lock(&profile_applied_mutex);
		profile_applied = true;
		g_cond_signal(&profile_applied_cond);
		g_mutex_unlock(&profile_applied_mutex);

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
	}
	kill_capture_thread = true;
	/* In order to kill the "Capture" thread, make sure it is not blocked */
	g_mutex_lock(&profile_applied_mutex);
	profile_applied = true;
	g_cond_signal(&profile_applied_cond);
	g_mutex_unlock(&profile_applied_mutex);

	g_thread_join(capture_thread);

	return NULL;
}

static void start_sweep_clicked(GtkButton *btn, gpointer data)
{
	gtk_widget_set_sensitive(GTK_WIDGET(btn), false);

#if DEBUG
	loop_durations_sum = 0;
	loop_count = 0;
#endif
	plugin_gather_user_setup(&psetup);
	build_profiles_for_entire_sweep(&psetup);
	configure_data_capture(&psetup);
	if (!spectrum_window)
		build_spectrum_window(&psetup);
	else
		configure_spectrum_window(&psetup);
	osc_plot_draw_start(OSC_PLOT(spectrum_window));

	kill_sweep_thread = false;
	freq_sweep_thread = g_thread_new("RX LO Sweep",
				(GThreadFunc)rx_lo_frequency_sweep, &psetup);

	gtk_widget_set_sensitive(GTK_WIDGET(stop_button), true);
}

static void stop_sweep_clicked(GtkButton *btn, gpointer data)
{
	gtk_widget_set_sensitive(GTK_WIDGET(btn), false);

	if (spectrum_window)
		osc_plot_draw_stop(OSC_PLOT(spectrum_window));
	if (freq_sweep_thread) {
		kill_sweep_thread = true;
		g_thread_join(freq_sweep_thread);
		freq_sweep_thread = NULL;
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

static GtkWidget * analyzer_init(GtkWidget *notebook, const char *ini_fn)
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

	if (!gtk_builder_add_from_file(builder, "spectrum_analyzer.glade", NULL))
		gtk_builder_add_from_file(builder,
			OSC_GLADE_FILE_PATH "spectrum_analyzer.glade", NULL);

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
	comboboxtext_rbw_fill(GTK_COMBO_BOX_TEXT(available_RBWs),
				HZ_TO_MHZ(device_get_rx_sampling_freq(cap)));
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

static int handle_external_request(const char *request)
{
	int ret = 0;

	if (!strcmp(request, "AD9361 Sampling Rate Changed")) {
		if (available_RBWs) {
			comboboxtext_rbw_fill(GTK_COMBO_BOX_TEXT(available_RBWs),
				HZ_TO_MHZ(device_get_rx_sampling_freq(cap)));
			gtk_combo_box_set_active(GTK_COMBO_BOX(available_RBWs), 6);
			ret = 1;
		}
	}

	return ret;
}

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void analyzer_get_preferred_size(int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void context_destroy(const char *ini_fn)
{
	if (capture_buffer) {
		iio_buffer_destroy(capture_buffer);
		capture_buffer = NULL;
	}
	g_source_remove_by_user_data(ctx);

	iio_context_destroy(ctx);
}

struct osc_plugin plugin;

static bool analyzer_identify(void)
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
