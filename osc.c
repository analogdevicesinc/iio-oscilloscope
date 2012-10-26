/**
 * Copyright (C) 2012 Analog Devices, Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
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

#include "iio_widget.h"
#include "iio_utils.h"
#include "int_fft.h"
#include "config.h"

static gfloat *X = NULL;
static gfloat *channel0 = NULL;
static gfloat *channel1 = NULL;

static gint capture_function = 0;

static int num_samples;
static int16_t *data;

static GtkWidget *databox;
static GtkWidget *sample_count_widget;
static GtkWidget *fft_size_widget;
static GtkWidget *channel0_widget;
static GtkWidget *channel1_widget;
static GtkWidget *fft_radio, *time_radio, *constalation_radio;
static GtkWidget *show_grid;
static GtkWidget *enable_auto_scale;

static GtkWidget *rx_lo_freq_label, *adc_freq_label;

static GtkDataboxGraph *channel0_graph;
static GtkDataboxGraph *channel1_graph;
static GtkDataboxGraph *grid;

static double adc_freq;
static bool is_fft_mode;

static GdkColor color_graph0 = {
	.red = 0,
	.green = 60000,
	.blue = 0,
};

static GdkColor color_graph1 = {
	.red = 60000,
	.green = 0,
	.blue = 0,
};

static GdkColor color_grid = {
	.red = 51000,
	.green = 51000,
	.blue = 0,
};

static GdkColor color_background = {
	.red = 0,
	.green = 0,
	.blue = 0,
};

#if DEBUG

static int sample_iio_data(int16_t *data, unsigned num)
{
	int buf_len = num * sizeof(*data);
	int i;

	for (i = 0; i < buf_len / 2; i++) {
		data[i*2] = 4096.0f * cos(i * G_PI / 100) + (rand() % 500 - 250);
		data[i*2+1] = 4096.0f * sin(i * G_PI / 100);// + (rand() % 1000 - 500);
	}

	return 0;
}

#else

static int sample_iio_data(int16_t *data, unsigned num)
{
	int buf_len = num * sizeof(*data) * 2;
	int ret, ret2;
	int fp;

	set_dev_paths("cf-ad9643-core-lpc");

	/* Setup ring buffer parameters */
	ret = write_devattr_int("buffer/length", buf_len);
	if (ret < 0) {
		fprintf(stderr, "Failed to enable buffer: %d\n", ret);
		goto error_ret;
	}

	/* Enable the buffer */
	ret = write_devattr_int("buffer/enable", 1);
	if (ret < 0) {
		fprintf(stderr, "write_sysfs_int failed (%d)\n",__LINE__);
		goto error_ret;
	}

	/* Attempt to open the event access dev */
	fp = iio_buffer_open();
	if (fp < 0) {
		fprintf(stderr, "Failed to open buffer\n");
		ret = -errno;
		goto error_disable;
	}

	ret = read(fp, data, buf_len);
	if (ret == -EAGAIN)
		fprintf(stderr, "No data available\n");

	ret = 0;

	close(fp);

error_disable:
	/* Stop the ring buffer */
	ret2 = write_devattr_int("buffer/enable", 0);
	if (ret2 < 0) {
		fprintf(stderr, "write_sysfs_int failed (%d)\n",__LINE__);
	}

error_ret:
	return ret;
}

#endif

static int frame_counter;

static void fps_counter(void)
{
	static time_t last_update;
	time_t t;

	frame_counter++;
	t = time(NULL);
	if (t - last_update >= 10) {
		printf("FPS: %d\n", frame_counter / 10);
		frame_counter = 0;
		last_update = t;
	}
}

static void rescale_databox(GtkDatabox *box, gfloat border)
{
	bool fixed_aspect = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (constalation_radio));

	if (fixed_aspect) {
		gfloat min_x;
		gfloat max_x;
		gfloat min_y;
		gfloat max_y;
        gfloat width;

		gint extrema_success = gtk_databox_calculate_extrema(box,
				&min_x, &max_x, &min_y, &max_y);
		if (extrema_success)
			return;
		if (min_x > min_y)
			min_x = min_y;
		if (max_x < max_y)
			max_x = max_y;

        width = max_x - min_x;
        if (width == 0)
			width = max_x;

        min_x -= border * width;
        max_x += border * width;

		gtk_databox_set_total_limits(box, min_x, max_x, max_x,
			min_x);

	} else {
		gtk_databox_auto_rescale(box, border);
	}
}

static void auto_scale_databox(GtkDatabox *box)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enable_auto_scale)))
		return;

	/* Auto scale every 10 seconds */
	if (frame_counter == 0)
		rescale_databox(box, 0.05);
}

static gboolean time_capture_func(GtkDatabox *box)
{
	int trigger = 500;
	gint i, j;
	int ret;

	if (!GTK_IS_DATABOX(box))
		return FALSE;

	ret = sample_iio_data(data, num_samples * 2);
	if (ret < 0) {
		fprintf(stderr, "Failed to capture samples: %d\n", ret);
		return FALSE;
	}

	for (j = 1; j < num_samples; j++) {
		if (data[j * 2 - 2] < trigger && data[j * 2] >= trigger)
			break;
	}

	for (i = 0; i < num_samples; i++, j++) {
		channel0[i] = data[j * 2];
		channel1[i] = data[j * 2 + 1];
	}

	auto_scale_databox(box);

	gtk_widget_queue_draw(GTK_WIDGET(box));
	usleep(50000);

	fps_counter();

	return TRUE;
}

static void add_grid(void)
{
	grid = gtk_databox_grid_new(15, 15, &color_grid, 2);
	gtk_databox_graph_add(GTK_DATABOX(databox), grid);
	gtk_databox_graph_set_hide(grid, !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(show_grid)));
}

static gboolean fft_capture_func(GtkDatabox *box)
{
	short *fft_buf;
	short *real, *imag, *amp;
	unsigned int fft_size = num_samples;
	unsigned int i, cnt;
	int ret;

	ret = sample_iio_data(data, fft_size);
	if (ret < 0) {
		fprintf(stderr, "Failed to capture samples: %d\n", ret);
		return FALSE;
	}

	fft_buf = malloc((fft_size * 2 + fft_size / 2) * sizeof(short));
	if (fft_buf == NULL){
		fprintf(stderr, "malloc failed (%d)\n", __LINE__);
		return FALSE;
	}

	real = fft_buf;
	imag = real + fft_size;
	amp = imag+ fft_size;

	cnt = 0;
	for (i = 0; i < fft_size * 2; i += 2) {
		real[cnt] = data[i];
		imag[cnt] = 0;
		cnt++;
	}

	window(real, fft_size);

	fix_fft(real, imag, (int)log2f(fft_size), 0);
	fix_loud(amp, real, imag, fft_size / 2, 2); /* scale 14->16 bit */

	for (i = 0; i < fft_size / 2; ++i) {
		channel0[i] = amp[i];
	}

	free(fft_buf);

	auto_scale_databox(box);

	gtk_widget_queue_draw(GTK_WIDGET(box));
	usleep(50000);

	fps_counter();

	return TRUE;
}

static void start_capture_fft(void)
{
	int i;

	gtk_databox_graph_remove_all(GTK_DATABOX(databox));

	num_samples = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(fft_size_widget)));

	data = g_renew(int16_t, data, num_samples * 2);
	X = g_renew(gfloat, X, num_samples / 2);
	channel0 = g_renew(gfloat, channel0, num_samples / 2);

	for (i = 0; i < num_samples / 2; i++)
	{
		X[i] = i * adc_freq / num_samples;
		channel0[i] = 0.0f;
	}
	is_fft_mode = true;

	channel0_graph = gtk_databox_lines_new(num_samples / 2, X, channel0, &color_graph0, 2);
	gtk_databox_graph_add(GTK_DATABOX(databox), channel0_graph);

	add_grid();
	gtk_databox_set_total_limits(GTK_DATABOX(databox), -5.0, adc_freq / 2.0 + 5.0, 0.0, -75.0);

	gtk_widget_queue_draw(GTK_WIDGET(databox));
	frame_counter = 0;

	capture_function = g_idle_add((GSourceFunc) fft_capture_func, databox);
}

static void start_capture_time(void)
{
	gboolean is_constalation;
	int i;

	is_constalation = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (constalation_radio));

	gtk_databox_graph_remove_all(GTK_DATABOX(databox));

	num_samples = gtk_spin_button_get_value(GTK_SPIN_BUTTON(sample_count_widget));

	data = g_renew(int16_t, data, num_samples * 4);
	X = g_renew(gfloat, X, num_samples);
	channel0 = g_renew(gfloat, channel0, num_samples);
	channel1 = g_renew(gfloat, channel1, num_samples);

	for (i = 0; i < num_samples; i++) {
		X[i] = i;
		channel0[i] = 8192.0f * cos(i * 4 * G_PI / num_samples);
		channel1[i] = 8192.0f * sin(i * 4 * G_PI / num_samples);
	}
	is_fft_mode = false;

	if (is_constalation) {
			channel0_graph = gtk_databox_lines_new(num_samples, channel0, channel1, &color_graph0, 2);
			gtk_databox_graph_add(GTK_DATABOX (databox), channel0_graph);
	} else {
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(channel0_widget))) {
			channel0_graph = gtk_databox_lines_new(num_samples, X, channel0, &color_graph0, 2);
			gtk_databox_graph_add(GTK_DATABOX (databox), channel0_graph);
		}

		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(channel1_widget))) {
			channel1_graph = gtk_databox_lines_new(num_samples, X, channel1, &color_graph1, 2);
			gtk_databox_graph_add(GTK_DATABOX(databox), channel1_graph);
		}
	}

	add_grid();
	if (is_constalation)
		gtk_databox_set_total_limits(GTK_DATABOX(databox), -8500.0, 8500.0, 8500.0, -8500.0);
	else
		gtk_databox_set_total_limits(GTK_DATABOX(databox), 0.0, num_samples, 8500.0, -8500.0);

	gtk_widget_queue_draw(GTK_WIDGET(databox));

	frame_counter = 0;

	capture_function = g_idle_add((GSourceFunc) time_capture_func, databox);
}

static void capture_button_clicked(GtkToggleButton *btn, gpointer data)
{
	if (gtk_toggle_button_get_active(btn)) {
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fft_radio)))
			start_capture_fft();
		else
			start_capture_time();
	} else {
		g_source_remove(capture_function);
	}
}

static void show_grid_toggled(GtkToggleButton *btn, gpointer data)
{
	if (grid) {
		gtk_databox_graph_set_hide(grid, !gtk_toggle_button_get_active(btn));
		gtk_widget_queue_draw(GTK_WIDGET (data));
	}
}

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static unsigned int num_tx, num_rx;

static void rx_update_labels(void)
{
	double freq = 0.0;
	char buf[100];
	int i;

	set_dev_paths("ad9523-lpc");
	read_devattr_double("out_altvoltage2_ADC_CLK_frequency", &adc_freq);
	adc_freq /= 1000000.0;
	snprintf(buf, sizeof(buf), "%.4f Mhz", adc_freq);
	gtk_label_set_text(GTK_LABEL(adc_freq_label), buf);

	set_dev_paths("adf4351-rx-lpc");
	read_devattr_double("out_altvoltage0_frequency", &freq);
	freq /= 1000000.0;
	snprintf(buf, sizeof(buf), "%.4f Mhz", freq);
	gtk_label_set_text(GTK_LABEL(rx_lo_freq_label), buf);

	if (is_fft_mode) {
		/* In FFT mode we need to scale the X-axis according to the selected
		 * sampling frequency. */
		for (i = 0; i < num_samples / 2; i++)
			X[i] = i * adc_freq / num_samples;
		gtk_databox_set_total_limits(GTK_DATABOX(databox), 0.0, adc_freq / 2.0, 0.0, -75.0);
	}
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

static void tx_save_button_clicked(GtkButton *btn, gpointer data)
{
	iio_save_widgets(tx_widgets, num_tx);
}

static void rx_save_button_clicked(GtkButton *btn, gpointer data)
{
	iio_save_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

static void zoom_fit(GtkButton *btn, gpointer data)
{
	rescale_databox(GTK_DATABOX(data), 0.05);
}

static void zoom_in(GtkButton *btn, gpointer data)
{
	gfloat left, right, top, bottom;
	gfloat width, height;

	gtk_databox_get_visible_limits(GTK_DATABOX(data), &left, &right, &top, &bottom);
	width = right - left;
	height = bottom - top;
	left += width * 0.25;
	right -= width * 0.25;
	top += height * 0.25;
	bottom -= height * 0.25;

	gtk_databox_set_visible_limits(GTK_DATABOX(data), left, right, top, bottom);
}

static void zoom_out(GtkButton *btn, gpointer data)
{
	gtk_databox_zoom_out(GTK_DATABOX(data));
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

static void g_builder_connect_signal(GtkBuilder *builder, const gchar *name,
	const gchar *signal, GCallback callback, gpointer data)
{
	GObject *tmp;
	tmp = gtk_builder_get_object(builder, name);
	g_signal_connect(tmp, signal, callback, data);
}

static const gdouble mhz_scale = 1000000.0;

static void init_application (void)
{
	GtkWidget *window;
	GtkWidget *box2;
	GtkWidget *table;
	GtkWidget *tmp;
	GtkBuilder *builder;

	builder = gtk_builder_new();

	gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "osc.glade", NULL);

	window = GTK_WIDGET(gtk_builder_get_object(builder, "toplevel"));
	box2 = GTK_WIDGET(gtk_builder_get_object(builder, "box2"));
	sample_count_widget = GTK_WIDGET(gtk_builder_get_object(builder, "sample_count"));
	fft_size_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size"));
	channel0_widget = GTK_WIDGET(gtk_builder_get_object(builder, "channel1"));
	channel1_widget = GTK_WIDGET(gtk_builder_get_object(builder, "channel2"));
	fft_radio = GTK_WIDGET(gtk_builder_get_object(builder, "type_fft"));
	time_radio = GTK_WIDGET(gtk_builder_get_object(builder, "type"));
	constalation_radio = GTK_WIDGET(gtk_builder_get_object(builder, "type_constalation"));
	adc_freq_label = GTK_WIDGET(gtk_builder_get_object(builder, "adc_freq_label"));
	rx_lo_freq_label = GTK_WIDGET(gtk_builder_get_object(builder, "rx_lo_freq_label"));
	show_grid = GTK_WIDGET(gtk_builder_get_object(builder, "show_grid"));
	enable_auto_scale = GTK_WIDGET(gtk_builder_get_object(builder, "auto_scale"));
	printf("%p\n", enable_auto_scale);

	/* Bind the IIO device files to the GUI widgets */
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage0_1A_raw",
			builder, "dds_enable");
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage0_1A_frequency",
			builder, "dds_tone1_freq", &mhz_scale);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage2_2A_frequency",
			builder, "dds_tone1_freq", &mhz_scale);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage1_1B_frequency",
			builder, "dds_tone2_freq", &mhz_scale);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage3_2B_frequency",
			builder, "dds_tone2_freq", &mhz_scale);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage0_1A_scale",
			builder, "dds_tone1_scale", compare_gain);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage2_2A_scale",
			builder, "dds_tone1_scale", compare_gain);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage1_1B_scale",
			builder, "dds_tone2_scale", compare_gain);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_altvoltage3_2B_scale",
			builder, "dds_tone2_scale", compare_gain);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage0_calibbias",
			builder, "dac_calibbias0", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage0_calibscale",
			builder, "dac_calibscale0", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage0_phase",
			builder, "dac_calibphase0", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage0_calibbias",
			builder, "dac_calibbias1", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage1_calibscale",
			builder, "dac_calibscale1", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"cf-ad9122-core-lpc", "out_voltage1_phase",
			builder, "dac_calibphase1", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_frequency",
			builder, "tx_lo_freq", &mhz_scale);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
			"adf4351-tx-lpc", "out_altvoltage0_frequency_resolution",
			builder, "tx_lo_spacing", NULL);

	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_frequency",
			builder, "rx_lo_freq", &mhz_scale);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"adf4351-rx-lpc", "out_altvoltage0_frequency_resolution",
			builder, "rx_lo_spacing", NULL);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"ad9523-lpc", "out_altvoltage2_ADC_CLK_frequency",
			builder, "adc_freq", &mhz_scale);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"cf-ad9643-core-lpc", "in_voltage0_calibbias",
			builder, "adc_calibbias0", NULL);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"cf-ad9643-core-lpc", "in_voltage1_calibbias",
			builder, "adc_calibbias1", NULL);
	iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
			"cf-ad9643-core-lpc", "in_voltage0_calibscale",
			builder, "adc_calibscale0", NULL);
	iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
			"cf-ad9643-core-lpc", "in_voltage1_calibscale",
			builder, "adc_calibscale1", NULL);
	iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
			"ad8366-lpc", "out_voltage0_hardwaregain",
			builder, "adc_gain0", NULL);
	iio_spin_button_init_from_builder(&rx_widgets[num_rx++],
			"ad8366-lpc", "out_voltage1_hardwaregain",
			builder, "adc_gain1", NULL);

	gtk_combo_box_set_active(GTK_COMBO_BOX(fft_size_widget), 0);

	g_signal_connect(G_OBJECT(window), "destroy",
			 G_CALLBACK(gtk_main_quit), NULL);

	/* Bind the plot mode radio buttons to the sensitivity of the sample count
	 * and FFT size widgets */
	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size_label"));
	g_object_bind_property(fft_radio, "active", tmp, "sensitive", 0);
	g_object_bind_property(fft_radio, "active", fft_size_widget, "sensitive", 0);
	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "sample_count_label"));
	g_object_bind_property(fft_radio, "active", tmp, "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_object_bind_property(fft_radio, "active", sample_count_widget, "sensitive", G_BINDING_INVERT_BOOLEAN);

	num_samples = 1;
	X = g_renew(gfloat, X, num_samples);
	channel0 = g_renew(gfloat, channel0, num_samples);
	channel1 = g_renew(gfloat, channel1, num_samples);

	/* Create a GtkDatabox widget along with scrollbars and rulers */
	gtk_databox_create_box_with_scrollbars_and_rulers(&databox, &table,
							  TRUE, TRUE, TRUE, TRUE);
	gtk_box_pack_start(GTK_BOX(box2), table, TRUE, TRUE, 0);
	gtk_widget_modify_bg(databox, GTK_STATE_NORMAL, &color_background);

	add_grid();

	gtk_widget_set_size_request(table, 800, 600);

	g_builder_connect_signal(builder, "capture_button", "toggled",
		G_CALLBACK(capture_button_clicked), NULL);
	g_builder_connect_signal(builder, "adc_settings_save", "clicked",
		G_CALLBACK(rx_save_button_clicked), NULL);
	g_builder_connect_signal(builder, "dds_settings_save", "clicked",
		G_CALLBACK(tx_save_button_clicked), NULL);
	g_builder_connect_signal(builder, "zoom_in", "clicked",
		G_CALLBACK(zoom_in), databox);
	g_builder_connect_signal(builder, "zoom_out", "clicked",
		G_CALLBACK(zoom_out), databox);
	g_builder_connect_signal(builder, "zoom_fit", "clicked",
		G_CALLBACK(zoom_fit), databox);
	g_signal_connect(G_OBJECT(show_grid), "toggled",
		G_CALLBACK(show_grid_toggled), databox);

	gtk_widget_show_all(window);
}

gint main(gint argc, char *argv[])
{
	gtk_init(&argc, &argv);
	init_application();
	tx_update_values();
	rx_update_values();
	gtk_main();

	return 0;
}
