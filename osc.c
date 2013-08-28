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
#include <gtkdatabox_markers.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <malloc.h>
#include <dlfcn.h>

#include <fftw3.h>

#include "iio_widget.h"
#include "iio_utils.h"
#include "int_fft.h"
#include "config.h"
#include "osc_plugin.h"
#include "osc.h"

extern char dev_dir_name[512];

static gfloat *X = NULL;
static gfloat *fft_channel = NULL;
static gfloat fft_corr = 0.0;

gint capture_function = 0;
static int buffer_fd = -1;

static struct buffer data_buffer;
static unsigned int num_samples;

static struct iio_channel_info *channels;
static unsigned int num_active_channels;
static unsigned int num_channels;
static gfloat **channel_data;
static unsigned int current_sample;
static unsigned int bytes_per_sample;

static GtkWidget *databox;
static GtkWidget *sample_count_widget;
static GtkWidget *fft_size_widget, *fft_avg_widget, *fft_pwr_offset_widget;
static GtkWidget *fft_radio, *time_radio, *constellation_radio;
static GtkWidget *show_grid;
static GtkWidget *enable_auto_scale;
static GtkWidget *device_list_widget;
static GtkWidget *capture_button;
static GtkWidget *hor_scale;
static GtkWidget *plot_type;

GtkWidget *capture_graph;

static GtkWidget *rx_lo_freq_label, *adc_freq_label;

static GtkDataboxGraph *fft_graph;
static GtkDataboxGraph *grid;

static GtkDataboxGraph **channel_graph;

#ifndef MAX_MARKERS
#define MAX_MARKERS 4
#endif

static gfloat markX[MAX_MARKERS + 2], markY[MAX_MARKERS + 2];
static GtkDataboxGraph *marker[MAX_MARKERS + 2];
static GtkWidget *marker_label;

static GtkListStore *channel_list_store;

static double adc_freq = 246760000.0;
static char adc_scale[10];

static bool is_fft_mode;

const char *current_device;

static GdkColor color_graph[] = {
	{
		.red = 0,
		.green = 60000,
		.blue = 0,
	},
	{
		.red = 60000,
		.green = 0,
		.blue = 0,
	},
	{
		.red = 0,
		.green = 0,
		.blue = 60000,
	},
	{
		.red = 0,
		.green = 60000,
		.blue = 60000,
	},
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

static GdkColor color_marker = {
	.red = 0xFFFF,
	.green = 0,
	.blue = 0,
};

G_LOCK_DEFINE(buffer_full);

/* Couple helper functions from fru parsing */
void printf_warn (const char * fmt, ...)
{
	return;
}


void printf_err (const char * fmt, ...)
{
	va_list ap;
	va_start(ap,fmt);
	vfprintf(stderr,fmt,ap);
	va_end(ap);
}


void * x_calloc (size_t nmemb, size_t size)
{
	unsigned int *ptr;

	ptr = calloc(nmemb, size);
	if (ptr == NULL)
		printf_err("memory error - calloc returned zero\n");
	return (void *)ptr;
}


struct buffer {
	void *data;
	void *data_copy;
	unsigned int available;
	unsigned int size;
};

static bool is_oneshot_mode(void)
{


	if (strncmp(current_device, "cf-ad9", 5) == 0)
		return true;

	return false;
}

static int buffer_open(unsigned int length)
{
	int ret;
	int fd;

	if (!current_device)
		return -ENODEV;

	set_dev_paths(current_device);

	fd = iio_buffer_open(true);
	if (fd < 0) {
		ret = -errno;
		fprintf(stderr, "Failed to open buffer: %d\n", ret);
		return ret;
	}

	/* Setup ring buffer parameters */
	ret = write_devattr_int("buffer/length", length);
	if (ret < 0) {
		fprintf(stderr, "Failed to set buffer length: %d\n", ret);
		goto err_close;
	}

	/* Enable the buffer */
	ret = write_devattr_int("buffer/enable", 1);
	if (ret < 0) {
		fprintf(stderr, "Failed to enable buffer: %d\n", ret);
		goto err_close;
	}

	return fd;

err_close:
	close(fd);
	return ret;
}

static void buffer_close(unsigned int fd)
{
	int ret;

	if (!current_device)
		return;

	set_dev_paths(current_device);

	/* Enable the buffer */
	ret = write_devattr_int("buffer/enable", 0);
	if (ret < 0) {
		fprintf(stderr, "Failed to disable buffer: %d\n", ret);
	}

	close(fd);
}

#if DEBUG

static int sample_iio_data_continuous(int buffer_fd, struct buffer *buf)
{
	static int offset;
	int i;

	for (i = 0; i < num_samples; i++) {
		((int16_t *)(buf->data))[i*2] = 4096.0f * cos((i + offset) * G_PI / 100) + (rand() % 500 - 250);
		((int16_t *)(buf->data))[i*2+1] = 4096.0f * sin((i + offset) * G_PI / 100) + (rand() % 1000 - 500);
	}

	buf->available = 10;
	offset += 10;

	return 0;
}

static int sample_iio_data_oneshot(struct buffer *buf)
{
	return sample_iio_data(0, buf);
}

#else

static int sample_iio_data_continuous(int buffer_fd, struct buffer *buf)
{
	int ret;

	ret = read(buffer_fd, buf->data + buf->available,
			buf->size - buf->available);
	if (ret == 0)
		return 0;
	if (ret < 0)
		return ret;

	buf->available += ret;

	return 0;
}

static int sample_iio_data_oneshot(struct buffer *buf)
{
	int fd, ret;

	fd = buffer_open(buf->size);
	if (fd < 0)
		return fd;

	ret = sample_iio_data_continuous(fd, buf);

	buffer_close(fd);

	return ret;
}

#endif

static int sample_iio_data(struct buffer *buf)
{
	int ret;

	if (is_oneshot_mode())
		ret = sample_iio_data_oneshot(buf);
	else
		ret = sample_iio_data_continuous(buffer_fd, buf);

	if ((buf->data_copy) && (buf->available == buf->size)) {
		memcpy(buf->data_copy, buf->data, buf->size);
		buf->data_copy = NULL;
		G_UNLOCK(buffer_full);
	}

	return ret;
}

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

/*
 * Fill in an array, of about num times
 */
static void fill_axis(gfloat *buf, gfloat start, gfloat inc, int num)
{
	int i;
	gfloat val = start;

	for (i = 0; i < num; i++) {
		buf[i] = val;
		val += inc;
	}

}

static void add_grid(void)
{
	static gfloat gridy[25], gridx[25];
/*
	This would be a better general solution, but it doesn't really work well
	gfloat left, right, top, bottom;
	int x, y;

	gtk_databox_get_total_limits(GTK_DATABOX(databox), &left, &right, &top, &bottom);
	y = fill_axis(gridy, top, bottom, 20);
	x = fill_axis(gridx, left, right, 20);
	grid = gtk_databox_grid_array_new (y, x, gridy, gridx, &color_grid, 1);
*/

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fft_radio))) {
		fill_axis(gridx, 0, 10, 15);
		fill_axis(gridy, 10, -10, 15);
		grid = gtk_databox_grid_array_new (15, 15, gridy, gridx, &color_grid, 1);
	} else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(constellation_radio))) {
		fill_axis(gridx, -80000, 10000, 18);
		fill_axis(gridy, -80000, 10000, 18);
		grid = gtk_databox_grid_array_new (18, 18, gridy, gridx, &color_grid, 1);
	} else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(time_radio))) {
		fill_axis(gridx, 0, 100, 5);
		fill_axis(gridy, -80000, 10000, 18);
		grid = gtk_databox_grid_array_new (18, 5, gridy, gridx, &color_grid, 1);
	}

	gtk_databox_graph_add(GTK_DATABOX(databox), grid);
	gtk_databox_graph_set_hide(grid, !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(show_grid)));
}


static void rescale_databox(GtkDatabox *box, gfloat border)
{
	bool fixed_aspect = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (constellation_radio));

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

		gtk_databox_set_total_limits(box, min_x, max_x, max_x, min_x);

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

static int sign_extend(unsigned int val, unsigned int bits)
{
	unsigned int shift = 32 - bits;
	return ((int)(val << shift)) >> shift;
}

static void demux_data_stream(void *data_in, gfloat **data_out,
	unsigned int num_samples, unsigned int offset, unsigned int data_out_size,
	struct iio_channel_info *channels, unsigned int num_channels)
{
	unsigned int i, j, n;
	unsigned int val;
	unsigned int k;

	for (i = 0; i < num_samples; i++) {
		n = (offset + i) % data_out_size;
		k = 0;
		for (j = 0; j < num_channels; j++) {
			if (!channels[j].enabled)
				continue;
			switch (channels[j].bytes) {
			case 1:
				val = *(uint8_t *)data_in;
				break;
			case 2:
				switch (channels[j].endianness) {
				case IIO_BE:
					val = be16toh(*(uint16_t *)data_in);
					break;
				case IIO_LE:
					val = le16toh(*(uint16_t *)data_in);
					break;
				default:
					val = 0;
					break;
				}
				break;
			case 4:
				switch (channels[j].endianness) {
				case IIO_BE:
					val = be32toh(*(uint32_t *)data_in);
					break;
				case IIO_LE:
					val = le32toh(*(uint32_t *)data_in);
					break;
				default:
					val = 0;
					break;
				}
				break;
			default:
				continue;
			}
			data_in += channels[j].bytes;
			val >>= channels[j].shift;
			val &= channels[j].mask;
			if (channels[j].is_signed)
				data_out[k][n] = sign_extend(val, channels[j].bits_used);
			else
				data_out[k][n] = val;
			k++;
		}
	}

}

static void abort_sampling(void)
{
	if (buffer_fd >= 0) {
		buffer_close(buffer_fd);
		buffer_fd = -1;
	}
	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(capture_button),
			FALSE);
	G_UNLOCK(buffer_full);
}

static gboolean time_capture_func(GtkDatabox *box)
{
	unsigned int n;
	int ret;

	if (!GTK_IS_DATABOX(box))
		return FALSE;

	ret = sample_iio_data(&data_buffer);
	if (ret < 0) {
		abort_sampling();
		fprintf(stderr, "Failed to capture samples: %s\n", strerror(-ret));
		return FALSE;
	}

	n = data_buffer.available / bytes_per_sample;

	demux_data_stream(data_buffer.data, channel_data, n, current_sample,
			num_samples, channels, num_channels);
	current_sample = (current_sample + n) % num_samples;
	data_buffer.available -= n * bytes_per_sample;
	if (data_buffer.available != 0) {
		memmove(data_buffer.data, data_buffer.data +  n * bytes_per_sample,
			data_buffer.available);
	}
/*
	for (j = 1; j < num_samples; j++) {
		if (data[j * 2 - 2] < trigger && data[j * 2] >= trigger)
			break;
	}
*/
	auto_scale_databox(box);

	gtk_widget_queue_draw(GTK_WIDGET(box));
	usleep(50000);

	fps_counter();

	return TRUE;
}

#if NO_FFTW

static void do_fft()
{
	unsigned int fft_size = num_samples;
	short *real, *imag, *amp, *fft_buf;
	unsigned int cnt, i;
	double avg;

	fft_buf = malloc((fft_size * 2 + fft_size / 2) * sizeof(short));
	if (fft_buf == NULL){
		fprintf(stderr, "malloc failed (%d)\n", __LINE__);
		return;
	}

	real = fft_buf;
	imag = real + fft_size;
	amp = imag+ fft_size;

	cnt = 0;
	for (i = 0; i < fft_size * 2; i += 2) {
		real[cnt] = ((int16_t *)(buf->data))[i];
		imag[cnt] = 0;
		cnt++;
	}

	window(real, fft_size);

	fix_fft(real, imag, (int)log2f(fft_size), 0);
	fix_loud(amp, real, imag, fft_size / 2, 2); /* scale 14->16 bit */

	avg = 1.0f / gtk_spin_button_get_value(GTK_SPIN_BUTTON(fft_avg_widget));

	for (i = 0; i < fft_size / 2; ++i)
		fft_channel[i] = ((1 - avg) * fft_channel[i]) + (avg * amp[i]);

	free(fft_buf);
}

#else

static double win_hanning(int j, int n)
{
	double a = 2.0*M_PI/(n-1), w;

	w = 0.5 * (1.0 - cos(a*j));

	return (w);
}

static void do_fft(struct buffer *buf)
{
	unsigned int fft_size = num_samples;
	unsigned int m = fft_size / 2;
	int i, j, k;
	int cnt;
	static double *in;
	static double *win;
	gfloat mag;
	double avg, pwr_offset;
	static fftw_complex *out;
	static fftw_plan plan_forward;
	static int cached_fft_size = -1;

	unsigned int maxx[MAX_MARKERS + 1];
	gfloat maxY[MAX_MARKERS + 1];

	static GtkTextBuffer *tbuf = NULL;
	GtkTextIter iter;
	char text[256];

	if ((cached_fft_size == -1) || (cached_fft_size != fft_size)) {

		if (cached_fft_size != -1) {
			fftw_destroy_plan(plan_forward);
			fftw_free(in);
			fftw_free(win);
			fftw_free(out);
		}

		in = fftw_malloc(sizeof(double) * fft_size);
		win = fftw_malloc(sizeof(double) * fft_size);
		out = fftw_malloc(sizeof(fftw_complex) * (m + 1));
		plan_forward = fftw_plan_dft_r2c_1d(fft_size, in, out, FFTW_ESTIMATE);

		for (i = 0; i < fft_size; i ++)
			win[i] = win_hanning(i, fft_size);

		cached_fft_size = fft_size;
	}

	for (cnt = 0, i = 0; i < fft_size; i++) {
		/* normalization and scaling see fft_corr */
		in[cnt] = ((int16_t *)(buf->data))[i] * win[cnt];
		cnt++;
	}

	fftw_execute(plan_forward);
	avg = gtk_spin_button_get_value(GTK_SPIN_BUTTON(fft_avg_widget));
	if (avg && avg != 128 )
		avg = 1.0f / avg;

	pwr_offset = gtk_spin_button_get_value(GTK_SPIN_BUTTON(fft_pwr_offset_widget));

	for (j = 0; j <= MAX_MARKERS; j++) {
		maxx[j] = 0;
		maxY[j] = -100.0f;
	}

	for (i = 0; i < m; ++i) {
		mag = 10 * log10((out[i][0] * out[i][0] +
				out[i][1] * out[i][1]) / (m * m)) +
			fft_corr +
			pwr_offset;

		/* it's better for performance to have seperate loops,
		 * rather than do these tests inside the loop, but it makes
		 * the code harder to understand... Oh well...
		 ***/
		if (fft_channel[i] == FLT_MAX) {
			/* Don't average the first iterration */
			 fft_channel[i] = mag;
		} else if (!avg) {
			/* keep peaks */
			if (fft_channel[i] <= mag)
				fft_channel[i] = mag;
		} else if (avg == 128) {
			/* keep min */
			if (fft_channel[i] >= mag)
				fft_channel[i] = mag;
		} else {
			/* do an average */
			fft_channel[i] = ((1 - avg) * fft_channel[i]) + (avg * mag);
		}
		if (MAX_MARKERS) {
			if (i == 0) {
				maxx[0] = 0;
				maxY[0] = fft_channel[0];
			} else {
				for (j = 0; j <= MAX_MARKERS; j++) {
					if  ((fft_channel[i - 1] > maxY[j]) &&
						((!((fft_channel[i - 2] > fft_channel[i - 1]) &&
						 (fft_channel[i - 1] > fft_channel[i]))) &&
						 (!((fft_channel[i - 2] < fft_channel[i - 1]) &&
						 (fft_channel[i - 1] < fft_channel[i]))))) {
						for (k = MAX_MARKERS; k > j; k--) {
							maxY[k] = maxY[k - 1];
							maxx[k] = maxx[k - 1];
						}
						maxY[j] = fft_channel[i - 1];
						maxx[j] = i - 1;
						break;
					}
				}
			}
		}
	}
	if (MAX_MARKERS) {
		if (tbuf == NULL) {
			tbuf = gtk_text_buffer_new(NULL);
			gtk_text_view_set_buffer(GTK_TEXT_VIEW(marker_label), tbuf);
		}

		for (j = 0; j <= MAX_MARKERS; j++) {
			markX[j] = (gfloat)X[maxx[j]];
			markY[j] = (gfloat)fft_channel[maxx[j]];

			sprintf(text, "M%i: %2.2f dB @ %2.2f %s\n",
					j, markY[j], markX[j], adc_scale);

			if (j == 0) {
				gtk_text_buffer_set_text(tbuf, text, -1);
				gtk_text_buffer_get_iter_at_line(tbuf, &iter, 1);
			} else {
				gtk_text_buffer_insert(tbuf, &iter, text, -1);
			}
		}
	}
}

#endif

static gboolean fft_capture_func(GtkDatabox *box)
{
	int ret;

	ret = sample_iio_data(&data_buffer);
	if (ret < 0) {
		abort_sampling();
		fprintf(stderr, "Failed to capture samples: %d\n", ret);
		return FALSE;
	}
	if (data_buffer.available == data_buffer.size) {
		do_fft(&data_buffer);
		data_buffer.available = 0;
		auto_scale_databox(box);
		gtk_widget_queue_draw(GTK_WIDGET(box));
	}

	usleep(5000);

	fps_counter();

	return TRUE;
}

static int fft_capture_setup(void)
{
	int i;
	char buf[10];

	if (num_active_channels != 1)
		return -EINVAL;

	num_samples = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(fft_size_widget)));
	data_buffer.size = num_samples * bytes_per_sample;

	data_buffer.data = g_renew(int8_t, data_buffer.data, data_buffer.size);
	data_buffer.data_copy = NULL;

	X = g_renew(gfloat, X, num_samples / 2);
	fft_channel = g_renew(gfloat, fft_channel, num_samples / 2);

	for (i = 0; i < num_samples / 2; i++)
	{
		X[i] = i * adc_freq / num_samples;
		fft_channel[i] = FLT_MAX;
	}
	is_fft_mode = true;

	/* Compute FFT normalization and scaling offset */
	fft_corr = 20 * log10(2.0 / (1 << (channels[0].bits_used - 1)));

	/*
	 * Init markers
	 */
	if (MAX_MARKERS) {
		for (i = 0; i <= MAX_MARKERS; i++) {
			markX[i] = 0.0f;
			markY[i] = -100.0f;
			marker[i] =  gtk_databox_markers_new(1, &markX[i], &markY[i], &color_marker,
					10, GTK_DATABOX_MARKERS_TRIANGLE);
			sprintf(buf, "M%i", i);
			gtk_databox_markers_set_label(GTK_DATABOX_MARKERS(marker[i]), 0,
					GTK_DATABOX_MARKERS_TEXT_N, buf, FALSE);
			gtk_databox_graph_add(GTK_DATABOX(databox), marker[i]);
		}
	}

	fft_graph = gtk_databox_lines_new(num_samples / 2, X, fft_channel, &color_graph[0], 1);
	gtk_databox_graph_add(GTK_DATABOX(databox), fft_graph);

	gtk_databox_set_total_limits(GTK_DATABOX(databox), -5.0, adc_freq / 2.0 + 5.0, 0.0, -75.0);

	return 0;
}

static void fft_capture_start(void)
{
	capture_function = g_idle_add((GSourceFunc) fft_capture_func, databox);
}

/*
 * helper functions for plugins which want to look at data
 */
int plugin_data_capture_size(void)
{
	return data_buffer.size;
}

int plugin_data_capture_num_active_channels(void)
{
	return num_active_channels;
}

int plugin_data_capture_bytes_per_sample(void)
{
	return bytes_per_sample;
}

void plugin_data_capture_demux(void *buf, gfloat **cooked, unsigned int num_samples,
	unsigned int num_channels)

{
	demux_data_stream(buf, cooked, num_samples, 0, num_samples, channels, num_channels);
}

int plugin_data_capture(void *buf)
{
	/* only one consumer at a time */
	if (data_buffer.data_copy)
		return false;

	data_buffer.data_copy = buf;
	return true;

}

static int time_capture_setup(void)
{
	gboolean is_constellation;
	unsigned int i, j;
	static int prev_num_active_ch = 0;

	is_constellation = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (constellation_radio));

	if (is_constellation && num_active_channels != 2)
		return -EINVAL;

	gtk_databox_graph_remove_all(GTK_DATABOX(databox));

	num_samples = gtk_spin_button_get_value(GTK_SPIN_BUTTON(sample_count_widget));
	data_buffer.size = num_samples * bytes_per_sample;

	data_buffer.data = g_renew(int8_t, data_buffer.data, data_buffer.size);
	data_buffer.data_copy = NULL;

	X = g_renew(gfloat, X, num_samples);

	for (i = 0; i < num_samples; i++)
		X[i] = i;

	is_fft_mode = false;

	if (channel_data)
		for (i = 0; i < prev_num_active_ch; i++)
			g_free(channel_data[i]);

	channel_data = g_renew(gfloat *, channel_data, num_active_channels);
	channel_graph = g_renew(GtkDataboxGraph *, channel_graph, num_active_channels);
	for (i = 0; i < num_active_channels; i++) {
		channel_data[i] = g_new(gfloat, num_samples);
		for (j = 0; j < num_samples; j++)
			channel_data[i][j] = 0.0f;
	}

	prev_num_active_ch = num_active_channels;

	if (is_constellation) {
		if (strcmp(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(plot_type)), "Lines"))
			fft_graph = gtk_databox_points_new(num_samples, channel_data[0],
					channel_data[1], &color_graph[0], 3);
		else
			fft_graph = gtk_databox_lines_new(num_samples, channel_data[0],
					channel_data[1], &color_graph[0], 1);
		gtk_databox_graph_add(GTK_DATABOX (databox), fft_graph);
	} else {
		j = 0;
		for (i = 0; i < num_channels; i++) {
			if (!channels[i].enabled)
				continue;

			if (strcmp(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(plot_type)), "Lines"))
				channel_graph[j] = gtk_databox_points_new(num_samples, X,
					channel_data[j], &color_graph[i], 3);
			else
				channel_graph[j] = gtk_databox_lines_new(num_samples, X,
					channel_data[j], &color_graph[i], 1);

			gtk_databox_graph_add(GTK_DATABOX(databox), channel_graph[j]);
			j++;
		}
	}

	if (is_constellation)
		gtk_databox_set_total_limits(GTK_DATABOX(databox), -8500.0, 8500.0, 8500.0, -8500.0);
	else
		gtk_databox_set_total_limits(GTK_DATABOX(databox), 0.0, num_samples, 8500.0, -8500.0);

	return 0;
}

static void time_capture_start()
{
	capture_function = g_idle_add((GSourceFunc) time_capture_func, databox);
}

static void capture_button_clicked(GtkToggleToolButton *btn, gpointer data)
{
	unsigned int i;
	int ret;

	if (gtk_toggle_tool_button_get_active(btn)) {
		gtk_databox_graph_remove_all(GTK_DATABOX(databox));

		G_UNLOCK(buffer_full);

		data_buffer.available = 0;
		current_sample = 0;
		num_active_channels = 0;
		bytes_per_sample = 0;
		for (i = 0; i < num_channels; i++) {
			if (channels[i].enabled) {
				bytes_per_sample += channels[i].bytes;
				num_active_channels++;
			}
		}

		if (num_active_channels == 0 || !current_device)
			goto play_err;

		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fft_radio))) {
			gtk_label_set_text(GTK_LABEL(hor_scale), adc_scale);
			gtk_widget_show(marker_label);
			ret = fft_capture_setup();
		} else {
			gtk_label_set_text(GTK_LABEL(hor_scale), "Samples");
			gtk_widget_hide(marker_label);
			ret = time_capture_setup();
		}

		if (ret)
			goto play_err;

		if (!is_oneshot_mode()) {
			buffer_fd = buffer_open(num_samples);
			if (buffer_fd < 0)
				goto play_err;
		}

		add_grid();
		gtk_widget_queue_draw(GTK_WIDGET(databox));
		frame_counter = 0;

		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(fft_radio)))
			fft_capture_start();
		else
			time_capture_start();

	} else {
		if (capture_function > 0) {
			g_source_remove(capture_function);
			capture_function = 0;
			G_UNLOCK(buffer_full);
		}
		if (buffer_fd >= 0) {
			buffer_close(buffer_fd);
			buffer_fd = -1;
		}
	}

	return;

play_err:
	gtk_toggle_tool_button_set_active(btn, FALSE);
}

static void show_grid_toggled(GtkToggleButton *btn, gpointer data)
{
	if (grid) {
		gtk_databox_graph_set_hide(grid, !gtk_toggle_button_get_active(btn));
		gtk_widget_queue_draw(GTK_WIDGET (data));
	}
}

static double read_sampling_frequency(void)
{
	double freq = 1.0;
	int ret;

	if (set_dev_paths(current_device) < 0)
		return -1.0f;

	if (iio_devattr_exists(current_device, "in_voltage_sampling_frequency")) {
		read_devattr_double("in_voltage_sampling_frequency", &freq);
		if (freq < 0)
			freq = ((double)4294967296) + freq;
	} else if (iio_devattr_exists(current_device, "sampling_frequency")) {
		read_devattr_double("sampling_frequency", &freq);
	} else {
		char *trigger;

		ret = read_devattr("trigger/current_trigger", &trigger);
		if (ret >= 0) {
			if (*trigger != '\0') {
				set_dev_paths(trigger);
				if (iio_devattr_exists(trigger, "frequency"))
					read_devattr_double("frequency", &freq);
			}
			free(trigger);
		} else
			freq = -1.0f;
	}

	return freq;
}

void rx_update_labels(void)
{
	double freq = 0.0;
	char buf[20];
	int i;

	adc_freq = read_sampling_frequency();

	if (adc_freq >= 1000000) {
		sprintf(adc_scale, "MHz");
		adc_freq /= 1000000;
	} else if(adc_freq >= 1000) {
		sprintf(adc_scale, "kHz");
		adc_freq /= 1000;
	} else if(adc_freq >= 0) {
		sprintf(adc_scale, "Hz");
	} else {
		sprintf(adc_scale, "???");
		adc_freq = 0;
	}

	snprintf(buf, sizeof(buf), "%.3f %s", adc_freq, adc_scale);

	gtk_label_set_text(GTK_LABEL(adc_freq_label), buf);

	if (!set_dev_paths("adf4351-rx-lpc"))
		read_devattr_double("out_altvoltage0_frequency", &freq);
	else if (!set_dev_paths("ad9361-phy"));
		read_devattr_double("out_altvoltage0_RX_LO_frequency", &freq);

	freq /= 1000000.0;
	snprintf(buf, sizeof(buf), "%.4f Mhz", freq);
	gtk_label_set_text(GTK_LABEL(rx_lo_freq_label), buf);

	if (is_fft_mode) {
		/*
		 * In FFT mode we need to scale the X-axis according to the selected
		 * sampling frequency.
		 */
		for (i = 0; i < num_samples / 2; i++)
			X[i] = i * adc_freq / num_samples;
		gtk_databox_set_total_limits(GTK_DATABOX(databox), 0.0, adc_freq / 2.0, 0.0, -75.0);
	}
}

static void zoom_fit(GtkButton *btn, gpointer data)
{
	rescale_databox(GTK_DATABOX(data), 0.05);
}

static void zoom_in(GtkButton *btn, gpointer data)
{
	bool fixed_aspect = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (constellation_radio));
	gfloat left, right, top, bottom;
	gfloat width, height;

	gtk_databox_get_visible_limits(GTK_DATABOX(data), &left, &right, &top, &bottom);
	width = right - left;
	height = bottom - top;
	left += width * 0.25;
	right -= width * 0.25;
	top += height * 0.25;
	bottom -= height * 0.25;

	if (fixed_aspect) {
		gfloat diff;
		width *= 0.5;
		height *= -0.5;
		if (height > width) {
			diff = width - height;
			left -= diff * 0.5;
			right += diff * 0.5;
		} else {
			diff = height - width;
			bottom += diff * 0.5;
			top -= diff * 0.5;
		}
	}

	gtk_databox_set_visible_limits(GTK_DATABOX(data), left, right, top, bottom);
}

static void zoom_out(GtkButton *btn, gpointer data)
{
	bool fixed_aspect = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (constellation_radio));
	gfloat left, right, top, bottom;
	gfloat t_left, t_right, t_top, t_bottom;
	gfloat width, height;

	gtk_databox_get_visible_limits(GTK_DATABOX(data), &left, &right, &top, &bottom);
	width = right - left;
	height = bottom - top;
	left -= width * 0.25;
	right += width * 0.25;
	top -= height * 0.25;
	bottom += height * 0.25;

	gtk_databox_get_total_limits(GTK_DATABOX(data), &t_left, &t_right, &t_top, &t_bottom);
	if (left < right) {
		if (left < t_left)
			left = t_left;
		if (right > t_right)
			right = t_right;
	} else {
		if (left > t_left)
			left = t_left;
		if (right < t_right)
			right = t_right;
	}

	if (top < bottom) {
		if (top < t_top)
			top = t_top;
		if (bottom > t_bottom)
			bottom = t_bottom;
	} else {
		if (top > t_top)
			top = t_top;
		if (bottom < t_bottom)
			bottom = t_bottom;
	}

	if (fixed_aspect) {
		gfloat diff;
		width = right - left;
		height = top - bottom;
		if (height < width) {
			diff = width - height;
			bottom -= diff * 0.5;
			top += diff * 0.5;
			if (top < t_top) {
				bottom += t_top - top;
				top = t_top;
			}
			if (bottom > t_bottom) {
				top -= bottom - t_bottom;
				bottom = t_bottom;
			}
		} else {
			diff = height - width;
			left -= diff * 0.5;
			right += diff * 0.5;
			if (left < t_left) {
				right += t_left - left;
				left = t_left;
			}
			if (right > t_right) {
				left -= right - t_right;
				right = t_right;
			}
		}
		width = right - left;
		height = top - bottom;
	}

	gtk_databox_set_visible_limits(GTK_DATABOX(data), left, right, top, bottom);
}

static bool force_plugin(const char *name)
{
	const char *force_plugin = getenv("OSC_FORCE_PLUGIN");
	const char *pos;

	if (!force_plugin)
		return false;

	if (strcmp(force_plugin, "all") == 0)
		return true;

	pos = strcasestr(force_plugin, name);
	if (pos) {
		switch (*(pos + strlen(name))) {
		case ' ':
		case '\0':
			return true;
		default:
			break;
		}
	}

	return false;
}

static void load_plugin(const char *name, GtkWidget *notebook)
{
	const struct osc_plugin *plugin;
	void *lib;

	lib = dlopen(name, RTLD_LOCAL | RTLD_LAZY);
	if (!lib) {
		fprintf(stderr, "Failed to load plugin \"%s\": %s\n", name, dlerror());
		return;
	}

	plugin = dlsym(lib, "plugin");
	if (!plugin) {
		fprintf(stderr, "Failed to load plugin \"%s\": Could not find plugin\n",
				name);
		return;
	}

	printf("Found plugin: %s\n", plugin->name);

	if (!plugin->identify() && !force_plugin(plugin->name))
		return;

	plugin->init(notebook);

	printf("Loaded plugin: %s\n", plugin->name);
}

static bool str_endswith(const char *str, const char *needle)
{
	const char *pos;
	pos = strstr(str, needle);
	if (pos == NULL)
		return false;
	return *(pos + strlen(needle)) == '\0';
}

static void load_plugins(GtkWidget *notebook)
{
	struct dirent *ent;
	char *plugin_dir = "plugins";
	char buf[512];
	DIR *d;

	/* Check the local plugins folder first */
	d = opendir(plugin_dir);
	if (!d) {
		plugin_dir = OSC_PLUGIN_PATH;
		d = opendir(plugin_dir);
	}

	while ((ent = readdir(d))) {
		if (ent->d_type != DT_REG)
			continue;
		if (!str_endswith(ent->d_name, ".so"))
			continue;
		snprintf(buf, sizeof(buf), "%s/%s", plugin_dir, ent->d_name);
		load_plugin(buf, notebook);
	}

	free(d);
}

static bool is_input_device(const char *device)
{
	struct iio_channel_info *channels = NULL;
	unsigned int num_channels;
	bool is_input = false;
	int ret;
	int i;

	set_dev_paths(device);

	ret = build_channel_array(dev_dir_name, &channels, &num_channels);
	if (ret)
		return false;

	for (i = 0; i < num_channels; i++) {
		if (strncmp("in", channels[i].name, 2) == 0) {
			is_input = true;
			break;
		}
	}

	free_channel_array(channels, num_channels);

	return is_input;
}

static void device_list_cb(GtkWidget *widget, gpointer data)
{
	GtkTreeIter iter;
	int ret;
	int i;

	gtk_list_store_clear(channel_list_store);
	if (num_channels)
		free_channel_array(channels, num_channels);

	current_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(device_list_widget));

	trigger_update_current_device();

	if (!current_device)
		return;

	set_dev_paths(current_device);

	ret = build_channel_array(dev_dir_name, &channels, &num_channels);
	if (ret)
		return;

	for (i = 0; i < num_channels; i++) {
		if (strncmp("in", channels[i].name, 2) == 0 &&
			strcmp("in_timestamp", channels[i].name) != 0)
		{
			gtk_list_store_append(channel_list_store, &iter);
			gtk_list_store_set(channel_list_store, &iter, 0, channels[i].name,
				1, channels[i].enabled, 2, &channels[i], -1);
		}
	}

}

static void init_device_list(void)
{
	char *devices = NULL, *device;
	unsigned int num;

	g_signal_connect(device_list_widget, "changed",
			G_CALLBACK(device_list_cb), NULL);

	num = find_iio_names(&devices, "iio:device");
	if (devices != NULL) {
		device = devices;
		for (; num > 0; num--) {
			if (is_input_device(device)) {
				gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(device_list_widget),
						device);
			}
			device += strlen(device) + 1;
		}
		free(devices);

		gtk_combo_box_set_active(GTK_COMBO_BOX(device_list_widget), 0);
	}

	device_list_cb(device_list_widget, NULL);
}

void channel_toggled(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer data)
{
	GtkTreePath* path = gtk_tree_path_new_from_string(pathStr);
	struct iio_channel_info *channel;
	GtkTreeIter iter;
	unsigned int enabled;
	char buf[512];
	FILE *f;
	int ret;

	set_dev_paths(current_device);

	gtk_tree_model_get_iter(GTK_TREE_MODEL (data), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL (data), &iter, 1, &enabled, 2, &channel, -1);
	enabled = !enabled;

	snprintf(buf, sizeof(buf), "%s/scan_elements/%s_en", dev_dir_name, channel->name);
	f = fopen(buf, "w");
	if (f) {
		fprintf(f, "%u\n", enabled);
		fclose(f);

		f = fopen(buf, "r");
		ret = fscanf(f, "%u", &enabled);
		if (ret != 1)
			enabled = false;
		fclose(f);
	} else
		enabled = false;

	channel->enabled = enabled;
	gtk_list_store_set(GTK_LIST_STORE (data), &iter, 1, enabled, -1);
}

static gboolean capture_button_icon_transform(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer user_data)
{
	if (g_value_get_boolean(source_value))
		g_value_set_static_string(target_value, "gtk-stop");
	else
		g_value_set_static_string(target_value, "gtk-media-play");

	return TRUE;
}

void application_quit (void)
{
	if (capture_function > 0) {
		g_source_remove(capture_function);
		capture_function = 0;
		G_UNLOCK(buffer_full);
	}
	if (buffer_fd >= 0) {
		buffer_close(buffer_fd);
		buffer_fd = -1;
	}

	gtk_main_quit();
}

void sigterm (int signum)
{
	application_quit();
}

static void init_application (void)
{
	GtkWidget *window;
	GtkWidget *table;
	GtkWidget *tmp;
	GtkWidget *notebook;
	GtkBuilder *builder;

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "./osc.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "osc.glade", NULL);
	else {
		GtkImage *logo;
		GtkAboutDialog *about;
		GdkPixbuf *pixbuf;
		GError *err = NULL;

		/* We are running locally, so load the local files */
		logo = GTK_IMAGE(gtk_builder_get_object(builder, "ADI_logo"));
		g_object_set(logo, "file","./icons/ADIlogo.png", NULL);
		logo = GTK_IMAGE(gtk_builder_get_object(builder, "about_ADI_logo"));
		g_object_set(logo, "file","./icons/ADIlogo.png", NULL);
		logo = GTK_IMAGE(gtk_builder_get_object(builder, "about_IIO_logo"));
		g_object_set(logo, "file","./icons/IIOlogo.png", NULL);
		about = GTK_ABOUT_DIALOG(gtk_builder_get_object(builder, "About_dialog"));
		pixbuf = gdk_pixbuf_new_from_file("./icons/osc128.png", &err);
		if (pixbuf) {
			g_object_set(about, "logo", pixbuf,  NULL);
			g_object_unref(pixbuf);
		}
	}

	window = GTK_WIDGET(gtk_builder_get_object(builder, "toplevel"));
	capture_graph = GTK_WIDGET(gtk_builder_get_object(builder, "display_capture"));
	sample_count_widget = GTK_WIDGET(gtk_builder_get_object(builder, "sample_count"));
	fft_size_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size"));
	fft_avg_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_avg"));
	fft_pwr_offset_widget = GTK_WIDGET(gtk_builder_get_object(builder, "pwr_offset"));
	fft_radio = GTK_WIDGET(gtk_builder_get_object(builder, "type_fft"));
	time_radio = GTK_WIDGET(gtk_builder_get_object(builder, "type"));
	constellation_radio = GTK_WIDGET(gtk_builder_get_object(builder, "type_constellation"));
	adc_freq_label = GTK_WIDGET(gtk_builder_get_object(builder, "adc_freq_label"));
	rx_lo_freq_label = GTK_WIDGET(gtk_builder_get_object(builder, "rx_lo_freq_label"));
	show_grid = GTK_WIDGET(gtk_builder_get_object(builder, "show_grid"));
	enable_auto_scale = GTK_WIDGET(gtk_builder_get_object(builder, "auto_scale"));
	notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook"));
	device_list_widget = GTK_WIDGET(gtk_builder_get_object(builder, "input_device_list"));
	capture_button = GTK_WIDGET(gtk_builder_get_object(builder, "capture_button"));
	hor_scale = GTK_WIDGET(gtk_builder_get_object(builder, "hor_scale"));
	marker_label = GTK_WIDGET(gtk_builder_get_object(builder, "marker_info"));
	plot_type = GTK_WIDGET(gtk_builder_get_object(builder, "plot_type"));

	channel_list_store = GTK_LIST_STORE(gtk_builder_get_object(builder, "channel_list"));
	g_builder_connect_signal(builder, "channel_toggle", "toggled",
		G_CALLBACK(channel_toggled), channel_list_store);

	dialogs_init(builder);
	trigger_dialog_init(builder);

	gtk_combo_box_set_active(GTK_COMBO_BOX(fft_size_widget), 0);

	/* Bind the plot mode radio buttons to the sensitivity of the sample count
	 * and FFT size widgets */
	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size_label"));
	g_object_bind_property(fft_radio, "active", tmp, "visible", 0);
	g_object_bind_property(fft_radio, "active", fft_size_widget, "visible", 0);
	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "fft_avg_label"));
	g_object_bind_property(fft_radio, "active", tmp, "visible", 0);
	g_object_bind_property(fft_radio, "active", fft_avg_widget, "visible", 0);
	g_object_bind_property(fft_radio, "active", fft_pwr_offset_widget, "visible", 0);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "sample_count_label"));
	g_object_bind_property(fft_radio, "active", tmp, "visible", G_BINDING_INVERT_BOOLEAN);
	g_object_bind_property(fft_radio, "active", sample_count_widget, "visible", G_BINDING_INVERT_BOOLEAN);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "plot_type_label"));
	g_object_bind_property(fft_radio, "active", tmp, "visible", G_BINDING_INVERT_BOOLEAN);
	g_object_bind_property(fft_radio, "active", plot_type, "visible", G_BINDING_INVERT_BOOLEAN);
	gtk_combo_box_set_active(GTK_COMBO_BOX(plot_type), 0);

	num_samples = 1;
	X = g_renew(gfloat, X, num_samples);
	fft_channel = g_renew(gfloat, fft_channel, num_samples);

	/* Create a GtkDatabox widget along with scrollbars and rulers */
	gtk_databox_create_box_with_scrollbars_and_rulers(&databox, &table,
							TRUE, TRUE, TRUE, TRUE);
	gtk_box_pack_start(GTK_BOX(capture_graph), table, TRUE, TRUE, 0);
	gtk_widget_modify_bg(databox, GTK_STATE_NORMAL, &color_background);

	add_grid();

	gtk_widget_set_size_request(table, 600, 600);

	g_builder_connect_signal(builder, "capture_button", "toggled",
		G_CALLBACK(capture_button_clicked), NULL);
	g_builder_connect_signal(builder, "zoom_in", "clicked",
		G_CALLBACK(zoom_in), databox);
	g_builder_connect_signal(builder, "zoom_out", "clicked",
		G_CALLBACK(zoom_out), databox);
	g_builder_connect_signal(builder, "zoom_fit", "clicked",
		G_CALLBACK(zoom_fit), databox);
	g_signal_connect(G_OBJECT(show_grid), "toggled",
		G_CALLBACK(show_grid_toggled), databox);

	g_signal_connect(G_OBJECT(window), "destroy",
			G_CALLBACK(application_quit), NULL);

	g_builder_bind_property(builder, "capture_button", "active",
			"channel_list_view", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
			"input_device_list", "sensitive", G_BINDING_INVERT_BOOLEAN);

	g_object_bind_property_full(capture_button, "active", capture_button,
			"stock-id", 0, capture_button_icon_transform, NULL, NULL, NULL);

	init_device_list();
	load_plugins(notebook);
	rx_update_labels();

	gtk_widget_show(window);
	gtk_widget_show_all(capture_graph);
}

gint main(gint argc, char *argv[])
{
	g_thread_init (NULL);
	gdk_threads_init ();
	gtk_init(&argc, &argv);

	signal(SIGTERM, sigterm);
	signal(SIGINT, sigterm);
	signal(SIGHUP, sigterm);

	gdk_threads_enter();
	init_application();
	gtk_main();
	gdk_threads_leave();

	return 0;
}
