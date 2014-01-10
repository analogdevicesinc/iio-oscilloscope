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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fftw3.h>

#include "osc.h"
#include "iio_widget.h"
#include "iio_utils.h"
#include "int_fft.h"
#include "config.h"
#include "osc_plugin.h"
#include "ini/ini.h"

#define SAMPLE_COUNT_MIN_VALUE 10
#define SAMPLE_COUNT_MAX_VALUE 1000000ul

GSList *plugin_list = NULL;

static gfloat *X = NULL;
static gfloat *fft_channel = NULL;
static gfloat fft_corr = 0.0;
gfloat plugin_fft_corr = 0.0;

gint capture_function = 0;
static int buffer_fd = -1;

static struct buffer data_buffer;
unsigned int num_samples;
unsigned int num_samples_ploted;
static struct iio_channel_info *channels;
unsigned int num_active_channels;
int cached_num_active_channels = -1;
static unsigned int num_channels;
gfloat **channel_data;
static unsigned int current_sample;
static unsigned int bytes_per_sample;

static GtkWidget *databox;
static GtkWidget *time_interval_widget;
static GtkWidget *sample_count_widget;
static GtkWidget *fft_size_widget, *fft_avg_widget, *fft_pwr_offset_widget;
GtkWidget *plot_domain;

static GtkWidget *show_grid;
static GtkWidget *enable_auto_scale;
static GtkWidget *device_list_widget;
static GtkWidget *capture_button;
static GtkWidget *hor_scale;
static GtkWidget *plot_type;
static GtkWidget *time_unit_lbl;
static gulong capture_button_hid = 0;
static GBinding *capture_button_bind;
static GtkWidget *notebook;

GtkWidget *capture_graph;

static GtkWidget *rx_lo_freq_label, *adc_freq_label;

static GtkDataboxGraph *fft_graph;
static GtkDataboxGraph *grid;

static GtkDataboxGraph **channel_graph;

static struct marker_type markers[MAX_MARKERS + 2];
static struct marker_type *markers_copy;
static GtkWidget *marker_label;
static enum marker_types marker_type;

struct detachable_plugin {
	const struct osc_plugin *plugin;
	gboolean detached_state;
	GtkWidget *detach_attach_button;
};

#define FORCE_UPDATE TRUE
#define NORMAL_UPDATE FALSE

static GSList *dplugin_list = NULL;

static GtkListStore *channel_list_store;

double adc_freq = 246760000.0;
static double adc_freq_raw;
static double lo_freq = 0.0;
char adc_scale[10];
int do_a_rescale_flag;
int deactivate_capture_btn_flag;

static bool is_fft_mode;
static int (*plugin_setup_validation_fct)(struct iio_channel_info*, int, char **) = NULL;
static struct plugin_check_fct *setup_check_functions = NULL;
static int num_check_fcts = 0;

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
	.blue = 0xFFFF,
};

G_LOCK_DEFINE_STATIC(buffer_full);
G_LOCK_DEFINE_STATIC(markers_copy);

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
	if (strncmp(current_device, "ad-mc-", 5) == 0)
		return true;

	return false;
}

static int buffer_open(unsigned int length, int flags)
{
	int ret;
	int fd;

	if (!current_device)
		return -ENODEV;

	set_dev_paths(current_device);

	fd = iio_buffer_open(true, flags);
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
static int sample_iio_data(struct buffer *buf);

static int sample_iio_data_oneshot(struct buffer *buf)
{
	return sample_iio_data(buf);
}

#else

static int sample_iio_data_continuous(int buffer_fd, struct buffer *buf)
{
	int ret;

	ret = read(buffer_fd, buf->data + buf->available,
			buf->size - buf->available);
	if (ret == 0)
		return 0;
	if (ret < 0) {
		if (errno == EAGAIN)
			return 0;
		else
			return -errno;
	}

	buf->available += ret;

	return 0;
}

static int sample_iio_data_oneshot(struct buffer *buf)
{
	int fd, ret;

	fd = buffer_open(buf->size, 0);
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

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == FFT_PLOT) {
		fill_axis(gridx, -30, 10, 15);
		fill_axis(gridy, 10, -10, 15);
		grid = gtk_databox_grid_array_new (15, 15, gridy, gridx, &color_grid, 1);
	} else if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == XY_PLOT) {
		fill_axis(gridx, -80000, 10000, 18);
		fill_axis(gridy, -80000, 10000, 18);
		grid = gtk_databox_grid_array_new (18, 18, gridy, gridx, &color_grid, 1);
	} else if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == TIME_PLOT) {
		fill_axis(gridx, 0, 100, 5);
		fill_axis(gridy, -80000, 10000, 18);
		grid = gtk_databox_grid_array_new (18, 5, gridy, gridx, &color_grid, 1);
	}

	gtk_databox_graph_add(GTK_DATABOX(databox), grid);
	gtk_databox_graph_set_hide(grid, !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(show_grid)));
}


static void rescale_databox(GtkDatabox *box, gfloat border)
{
	bool fixed_aspect = gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == XY_PLOT;

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
	if ((frame_counter == 0) || (do_a_rescale_flag == 1)) {
		do_a_rescale_flag = 0;
		rescale_databox(box, 0.05);
	}
}

static int sign_extend(unsigned int val, unsigned int bits)
{
	unsigned int shift = 32 - bits;
	return ((int)(val << shift)) >> shift;
}

static void demux_data_stream(void *data_in, gfloat **data_out,
	unsigned int num_sam, unsigned int offset, unsigned int data_out_size,
	struct iio_channel_info *channels, unsigned int num_channels)
{
	unsigned int i, j, n;
	unsigned int val;
	unsigned int k;

	for (i = 0; i < num_sam; i++) {
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
	G_TRYLOCK(buffer_full);
	G_UNLOCK(buffer_full);
	G_TRYLOCK(markers_copy);
	G_UNLOCK(markers_copy);
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
	static unsigned int m;
	int i, j, k;
	int cnt;
	static double *in;
	static fftw_complex *in_c;
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

	if ((cached_fft_size == -1) || (cached_fft_size != fft_size) ||
		(cached_num_active_channels != num_active_channels)) {

		if (cached_fft_size != -1) {
			fftw_destroy_plan(plan_forward);
			fftw_free(win);
			fftw_free(out);
			if (in != NULL)
				fftw_free(in);
			if (in_c != NULL)
				fftw_free(in_c);
			in_c = NULL;
			in = NULL;
		}

		win = fftw_malloc(sizeof(double) * fft_size);

		if (num_active_channels == 2) {
			m = fft_size;
			in_c = fftw_malloc(sizeof(fftw_complex) * fft_size);
			in = NULL;
			out = fftw_malloc(sizeof(fftw_complex) * (m + 1));
			plan_forward = fftw_plan_dft_1d(fft_size, in_c, out, FFTW_FORWARD, FFTW_ESTIMATE);

		} else {
			m = fft_size / 2;
			out = fftw_malloc(sizeof(fftw_complex) * (m + 1));
			in_c = NULL;
			in = fftw_malloc(sizeof(double) * fft_size);
			plan_forward = fftw_plan_dft_r2c_1d(fft_size, in, out, FFTW_ESTIMATE);
		}

		for (i = 0; i < fft_size; i ++)
			win[i] = win_hanning(i, fft_size);

		cached_fft_size = fft_size;
		cached_num_active_channels = num_active_channels;
	}

	if (num_active_channels == 2) {
		for (cnt = 0, i = 0; cnt < fft_size; cnt++) {
			/* normalization and scaling see fft_corr */
			in_c[cnt][0] = ((int16_t *)(buf->data))[i++] * win[cnt];
			in_c[cnt][1] = ((int16_t *)(buf->data))[i++] * win[cnt];
		}

	} else  {
		for (cnt = 0, i = 0; i < fft_size; i++) {
			/* normalization and scaling see fft_corr */
			in[cnt] = ((int16_t *)(buf->data))[i] * win[cnt];
			cnt++;
		}
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

		if (num_active_channels == 2) {
			if (i < (m / 2))
				j = i + (m / 2);
			else
				j = i - (m / 2);
		} else {
			j = i;
		}

		mag = 10 * log10((out[j][0] * out[j][0] +
				out[j][1] * out[j][1]) / (m * m)) +
			fft_corr + pwr_offset + plugin_fft_corr;

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

		if (MAX_MARKERS && (marker_type == MARKER_PEAK ||
				    marker_type == MARKER_ONE_TONE ||
				    marker_type == MARKER_IMAGE)) {
			if (i == 0) {
				maxx[0] = 0;
				maxY[0] = fft_channel[0];
			} else {
				for (j = 0; j <= MAX_MARKERS && markers[j].active; j++) {
					if  ((fft_channel[i - 1] > maxY[j]) &&
						((!((fft_channel[i - 2] > fft_channel[i - 1]) &&
						 (fft_channel[i - 1] > fft_channel[i]))) &&
						 (!((fft_channel[i - 2] < fft_channel[i - 1]) &&
						 (fft_channel[i - 1] < fft_channel[i]))))) {
						if (marker_type == MARKER_PEAK) {
							for (k = MAX_MARKERS; k > j; k--) {
								maxY[k] = maxY[k - 1];
								maxx[k] = maxx[k - 1];
							}
						}
						maxY[j] = fft_channel[i - 1];
						maxx[j] = i - 1;
						break;
					}
				}
			}
		}
	}

	if (tbuf == NULL) {
		tbuf = gtk_text_buffer_new(NULL);
		gtk_text_view_set_buffer(GTK_TEXT_VIEW(marker_label), tbuf);
	}

	if (MAX_MARKERS && marker_type != MARKER_OFF) {
		for (j = 0; j <= MAX_MARKERS && markers[j].active; j++) {
			if (marker_type == MARKER_PEAK) {
				markers[j].x = (gfloat)X[maxx[j]];
				markers[j].y = (gfloat)fft_channel[maxx[j]];
				markers[j].bin = maxx[j];
			} else if (marker_type == MARKER_FIXED) {
				markers[j].x = (gfloat)X[markers[j].bin];
				markers[j].y = (gfloat)fft_channel[markers[j].bin];
			} else if (marker_type == MARKER_ONE_TONE) {
				/* assume peak is the tone */
				if (j == 0) {
					markers[j].bin = maxx[j];
					i = 1;
				} else if (j == 1) {
					/* keep DC */
					if (num_active_channels == 2)
						markers[j].bin = m / 2;
					else
						markers[j].bin = 0;
				} else {
					/* where should the spurs be? */
					i++;
					if (num_active_channels == 2) {
						markers[j].bin = (markers[0].bin - (m / 2)) * i + (m / 2);
						if (markers[j].bin > m)
							markers[j].bin -= 2 * (markers[j].bin - m);
						if (markers[j].bin < ( m/2 ))
							markers[j].bin += 2 * ((m / 2) - markers[j].bin);
					} else {
						markers[j].bin = markers[0].bin * i;
						if (markers[j].bin > (m))
							markers[j].bin -=  2 * (markers[j].bin - (m));
						if (markers[j].bin < 0)
							markers[j].bin += -markers[j].bin;
					}
				}
				/* make sure we don't need to nudge things one way or the other */
				k = markers[j].bin;
				while (fft_channel[k] < fft_channel[k + 1]) {
					k++;
				}

				while (markers[j].bin != 0 &&
						fft_channel[markers[j].bin] < fft_channel[markers[j].bin - 1]) {
					markers[j].bin--;
				}

				if (fft_channel[k] > fft_channel[markers[j].bin])
					markers[j].bin = k;

				markers[j].x = (gfloat)X[markers[j].bin];
				markers[j].y = (gfloat)fft_channel[markers[j].bin];
			} else if (marker_type == MARKER_IMAGE) {
				/* keep DC, fundamental, and image
				 * num_active_channels always needs to be 2 for images */
				if (j == 0) {
					/* Fundamental */
					markers[j].bin = maxx[j];
				} else if (j == 1) {
					/* DC */
					markers[j].bin = m / 2;
				} else if (j == 2) {
					/* Image */
					markers[j].bin = m / 2 - (markers[0].bin - m/2);
				} else
					continue;
				markers[j].x = (gfloat)X[markers[j].bin];
				markers[j].y = (gfloat)fft_channel[markers[j].bin];

			}

			sprintf(text, "M%i: %2.2f dBFS @ %2.3f %sHz%c",
					j, markers[j].y, lo_freq + markers[j].x, adc_scale,
					j != MAX_MARKERS ? '\n' : '\0');

			if (j == 0) {
				gtk_text_buffer_set_text(tbuf, text, -1);
				gtk_text_buffer_get_iter_at_line(tbuf, &iter, 1);
			} else {
				gtk_text_buffer_insert(tbuf, &iter, text, -1);
			}
		}
		if (markers_copy) {
			memcpy(markers_copy, &markers, sizeof(struct marker_type) * MAX_MARKERS);
			markers_copy = NULL;
			G_UNLOCK(markers_copy);
		}
	} else {
		gtk_text_buffer_set_text(tbuf, "No markers active", 17);
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

static void fft_update_scale(bool force_update)
{
	double corr;
	int i;

	if (num_active_channels == 2) {
		corr =  adc_freq / 2;
	} else {
		corr = 0;
	}

	for (i = 0; i < num_samples_ploted; i++)
	{
		X[i] = (i * adc_freq / num_samples) - corr;
		fft_channel[i] = FLT_MAX;
	}

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enable_auto_scale)) && force_update == FALSE)
		return;
	gtk_databox_set_total_limits(GTK_DATABOX(databox), -5.0 - corr, adc_freq / 2.0 + 5.0, 0.0, -75.0);
	do_a_rescale_flag = 1;

}

#define OFF_MRK    "Markers Off"
#define PEAK_MRK   "Peak Markers"
#define FIX_MRK    "Fixed Markers"
#define SINGLE_MRK "Single Tone Markers"
#define DUAL_MRK   "Two Tone Markers"
#define IMAGE_MRK  "Image Markers"
#define ADD_MRK    "Add Marker"
#define REMOVE_MRK "Remove Marker"

static inline void marker_set(int i, char *buf, bool force)
{
	if (force)
		markers[i].active = TRUE;

	if (markers[i].graph) {
		gtk_databox_markers_set_label(GTK_DATABOX_MARKERS(markers[i].graph), 0,
				GTK_DATABOX_MARKERS_TEXT_N, buf, FALSE);
		gtk_databox_graph_set_hide(markers[i].graph, !markers[i].active);
	}
}

static void set_marker_labels(gchar *buf, enum marker_types type)
{
	char tmp[128];
	int i;

	if (!MAX_MARKERS)
		return;

	if ((buf && !strcmp(buf, PEAK_MRK)) || type == MARKER_PEAK) {
		marker_type = MARKER_PEAK;
		for (i = 0; i <= MAX_MARKERS; i++) {
			sprintf(tmp, "P%i", i);
			marker_set(i, tmp, FALSE);
		}
		return;
	} else if ((buf && !strcmp(buf, FIX_MRK)) || type == MARKER_FIXED) {
		marker_type = MARKER_FIXED;
		for (i = 0; i <= MAX_MARKERS; i++) {
			sprintf(tmp, "F%i", i);
			marker_set(i, tmp, FALSE);
		}
		return;
	} else if ((buf && !strcmp(buf, SINGLE_MRK)) || type == MARKER_ONE_TONE) {
		marker_type = MARKER_ONE_TONE;
		marker_set(0, "Fund", TRUE);
		marker_set(1, "DC", TRUE);
		for (i = 2; i < MAX_MARKERS; i++) {
			sprintf(tmp, "%iH", i);
			marker_set(i, tmp, FALSE);
		}
		return;
	} else if ((buf && !strcmp(buf, DUAL_MRK)) || type == MARKER_TWO_TONE) {
		marker_type = MARKER_TWO_TONE;
		return;
	} else if ((buf && !strcmp(buf, IMAGE_MRK)) || type == MARKER_IMAGE) {
		marker_type = MARKER_IMAGE;
		marker_set(0, "Fund", TRUE);
		marker_set(1, "DC", TRUE);
		marker_set(2, "Image", TRUE);
		for (i = 3; i <= MAX_MARKERS; i++) {
			markers[i].active = FALSE;
			if(markers[i].graph)
				gtk_databox_graph_set_hide(markers[i].graph, TRUE);
		}
		return;
	} else if (buf && !strcmp(buf, OFF_MRK)) {
		marker_type = MARKER_OFF;
		for (i = 0; i <= MAX_MARKERS; i++) {
			if (markers[i].graph)
				gtk_databox_graph_set_hide(markers[i].graph, TRUE);
		}
		return;
	} else if (buf && !strcmp(buf, REMOVE_MRK)) {
		for (i = MAX_MARKERS; i != 0; i--) {
			if (markers[i].active) {
				markers[i].active = FALSE;
				gtk_databox_graph_set_hide(markers[i].graph, TRUE);
				break;
			}
		}
		return;
	} else if (buf && !strcmp(buf, ADD_MRK)) {
		for (i = 0; i <= MAX_MARKERS; i++) {
			if (!markers[i].active) {
				markers[i].active = TRUE;
				gtk_databox_graph_set_hide(markers[i].graph, FALSE);
				break;
			}
		}
		return;
	}

	printf("unhandled event at %s : %s\n", __func__, buf);
}

static void marker_menu (gchar *buf)
{
	set_marker_labels(buf, MARKER_NULL);
}

static gint moved_fixed(GtkDatabox *box, GdkEventMotion *event, int mark)
{
	unsigned int max_size;

	if (num_active_channels == 2)
		max_size = num_samples;
	else
		max_size = num_samples / 2;

	while((gfloat)X[markers[mark].bin] < gtk_databox_pixel_to_value_x(box, event->x) &&
			markers[mark].bin < max_size)
		markers[mark].bin++;

	while ((gfloat)X[markers[mark].bin] > gtk_databox_pixel_to_value_x(box, event->x) &&
			markers[mark].bin > 0)
		markers[mark].bin--;

	return FALSE;
}

static gint marker_button (GtkDatabox *box, GdkEventButton *event)
{
	gfloat x, y, dist;
	GtkWidget *popupmenu, *menuitem;
	gfloat left, right, top, bottom;
	int i, fix = -1;
	bool full = TRUE, empty = TRUE;
	static gulong fixed_marker_hid = 0;

	/* FFT? */
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) != FFT_PLOT)
		return FALSE;

	/* Right button */
	if (event->button != 3)
		return FALSE;

	/* things are running? */
	if (!markers[0].graph)
		return FALSE;

	if (event->type == GDK_BUTTON_RELEASE) {
		if (fixed_marker_hid) {
			g_signal_handler_disconnect(GTK_DATABOX(databox), fixed_marker_hid);
			fixed_marker_hid = 0;
			return TRUE;
		}
		return FALSE;
	}

	x = gtk_databox_pixel_to_value_x(box, event->x);
	y = gtk_databox_pixel_to_value_y(box, event->y);
	gtk_databox_get_total_limits(GTK_DATABOX(box), &left, &right, &top, &bottom);

	for (i = 0 ; i <= MAX_MARKERS; i++) {
		if (marker_type == MARKER_FIXED) {
			/* sqrt of ((delta X / X range)^2 + (delta Y / Y range)^2 ) */
			dist = sqrtf(powf((x - markers[i].x) / (right - left), 2.0) +
					powf((y - markers[i].y) / (bottom - top), 2.0)) * 100;
			if (dist <= 2.0)
				fix = i;
		}
		if (!markers[i].active)
			full = FALSE;
		else if (empty)
			empty = FALSE;
	}

	if (fix != -1) {
		fixed_marker_hid = g_signal_connect(GTK_DATABOX(databox), "motion_notify_event",
				G_CALLBACK(moved_fixed), (gpointer) fix);
		return TRUE;
	}

	popupmenu = gtk_menu_new();

	i = 0;
	if (!full && !(marker_type == MARKER_OFF || marker_type == MARKER_IMAGE)) {
		menuitem = gtk_menu_item_new_with_label(ADD_MRK);
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
				GTK_SIGNAL_FUNC(marker_menu), (gpointer) ADD_MRK);
		gtk_widget_show(menuitem);
		i++;
	}

	if (!empty && !(marker_type == MARKER_OFF || marker_type == MARKER_IMAGE)) {
		menuitem = gtk_menu_item_new_with_label(REMOVE_MRK);
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
				GTK_SIGNAL_FUNC(marker_menu), (gpointer) REMOVE_MRK);
		gtk_widget_show(menuitem);
		i++;
	}

	if (!full || !empty) {
		menuitem = gtk_separator_menu_item_new();
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_widget_show(menuitem);
		i++;
	}

	menuitem = gtk_check_menu_item_new_with_label(PEAK_MRK);
	gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
			marker_type == MARKER_PEAK);
	gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
			GTK_SIGNAL_FUNC(marker_menu), (gpointer) PEAK_MRK);
	gtk_widget_show(menuitem);
	i++;

	menuitem = gtk_check_menu_item_new_with_label(FIX_MRK);
	gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
			marker_type == MARKER_FIXED);
	gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
			GTK_SIGNAL_FUNC(marker_menu), (gpointer) FIX_MRK);
	gtk_widget_show(menuitem);
	i++;

	menuitem = gtk_check_menu_item_new_with_label(SINGLE_MRK);
	gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
			marker_type == MARKER_ONE_TONE);
	gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
			GTK_SIGNAL_FUNC(marker_menu), (gpointer) SINGLE_MRK);
	gtk_widget_show(menuitem);
	i++;

/*
	menuitem = gtk_check_menu_item_new_with_label(DUAL_MRK);
	gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
			marker_type == MARKER_TWO_TONE);
	gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
			GTK_SIGNAL_FUNC(marker_menu), (gpointer) DUAL_MRK);
	gtk_widget_show(menuitem);
	i++;
*/

	if (num_active_channels == 2) {
		menuitem = gtk_check_menu_item_new_with_label(IMAGE_MRK);
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
				marker_type == MARKER_IMAGE);
		gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
		GTK_SIGNAL_FUNC(marker_menu), (gpointer) IMAGE_MRK);
		gtk_widget_show(menuitem);
		i++;
	}

	if (marker_type != MARKER_OFF) {
		menuitem = gtk_check_menu_item_new_with_label(OFF_MRK);
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
				marker_type == MARKER_OFF);
		gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
		GTK_SIGNAL_FUNC(marker_menu), (gpointer) OFF_MRK);
		gtk_widget_show(menuitem);
		i++;
	}

	gtk_menu_popup(GTK_MENU(popupmenu), NULL, NULL, NULL, NULL,
		event->button, event->time);

	if (marker_type == MARKER_FIXED)
		return TRUE;

	return FALSE;

}
static int fft_capture_setup(void)
{
	int i;
	char buf[10];

	num_samples = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(fft_size_widget)));

	data_buffer.size = num_samples * bytes_per_sample * num_active_channels;
	data_buffer.data = g_renew(int8_t, data_buffer.data, data_buffer.size);
	data_buffer.data_copy = NULL;
	markers_copy = NULL;

	num_samples_ploted = num_samples * num_active_channels / 2;

	X = g_renew(gfloat, X, num_samples_ploted);
	fft_channel = g_renew(gfloat, fft_channel, num_samples_ploted);

	fft_update_scale(FORCE_UPDATE);

	is_fft_mode = true;

	/* Compute FFT normalization and scaling offset */
	fft_corr = 20 * log10(2.0 / (1 << (channels[0].bits_used - 1)));

	/*
	 * Init markers
	 */
	if (MAX_MARKERS) {
		for (i = 0; i <= MAX_MARKERS; i++) {
			markers[i].x = 0.0f;
			markers[i].y = -100.0f;
			if (markers[i].graph)
				g_object_unref(markers[i].graph);

			markers[i].graph =  gtk_databox_markers_new(1, &markers[i].x, &markers[i].y, &color_marker,
					10, GTK_DATABOX_MARKERS_TRIANGLE);
			gtk_databox_graph_add(GTK_DATABOX(databox), markers[i].graph);

			sprintf(buf, "?%i", i);
			gtk_databox_markers_set_label(GTK_DATABOX_MARKERS(markers[i].graph), 0,
					GTK_DATABOX_MARKERS_TEXT_N, buf, FALSE);

			if (marker_type == MARKER_OFF)
				gtk_databox_graph_set_hide(markers[i].graph, TRUE);
			else
				gtk_databox_graph_set_hide(markers[i].graph, !markers[i].active);
		}
		if (marker_type != MARKER_OFF)
			set_marker_labels(NULL, marker_type);
	}

	fft_graph = gtk_databox_lines_new(num_samples_ploted, X, fft_channel, &color_graph[0], 1);
	gtk_databox_graph_add(GTK_DATABOX(databox), fft_graph);

	return 0;
}

static void fft_capture_start(void)
{
	capture_function = g_idle_add((GSourceFunc) fft_capture_func, databox);
}

static void detach_plugin(GtkToolButton *btn, gpointer data);

static GtkWidget* plugin_tab_add_detach_btn(GtkWidget *page, struct detachable_plugin *d_plugin)
{
	GtkWidget *tab_box;
	GtkWidget *tab_label;
	GtkWidget *tab_toolbar;
	GtkWidget *tab_detach_btn;
	const struct osc_plugin *plugin = d_plugin->plugin;
	const char *plugin_name = plugin->name;

	tab_box = gtk_hbox_new(FALSE, 0);
	tab_label = gtk_label_new(plugin_name);
	tab_toolbar = gtk_toolbar_new();
	tab_detach_btn = (GtkWidget *)gtk_tool_button_new_from_stock("gtk-disconnect");

	gtk_widget_set_size_request(tab_detach_btn, 25, 5);

	gtk_toolbar_insert(GTK_TOOLBAR(tab_toolbar), GTK_TOOL_ITEM(tab_detach_btn), 0);
	gtk_container_add(GTK_CONTAINER(tab_box), tab_label);
	gtk_container_add(GTK_CONTAINER(tab_box), tab_toolbar);

	gtk_widget_show_all(tab_box);

	gtk_notebook_set_tab_label(GTK_NOTEBOOK(notebook), page, tab_box);
	g_signal_connect(tab_detach_btn, "clicked",
		G_CALLBACK(detach_plugin), (gpointer)d_plugin);

	return tab_detach_btn;
}

static void plugin_make_detachable(struct detachable_plugin *d_plugin)
{
	GtkWidget *page = NULL;
	int num_pages = 0;

	num_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
	page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), num_pages - 1);

	d_plugin->detached_state = FALSE;
	d_plugin->detach_attach_button = plugin_tab_add_detach_btn(page, d_plugin);
}

static void attach_plugin(GtkToolButton *btn, gpointer data)
{
	GtkWidget *window;
	GtkWidget *plugin_page;
	GtkWidget *detach_btn;
	struct detachable_plugin *d_plugin = (struct detachable_plugin *)data;
	const struct osc_plugin *plugin = d_plugin->plugin;
	gint plugin_page_index;

	window = (GtkWidget *)gtk_widget_get_toplevel(GTK_WIDGET(btn));

	GtkWidget *hbox = NULL;
	GList *hbox_elems = NULL;
	GList *first = NULL;

	hbox = gtk_bin_get_child(GTK_BIN(window));
	hbox_elems = gtk_container_get_children(GTK_CONTAINER(hbox));
	first = g_list_first(hbox_elems);
	plugin_page = first->data;
	gtk_container_remove(GTK_CONTAINER(hbox), plugin_page);
	gtk_widget_destroy(window);
	plugin_page_index = gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
		plugin_page, NULL);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), plugin_page_index);
	detach_btn = plugin_tab_add_detach_btn(plugin_page, d_plugin);

	if (plugin->update_active_page)
		plugin->update_active_page(plugin_page_index, FALSE);
	d_plugin->detached_state = FALSE;
	d_plugin->detach_attach_button = detach_btn;
}

static GtkWidget * extract_label_from_box(GtkWidget *box)
{
	GList *children = NULL;
	GList *first = NULL;
	GtkWidget *label;

	children = gtk_container_get_children(GTK_CONTAINER(box));
	first = g_list_first(children);
	label = first->data;
	g_list_free(children);

	return label;
}

static void detach_plugin(GtkToolButton *btn, gpointer data)
{
	struct detachable_plugin *d_plugin = (struct detachable_plugin *)data;
	const struct osc_plugin *plugin = d_plugin->plugin;
	const char *plugin_name = plugin->name;
	const char *page_name = NULL;
	GtkWidget *page = NULL;
	GtkWidget *box;
	GtkWidget *label;
	int num_pages;
	int i;

	/* Find the page that belongs to a plugin, using the plugin name */
	num_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
	for (i = 0; i < num_pages; i++) {
		page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i);
		box = gtk_notebook_get_tab_label(GTK_NOTEBOOK(notebook), page);
		if (GTK_IS_BOX(box))
			label = extract_label_from_box(box);
		else
			label = box;
		page_name = gtk_label_get_text(GTK_LABEL(label));
		if (!strcmp(page_name, plugin_name))
			break;
	}
	if (i == num_pages) {
		printf("Could not find %s plugin in the notebook\n", plugin_name);
		return;
	}

	GtkWidget *window;
	GtkWidget *hbox;
	GtkWidget *vbox;
	GtkWidget *vbox_empty;
	GtkWidget *toolbar;
	GtkWidget *attach_button;

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_deletable(GTK_WINDOW(window), FALSE);
	hbox = gtk_hbox_new(FALSE, 0);
	vbox = gtk_vbox_new(FALSE, 0);
	vbox_empty = gtk_vbox_new(FALSE, 0);
	toolbar = gtk_toolbar_new();
	attach_button = (GtkWidget *)gtk_tool_button_new_from_stock("gtk-connect");
	gtk_widget_set_size_request(attach_button, 25, 5);

	gtk_window_set_title(GTK_WINDOW(window), page_name);
	gtk_widget_reparent(page, hbox);
	gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), vbox_empty, TRUE, TRUE, 0);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(attach_button), 0);
	gtk_container_add(GTK_CONTAINER(window), hbox);

	g_signal_connect(attach_button, "clicked",
			G_CALLBACK(attach_plugin), (gpointer)d_plugin);

	if (plugin->update_active_page)
		plugin->update_active_page(-1, TRUE);
	d_plugin->detached_state = TRUE;
	d_plugin->detach_attach_button = attach_button;

	gtk_widget_show(window);
	gtk_widget_show(hbox);
	gtk_widget_show_all(vbox);
}

/*
 * helper functions for plugins which want to look at data
 * Note that multiosc application will implement these functions differently.
 */

const void * plugin_get_device_by_reference(const char * device_name)
{

	if(!current_device)
		return NULL;

	if(!strcmp(current_device, device_name))
		return current_device;

	return NULL;
}

int plugin_data_capture_size(const char *device)
{
	if ((device && !strcmp(current_device, device)) || !device)
		return data_buffer.size;

	return 0;
}

int plugin_data_capture_num_active_channels(const char *device)
{
	if (!strcmp(current_device, device))
		return num_active_channels;

	return 0;
}

int plugin_data_capture_bytes_per_sample(const char *device)
{
	if (!strcmp(current_device, device))
		return bytes_per_sample;

	return 0;
}

enum marker_types plugin_get_marker_type(const char *device)
{
	if (is_fft_mode && !strcmp(current_device, device))
		return marker_type;

	return MARKER_NULL;
}

void plugin_set_marker_type(const char *device, enum marker_types type)
{
	if (!strcmp(current_device, device))
		marker_type = type;
}

gdouble plugin_get_fft_avg(const char *device)
{
	if ((device && !strcmp(current_device, device)) || !device)
		return gtk_spin_button_get_value(GTK_SPIN_BUTTON(fft_avg_widget));

	return 0;
}

int plugin_data_capture(const char *device, void **buf, gfloat ***cooked_data,
			struct marker_type **markers_cp)
{
	int i, j;
	bool new = FALSE;

	/* if there isn't anything to send, clear everything */
	if (data_buffer.size == 0 || device == NULL) {
		if (buf && *buf) {
			g_free(*buf);
			*buf = NULL;
		}
		if (cooked_data && *cooked_data) {
			for (i = 0; i < num_active_channels; i++)
				g_free((*cooked_data)[i]);
			g_free(*cooked_data);
			*cooked_data = NULL;
		}
		if (markers_cp && *markers_cp) {
			g_free(*markers_cp);
			*markers_cp = NULL;
		}
		return -ENXIO;
	}

	if (strcmp(current_device, device))
		return -ENXIO;

	if (buf) {
		/* One consumer at a time */
		if (data_buffer.data_copy)
			return -EBUSY;

		/* make sure space is allocated */
		if (*buf)
			*buf = g_renew(int8_t, *buf, data_buffer.size);
		else
			*buf = g_new(int8_t, data_buffer.size);

		if (!*buf)
			goto capture_malloc_fail;

		/* where to put the copy */
		data_buffer.data_copy = *buf;

		/* Wait til the buffer is full */
		G_LOCK(buffer_full);

		/* if the lock is released, but the copy is still there
		 * that's because someone else broke the lock
		 */
		if (data_buffer.data_copy) {
			data_buffer.data_copy = NULL;
			return -EINTR;
		}

		if (cooked_data) {
			/* make sure space is allocated */
			if (*cooked_data) {
				*cooked_data = g_renew(gfloat *, *cooked_data,
							num_active_channels);
				new = FALSE;
			} else {
				*cooked_data = g_new(gfloat *, num_active_channels);
				new = TRUE;
			}

			if (!*cooked_data)
				goto capture_malloc_fail;

			for (i = 0; i < num_active_channels; i++) {
				if (new)
					(*cooked_data)[i] = g_new(gfloat,
							data_buffer.size / bytes_per_sample);
				else
					(*cooked_data)[i] = g_renew(gfloat,
							(*cooked_data)[i],
							data_buffer.size / bytes_per_sample);

				if (!(*cooked_data)[i])
					goto capture_malloc_fail;

				for (j = 0; j < data_buffer.size / bytes_per_sample; j++)
					(*cooked_data)[i][j] = 0.0f;
			}

			/* Now that we have the space, process it */
			 demux_data_stream(*buf, *cooked_data,
					data_buffer.size / 4, 0, data_buffer.size / 4,
					channels, num_active_channels);
		}
	}

	if (markers_cp) {
		if (!is_fft_mode) {
			if (*markers_cp) {
				g_free(*markers_cp);
				*markers_cp = NULL;
			}
			return 0;
		}

		/* One consumer at a time */
		if (markers_copy)
			return -EBUSY;

		/* make sure space is allocated */
		if (*markers_cp)
			*markers_cp = g_renew(struct marker_type, *markers_cp, MAX_MARKERS + 2);
		else
			*markers_cp = g_new(struct marker_type, MAX_MARKERS + 2);

		if (!*markers_cp)
			goto capture_malloc_fail;

		/* where to put the copy */
		markers_copy = *markers_cp;

		/* Wait til the copy is complete */
		G_LOCK(markers_copy);

		/* if the lock is released, but the copy is still there
		 * that's because someone else broke the lock
		 */
		if (markers_copy) {
			markers_copy = NULL;
			return -EINTR;
		}
	}
	return 0;


capture_malloc_fail:
	printf("%s:%s malloc failed\n", __FILE__, __func__);
	return -ENOMEM;
}

static int time_capture_setup(void)
{
	gboolean is_constellation;
	unsigned int i, j;
	static int prev_num_active_ch = 0;

	is_constellation = gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == XY_PLOT;

	gtk_databox_graph_remove_all(GTK_DATABOX(databox));

	num_samples = gtk_spin_button_get_value(GTK_SPIN_BUTTON(sample_count_widget));
	data_buffer.size = num_samples * bytes_per_sample;

	data_buffer.data = g_renew(int8_t, data_buffer.data, data_buffer.size);
	data_buffer.data_copy = NULL;
	markers_copy = NULL;

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
		gtk_databox_set_total_limits(GTK_DATABOX(databox), -1000.0, 1000.0, 1000.0, -1000.0);
	else
		gtk_databox_set_total_limits(GTK_DATABOX(databox), 0.0, num_samples, 1000.0, -1000.0);

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
	char buf[10];

	if (gtk_toggle_tool_button_get_active(btn)) {
		gtk_databox_graph_remove_all(GTK_DATABOX(databox));

		G_TRYLOCK(buffer_full);
		G_TRYLOCK(markers_copy);

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

		if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == FFT_PLOT) {
			sprintf(buf, "%sHz", adc_scale);
			gtk_label_set_text(GTK_LABEL(hor_scale), buf);
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
			buffer_fd = buffer_open(num_samples, O_NONBLOCK);
			if (buffer_fd < 0)
				goto play_err;
		}

		add_grid();
		gtk_widget_queue_draw(GTK_WIDGET(databox));
		frame_counter = 0;

		if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == FFT_PLOT)
			fft_capture_start();
		else
			time_capture_start();

	} else {
		G_TRYLOCK(buffer_full);
		G_UNLOCK(buffer_full);
		G_TRYLOCK(markers_copy);
		G_UNLOCK(markers_copy);

		if (capture_function > 0) {
			g_source_remove(capture_function);
			capture_function = 0;
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

static void enable_auto_scale_cb(GtkToggleButton *btn, gpointer data)
{
	if (gtk_toggle_button_get_active(btn))
		do_a_rescale_flag = 1;
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

void time_interval_adjust(void)
{
	GtkAdjustment *adj;
	gdouble min_time;
	gdouble max_time;

	adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(time_interval_widget));

	min_time = (SAMPLE_COUNT_MIN_VALUE / adc_freq_raw) * 1000000;
	max_time = (SAMPLE_COUNT_MAX_VALUE / adc_freq_raw) * 1000000;

	gtk_adjustment_set_lower(adj, min_time);
	gtk_adjustment_set_upper(adj, max_time);
}

void rx_update_labels(void)
{
	char buf[20];

	adc_freq = read_sampling_frequency();
	adc_freq_raw = adc_freq;
	time_interval_adjust();
	if (adc_freq >= 1000000) {
		sprintf(adc_scale, "M");
		adc_freq /= 1000000;
	} else if(adc_freq >= 1000) {
		sprintf(adc_scale, "k");
		adc_freq /= 1000;
	} else if(adc_freq >= 0) {
		sprintf(adc_scale, " ");
	} else {
		sprintf(adc_scale, "?");
		adc_freq = 0;
	}

	snprintf(buf, sizeof(buf), "%.3f %sSPS", adc_freq, adc_scale);

	gtk_label_set_text(GTK_LABEL(adc_freq_label), buf);

	if (!set_dev_paths("adf4351-rx-lpc"))
		read_devattr_double("out_altvoltage0_frequency", &lo_freq);
	else if (!set_dev_paths("ad9361-phy"));
		read_devattr_double("out_altvoltage0_RX_LO_frequency", &lo_freq);

	if (lo_freq) {
		lo_freq /= 1000000.0;
		snprintf(buf, sizeof(buf), "%.4f Mhz", lo_freq);
		gtk_label_set_text(GTK_LABEL(rx_lo_freq_label), buf);
	}

	if (is_fft_mode) {
		/*
		 * In FFT mode we need to scale the X-axis according to the selected
		 * sampling frequency.
		 */
		fft_update_scale(NORMAL_UPDATE);
	}
}

static void zoom_fit(GtkButton *btn, gpointer data)
{
	rescale_databox(GTK_DATABOX(data), 0.05);
}

static void zoom_in(GtkButton *btn, gpointer data)
{
	bool fixed_aspect = gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == XY_PLOT;
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
	bool fixed_aspect = gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == XY_PLOT;
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
	struct detachable_plugin *d_plugin;
	struct osc_plugin *plugin;
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

	if (!plugin->identify() && !force_plugin(plugin->name)) {
		dlclose(lib);
		return;
	}

	plugin->handle = lib;
	plugin_list = g_slist_append (plugin_list, (gpointer)plugin);
	plugin->init(notebook);

	d_plugin = malloc(sizeof(struct detachable_plugin));
	d_plugin->plugin = plugin;
	dplugin_list = g_slist_append (dplugin_list, (gpointer)d_plugin);

	plugin_make_detachable(d_plugin);

	printf("Loaded plugin: %s\n", plugin->name);
}

static void close_plugins(void)
{
	GSList *node;
	struct osc_plugin *plugin = NULL;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin) {
			printf("Closing plugin: %s\n", plugin->name);
			dlclose(plugin->handle);
		}
	}
}

bool plugin_installed(const char *name)
{
	GSList *node;
	struct osc_plugin *plugin = NULL;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin && !strcmp(plugin->name, name))
			return true;
	}

	return false;
}

void * plugin_dlsym(const char *name, const char *symbol)
{
	GSList *node;
	struct osc_plugin *plugin = NULL;
	void *fcn;
	char *buf;
	Dl_info info;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin && !strcmp(plugin->name, name)) {
			dlerror();
			fcn = dlsym(plugin->handle, symbol);
			buf = dlerror();
			if (buf) {
				fprintf(stderr, "%s:%s(): found plugin %s, error looking up %s\n"
						"\t%s\n", __FILE__, __func__, name, symbol, buf);
				if (dladdr(__builtin_return_address(0), &info))
					fprintf(stderr, "\tcalled from %s:%s()\n", info.dli_fname, info.dli_sname);
			}
			return fcn;
		}
	}

	fprintf(stderr, "%s:%s : No plugin with matching name %s\n", __FILE__, __func__, name);
	if (dladdr(__builtin_return_address(0), &info))
		fprintf(stderr, "\tcalled from %s:%s()\n", info.dli_fname, info.dli_sname);

	return NULL;
}

bool str_endswith(const char *str, const char *needle)
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

bool is_input_device(const char *device)
{
	struct iio_channel_info *channels = NULL;
	unsigned int num_channels;
	bool is_input = false;
	int ret;
	int i;

	set_dev_paths(device);

	ret = build_channel_array(dev_name_dir(), &channels, &num_channels);
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

struct plugin_check_fct {
	void *fct_pointer;
	char *dev_name;
};

void add_ch_setup_check_fct(char *device_name, void *fp)
{
	int n;

	setup_check_functions = (struct plugin_check_fct *)g_renew(struct plugin_check_fct, setup_check_functions, ++num_check_fcts);
	n = num_check_fcts - 1;
	setup_check_functions[n].fct_pointer = fp;
	setup_check_functions[n].dev_name = (char *)g_new(char, strlen(device_name) + 1);
	strcpy(setup_check_functions[n].dev_name, device_name);
}

void *find_setup_check_fct_by_devname(const char *dev_name)
{
	int i;

	if (!dev_name)
		return NULL;

	for (i = 0; i < num_check_fcts; i++)
		if (strcmp(dev_name, setup_check_functions[i].dev_name) == 0)
			return setup_check_functions[i].fct_pointer;

	return NULL;
}

void free_setup_check_fct_list(void)
{
	int i;

	for (i = 0; i < num_check_fcts; i++) {
		g_free(setup_check_functions[i].dev_name);
	}
	g_free(setup_check_functions);
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
	plugin_setup_validation_fct = find_setup_check_fct_by_devname(current_device);

	ret = build_channel_array(dev_name_dir(), &channels, &num_channels);
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

	snprintf(buf, sizeof(buf), "%s/scan_elements/%s_en", dev_name_dir(), channel->name);
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
	if (deactivate_capture_btn_flag == 1)
		return FALSE;

	if (g_value_get_boolean(source_value))
		g_value_set_static_string(target_value, "gtk-stop");
	else
		g_value_set_static_string(target_value, "gtk-media-play");

	return TRUE;
}

static gboolean domain_is_fft(GBinding *binding,
        const GValue *source_value, GValue *target_value, gpointer user_data)
{
	g_value_set_boolean(target_value, g_value_get_int(source_value) == FFT_PLOT);
	return TRUE;
}

static gboolean domain_is_time(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer user_data)
{
	g_value_set_boolean(target_value, g_value_get_int(source_value) != FFT_PLOT);
	return TRUE;
}


static gboolean check_valid_setup()
{
	GtkTreeIter iter;
	int j = 0;
	gboolean loop, enabled;
	char warning_text[100];
	char *ch_names[2];

	loop = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(channel_list_store), &iter);
	while (loop) {
		gtk_tree_model_get(GTK_TREE_MODEL(channel_list_store), &iter, 1, &enabled, -1);
		if (enabled)
			j++;
		loop = gtk_tree_model_iter_next(GTK_TREE_MODEL (channel_list_store), &iter);
	}

	/* Additional validation rules provided by the plugin of the device */
	if (plugin_setup_validation_fct)
		if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == FFT_PLOT ||
			gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == XY_PLOT)
			if (j == 2)
				if(!(*plugin_setup_validation_fct)(channels, num_channels, ch_names)) {
					snprintf(warning_text, sizeof(warning_text),
						"Combination bewteen %s and %s is invalid", ch_names[0], ch_names[1]);
					gtk_widget_set_tooltip_text(capture_button, warning_text);
					goto capture_button_err;
				}


	/* Basic validation rules */
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == FFT_PLOT) {
		if (j != 2 && j != 1) {
			gtk_widget_set_tooltip_text(capture_button, "FFT needs 2 channels or less");
			goto capture_button_err;
		}
	} else if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == XY_PLOT) {
		if (j != 2) {
			gtk_widget_set_tooltip_text(capture_button, "Constellation needs 2 channels");
			goto capture_button_err;
		}
	} else if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == TIME_PLOT){
		if (j == 0) {
			gtk_widget_set_tooltip_text(capture_button, "Time Domain needs at least one channel");
			goto capture_button_err;
		}
	} else {
		gtk_widget_set_tooltip_text(capture_button, "Unknown capture type");
		goto capture_button_err;
	}

	g_object_set(capture_button, "stock-id", "gtk-media-play", NULL);
	gtk_widget_set_tooltip_text(capture_button, "Capture / Stop");
	if (!capture_button_hid) {
		capture_button_hid = g_signal_connect(capture_button, "toggled",
		G_CALLBACK(capture_button_clicked), NULL);
		deactivate_capture_btn_flag = 0;
	}

	return false;

capture_button_err:
	g_object_set(capture_button, "stock-id", "gtk-dialog-warning", NULL);
	if (capture_button_hid) {
		g_signal_handler_disconnect(capture_button, capture_button_hid);
		deactivate_capture_btn_flag = 1;
	}
	capture_button_hid = 0;

	return false;
}

gboolean time_to_samples(GBinding *binding, const GValue *source_val,
	GValue *target_val, gpointer data)
{
	gdouble time;
	gdouble samples;

	time = g_value_get_double(source_val);
	/* Microseconds to seconds */
	time /= 1000000.0;
	samples = time * adc_freq_raw;
	if (samples < 10)
		samples = 10;
	else if (samples > 1000000)
		samples = 1000000;
	g_value_set_double(target_val, samples);

	return TRUE;
}

gboolean samples_to_time(GBinding *binding, const GValue *source_val,
	GValue *target_val, gpointer data)
{
	gdouble time;
	gdouble samples;

	samples = g_value_get_double(source_val);
	time = samples / adc_freq_raw;
	/* Seconds to microseconds */
	time *= 1000000;
	g_value_set_double(target_val, time);

	return TRUE;
}

static void plugin_state_ini_save(gpointer data, gpointer user_data)
{
	struct detachable_plugin *p = (struct detachable_plugin *)data;
	FILE *fp = (FILE *)user_data;

	fprintf(fp, "plugin.%s.detached=%d\n", p->plugin->name, p->detached_state);
}

void capture_profile_save(const char *filename)
{
	FILE *inifp;
	gchar *crt_dev_name;
	gchar *ch_name;
	GtkTreeIter iter;
	gboolean loop, enabled;
	int tmp_int;
	float tmp_float;
	gchar *tmp_string;

	inifp = fopen(filename, "w");
	if (!inifp)
		return;

	fprintf(inifp, "[%s]\n", CAPTURE_CONF);
	crt_dev_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(device_list_widget));
	fprintf(inifp, "device_name=%s\n", crt_dev_name);
	g_free(crt_dev_name);
	loop = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(channel_list_store), &iter);
	while (loop) {
		gtk_tree_model_get(GTK_TREE_MODEL(channel_list_store), &iter, 0, &ch_name, 1, &enabled, -1);
		fprintf(inifp, "%s.enabled=%d\n", ch_name, enabled);
		loop = gtk_tree_model_iter_next(GTK_TREE_MODEL(channel_list_store), &iter);
		g_free(ch_name);
	}

	fprintf(inifp, "domain=");
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == FFT_PLOT)
		fprintf(inifp, "%s\n", "fft");
	else if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == XY_PLOT)
		fprintf(inifp, "%s\n", "constellation");
	else if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot_domain)) == TIME_PLOT)
		fprintf(inifp, "%s\n", "time");

	tmp_int = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(sample_count_widget));
	fprintf(inifp, "sample_count=%d\n", tmp_int);

	tmp_int = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(fft_size_widget)));
	fprintf(inifp, "fft_size=%d\n", tmp_int);

	tmp_int = gtk_spin_button_get_value(GTK_SPIN_BUTTON(fft_avg_widget));
	fprintf(inifp, "fft_avg=%d\n", tmp_int);

	tmp_float = gtk_spin_button_get_value(GTK_SPIN_BUTTON(fft_pwr_offset_widget));
	fprintf(inifp, "fft_pwr_offset=%f\n", tmp_float);

	tmp_string = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(plot_type));
	fprintf(inifp, "graph_type=%s\n", tmp_string);
	g_free(tmp_string);

	tmp_int = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(show_grid));
	fprintf(inifp, "show_grid=%d\n", tmp_int);

	tmp_int = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enable_auto_scale));
	fprintf(inifp, "enable_auto_scale=%d\n", tmp_int);

	gfloat left, right, top, bottom;
	gtk_databox_get_visible_limits(GTK_DATABOX(databox), &left, &right, &top, &bottom);
	fprintf(inifp, "x_axis_min=%f\n", left);
	fprintf(inifp, "x_axis_max=%f\n", right);
	fprintf(inifp, "y_axis_min=%f\n", bottom);
	fprintf(inifp, "y_axis_max=%f\n", top);

	if (marker_type == MARKER_OFF)
		fprintf(inifp, "marker_type = %s\n", OFF_MRK);
	else if (marker_type == MARKER_PEAK)
		fprintf(inifp, "marker_type = %s\n", PEAK_MRK);
	else if (marker_type == MARKER_FIXED)
		fprintf(inifp, "marker_type = %s\n", FIX_MRK);
	else if (marker_type == MARKER_ONE_TONE)
		fprintf(inifp, "marker_type = %s\n", SINGLE_MRK);
	else if (marker_type == MARKER_TWO_TONE)
		fprintf(inifp, "marker_type = %s\n", DUAL_MRK);
	else if (marker_type == MARKER_IMAGE)
		fprintf(inifp, "marker_type = %s\n", IMAGE_MRK);

	for (tmp_int = 0; tmp_int <= MAX_MARKERS; tmp_int++) {
		if (markers[tmp_int].active)
			fprintf(inifp, "marker.%i = %i\n", tmp_int, markers[tmp_int].bin);
	}

	fprintf(inifp, "capture_started = %d\n", (capture_function) ? 1 : 0);

	g_slist_foreach(dplugin_list, plugin_state_ini_save, inifp);

	fclose(inifp);
}

static int comboboxtext_set_active_by_string(GtkComboBox *combo_box, const char *name)
{
	GtkTreeModel *model = gtk_combo_box_get_model(combo_box);
	GtkTreeIter iter;
	gboolean has_iter;
	char *item;

	has_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (has_iter) {
		gtk_tree_model_get(model, &iter, 0, &item, -1);
		if (strcmp(name, item) == 0) {
			g_free(item);
			gtk_combo_box_set_active_iter(combo_box, &iter);
			return 1;
		}
		has_iter = gtk_tree_model_iter_next(model, &iter);
	}

	return 0;
}

static int channel_soft_toggle(const char *ch_name, gboolean enable)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	gchar *path_string;
	gboolean loop;
	char *item;

	loop = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(channel_list_store), &iter);
	while (loop) {
		gtk_tree_model_get(GTK_TREE_MODEL(channel_list_store), &iter, 0, &item, -1);
		if (!strcmp(ch_name, item)) {
			g_free(item);
			gtk_list_store_set(GTK_LIST_STORE(channel_list_store), &iter, 1, !enable, -1);
			path = gtk_tree_model_get_path(GTK_TREE_MODEL(channel_list_store), &iter);
			path_string = gtk_tree_path_to_string(path);
			channel_toggled(NULL, path_string, channel_list_store);
			g_free(path_string);
			return 1;
		}
		loop = gtk_tree_model_iter_next(GTK_TREE_MODEL(channel_list_store), &iter);
	}

	return 0;
}

static int count_char_in_string(char c, const char *s)
{
	int i;

	for (i = 0; s[i];)
		if (s[i] == c)
			i++;
		else
			s++;

	return i;
}

static gint plugin_names_cmp(gconstpointer a, gconstpointer b)
{
	struct detachable_plugin *p = (struct detachable_plugin *)a;
	char *key = (char *)b;

	return strcmp(p->plugin->name, key);
}

static void plugin_restore_ini_state(char *plugin_name, gboolean detached)
{
	struct detachable_plugin *dplugin;
	GSList *found_plugin = NULL;
	GtkWidget *button;

	found_plugin = g_slist_find_custom(dplugin_list,
		(gconstpointer)plugin_name, plugin_names_cmp);
	if (found_plugin == NULL) {
		printf("Plugin: %s not currently loaded, skipping\n", plugin_name);
		return;
	}

	dplugin = found_plugin->data;
	button = dplugin->detach_attach_button;
	if ((dplugin->detached_state) ^ (detached))
		g_signal_emit_by_name(button, "clicked", dplugin);
}

static gfloat plot_left, plot_right, plot_top, plot_bottom;
static int read_scale_params;

#define MATCH_NAME(s) (strcmp(name, s) == 0)

/*Handler should return nonzero on success, zero on error. */
int capture_profile_handler(const char* name, const char *value)
{
	int elem_type;
	gchar **elems = NULL, **min_max = NULL;
	gfloat max_f, min_f;
	gchar *ch_name;
	int ret = 1, i;
	FILE *fd;

	elem_type = count_char_in_string('.', name);
	switch (elem_type) {
		case 0:
			if (MATCH_NAME("capture_started")) {
				check_valid_setup();
				gtk_databox_graph_remove_all(GTK_DATABOX(databox));
				add_grid();
				if (read_scale_params == 4) {
					gtk_databox_set_total_limits(GTK_DATABOX(databox),
							plot_left, plot_right, plot_top, plot_bottom);
					read_scale_params = 0;
				}
				gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(marker_label)), "", -1);
				gtk_label_set_text(GTK_LABEL(hor_scale), "");
				gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(capture_button), atoi(value));
			} else if (MATCH_NAME("device_name")) {
				ret = comboboxtext_set_active_by_string(GTK_COMBO_BOX(device_list_widget), value);
				if (ret == 0)
					printf("found invalid device name in .ini file\n");
			} else if (MATCH_NAME("domain")) {
				if (!strcmp(value, "time"))
					gtk_combo_box_set_active(GTK_COMBO_BOX(plot_domain), TIME_PLOT);
				else if (!strcmp(value, "fft"))
					gtk_combo_box_set_active(GTK_COMBO_BOX(plot_domain), FFT_PLOT);
				else if (!strcmp(value, "constellation"))
					gtk_combo_box_set_active(GTK_COMBO_BOX(plot_domain), XY_PLOT);
			} else if (MATCH_NAME("sample_count")) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(sample_count_widget), atoi(value));
			} else if (MATCH_NAME("fft_size")) {
				ret = comboboxtext_set_active_by_string(GTK_COMBO_BOX(fft_size_widget), value);
				if (ret == 0)
					printf("found invalid fft size in .ini file\n");
			} else if (MATCH_NAME("fft_avg")) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(fft_avg_widget), atoi(value));
			} else if (MATCH_NAME("fft_pwr_offset")) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(fft_pwr_offset_widget), atof(value));
			} else if (MATCH_NAME("graph_type")) {
				ret = comboboxtext_set_active_by_string(GTK_COMBO_BOX(plot_type), value);
				if (ret == 0)
					printf("found invalid graph type in .ini file\n");
			} else if (MATCH_NAME("show_grid")) {
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(show_grid), atoi(value));
			} else if (MATCH_NAME("enable_auto_scale")) {
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enable_auto_scale), atoi(value));
			} else if (MATCH_NAME("x_axis_min")) {
				plot_left = atof(value);
				read_scale_params++;
			} else if (MATCH_NAME("x_axis_max")) {
				plot_right = atof(value);
				read_scale_params++;
			} else if (MATCH_NAME("y_axis_min")) {
				plot_bottom = atof(value);
				read_scale_params++;
			} else if (MATCH_NAME("y_axis_max")) {
				plot_top = atof(value);
				read_scale_params++;
			} else if (MATCH_NAME("marker_type")) {
				set_marker_labels((gchar *)value, MARKER_NULL);
				for (i = 0; i <= MAX_MARKERS; i++)
					markers[i].active = FALSE;
			} else if (MATCH_NAME("save_png")) {
				save_as(value, SAVE_PNG);
			} else if (MATCH_NAME("cycle")) {
				i = 0;
				while (gtk_events_pending() && i < atoi(value)) {
					i++;
					gtk_main_iteration();
				}
			} else if (MATCH_NAME("save_markers")) {
				fd = fopen(value, "a");
				if (!fd)
					return 0;

				fprintf (fd, "%f", lo_freq);

				for (i = 0; i <= MAX_MARKERS; i++) {
					if (markers[i].active) {
						fprintf(fd ,", %f, %f", markers[i].x, markers[i].y);
					}
				}
				fprintf (fd, "\n");
				fclose (fd);
			} else if (MATCH_NAME("fru_connect")) {
				if (value) {
					if (atoi(value) == 1) {
						i = fru_connect();
						if (i == GTK_RESPONSE_OK)
							ret = 1;
						else
							ret = 0;
					} else
						ret = 0;
				}
			} else if (MATCH_NAME("quit")) {
				return 0;
			} else
				goto unhandled;
			break;
		case 1:
			elems = g_strsplit(name, ".", 2);
			ch_name = elems[0];
			if (!strcmp(elems[1], "enabled")) {
				ret = channel_soft_toggle(ch_name, atoi(value));
				if (ret == 0)
					goto unhandled;
			} else if (!strcmp(elems[0], "marker")) {
				i = atoi(elems[1]);
				if (i >= 0 && i <= MAX_MARKERS) {
					markers[i].bin = atoi(value);
					markers[i].active = TRUE;
				}
			} else if (!strcmp(elems[0], "test")) {
				if (!strcmp(elems[1], "message")) {
					create_blocking_popup(GTK_MESSAGE_QUESTION, GTK_BUTTONS_CLOSE,
							"Profile status", value);
				} else
					goto unhandled;
			} else
				goto unhandled;
			break;
		case 2:
			elems = g_strsplit(name, ".", 3);
			if (!strcmp(elems[0], "plugin")) {
				if (!strcmp(elems[2], "detached"))
					plugin_restore_ini_state(elems[1], atoi(value));
				else goto unhandled;
			} else if (!strcmp(elems[0], "test")) {
				if (!strcmp(elems[1], "marker")) {
					min_max = g_strsplit(value, " ", 0);
					min_f = atof(min_max[0]);
					max_f = atof(min_max[1]);
					i = atoi(elems[2]);
					if (markers[i].active &&
							markers[i].y >= min_f &&
							markers[i].y <= max_f) {
						ret = 1;
					} else {
						ret = 0;
						printf("%smarker %i failed : level %f\n",
								markers[i].active ? "" : "In",
								i, markers[i].y);
					}
					g_strfreev(min_max);
				} else
					goto unhandled;
			} else {
				goto unhandled;
			}
			break;
		default:
unhandled:
			printf("Unhandled tokens in ini file,\n"
					"\tSection %s\n\tAtttribute : %s\n\tValue: %s\n",
					CAPTURE_CONF, name, value);
			ret = 0;
			break;
	}

	if (elems != NULL)
		g_strfreev(elems);

	return ret;
}

#define DEFAULT_PROFILE_NAME ".osc_profile.ini"
void application_quit (void)
{
	const char *home_dir = getenv("HOME");
	char buf[1024];

	/* Before we shut down, let's save the profile */
	sprintf(buf, "%s/%s", home_dir, DEFAULT_PROFILE_NAME);
	capture_profile_save(buf);
	save_all_plugins(buf, NULL);

	if (capture_function > 0) {
		g_source_remove(capture_function);
		capture_function = 0;
		G_TRYLOCK(buffer_full);
		G_UNLOCK(buffer_full);
		G_TRYLOCK(markers_copy);
		G_UNLOCK(markers_copy);
	}
	if (buffer_fd >= 0) {
		buffer_close(buffer_fd);
		buffer_fd = -1;
	}
	free_setup_check_fct_list();

	if (gtk_main_level())
		gtk_main_quit();

	/* This can't be done until all the windows are detroyed with main_quit
	 * otherwise, the widgets need to be updated, but they don't exist anymore
	 */
	close_plugins();
	g_slist_free(dplugin_list);
}

void sigterm (int signum)
{
	application_quit();
}

/* Before we really start, let's load the last saved profile */
static bool check_inifile (char *filepath)
{
	struct stat sts;
	FILE *fd;
	char buf[1024];
	size_t i;

	if (stat(filepath, &sts) == -1)
		return FALSE;

	if (!S_ISREG(sts.st_mode))
		return FALSE;

	fd = fopen(filepath, "r");
	if (!fd)
		return FALSE;

	i = fread(buf, 1023, 1, fd);
	fclose(fd);

	if (i == 0 )
		return FALSE;

	if (!strstr(buf, "[" CAPTURE_CONF "]"))
		return FALSE;

	return TRUE;
}

static int load_default_profile (char * filename)
{
	const char *home_dir = getenv("HOME");
	char buf[1024], tmp[1024];
	int ret, linecount, flag = 0;
	FILE *fd;

	/* Don't load anything */
	if (filename && !strcmp(filename, "-"))
		return 0;

	if (filename) {
		strncpy(buf, filename, 1023);
		if (!check_inifile(buf))
			filename = NULL;
	}

	if (!filename) {
		sprintf(buf, "%s/%s", home_dir, DEFAULT_PROFILE_NAME);
		/* if this is bad, we don't load anything and
		 * return sucess, so we still run */
		if (!check_inifile(buf))
			return 0;
		flag = 1;
	}

	ret = restore_all_plugins(buf, NULL);

	if (flag)
		return 0;

	if (ret > 0) {
		fd = fopen(buf, "r");
		if (!fd)
			return 0;

		linecount = 0;
		while (NULL != fgets(tmp, 1023, fd)) {
			linecount++;
			if (linecount == ret) {
				tmp[strlen(tmp) - 1] = 0;
				if (strcmp(tmp, "quit = 1")) {
					create_blocking_popup(GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
							"INI parsing / test failure",
							"Error parsing file '%s'\n\tline %i : '%s'\n",
							buf, ret, tmp);
				} else
					ret = -ENOTTY;
				break;
			}
		}
	}

	return ret;
}

static void init_application (void)
{
	GtkWidget *window;
	GtkWidget *table;
	GtkWidget *tmp;
	GtkBuilder *builder;
	int i;

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
	time_interval_widget = GTK_WIDGET(gtk_builder_get_object(builder, "time_interval"));
	sample_count_widget = GTK_WIDGET(gtk_builder_get_object(builder, "sample_count"));
	fft_size_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size"));
	fft_avg_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_avg"));
	fft_pwr_offset_widget = GTK_WIDGET(gtk_builder_get_object(builder, "pwr_offset"));
	plot_domain = GTK_WIDGET(gtk_builder_get_object(builder, "capture_domains"));
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
	time_unit_lbl = GTK_WIDGET(gtk_builder_get_object(builder, "time_unit_label"));

	channel_list_store = GTK_LIST_STORE(gtk_builder_get_object(builder, "channel_list"));
	g_builder_connect_signal(builder, "channel_toggle", "toggled",
		G_CALLBACK(channel_toggled), channel_list_store);

	dialogs_init(builder);
	trigger_dialog_init(builder);

	gtk_combo_box_set_active(GTK_COMBO_BOX(fft_size_widget), 2);

	/* Bind the plot mode radio buttons to the sensitivity of the sample count
	 * and FFT size widgets */
	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size_label"));
	g_object_bind_property_full(plot_domain, "active", tmp, "visible",
			0, domain_is_fft, NULL, NULL, NULL);
	g_object_bind_property_full(plot_domain, "active", fft_size_widget, "visible",
			 0, domain_is_fft, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "fft_avg_label"));
	g_object_bind_property_full(plot_domain, "active", tmp, "visible",
			0, domain_is_fft, NULL, NULL, NULL);
	g_object_bind_property_full(plot_domain, "active", fft_avg_widget, "visible",
			0, domain_is_fft, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "pwr_offset_label"));
	g_object_bind_property_full(plot_domain, "active", tmp, "visible",
			0, domain_is_fft, NULL, NULL, NULL);
	g_object_bind_property_full(plot_domain, "active", fft_pwr_offset_widget, "visible",
			0, domain_is_fft, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "time_interval_label"));
	g_object_bind_property_full(plot_domain, "active", tmp, "visible",
			0, domain_is_time, NULL, NULL, NULL);
	g_object_bind_property_full(plot_domain, "active", time_interval_widget, "visible",
			0, domain_is_time, NULL, NULL, NULL);
	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "time_unit_label"));
	g_object_bind_property_full(plot_domain, "active", tmp, "visible",
			0, domain_is_time, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "sample_count_label"));
	g_object_bind_property_full(plot_domain, "active", tmp, "visible",
			0, domain_is_time, NULL, NULL, NULL);
	g_object_bind_property_full(plot_domain, "active", sample_count_widget, "visible",
			0, domain_is_time, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "plot_type_label"));
	g_object_bind_property_full(plot_domain, "active", tmp, "visible",
		0, domain_is_time, NULL, NULL, NULL);
	g_object_bind_property_full(plot_domain, "active", plot_type, "visible",
		0, domain_is_time, NULL, NULL, NULL);

	gtk_combo_box_set_active(GTK_COMBO_BOX(plot_domain), TIME_PLOT);
	gtk_combo_box_set_active(GTK_COMBO_BOX(plot_type), 0);

	num_samples = 1;
	X = g_renew(gfloat, X, num_samples);
	fft_channel = g_renew(gfloat, fft_channel, num_samples);

	/* Create a GtkDatabox widget along with scrollbars and rulers */
	gtk_databox_create_box_with_scrollbars_and_rulers(&databox, &table,
							TRUE, TRUE, TRUE, TRUE);
	g_signal_connect(GTK_DATABOX(databox), "button_press_event",
				G_CALLBACK(marker_button), NULL);
	g_signal_connect(GTK_DATABOX(databox), "button_release_event",
				G_CALLBACK(marker_button), NULL);
	gtk_box_pack_start(GTK_BOX(capture_graph), table, TRUE, TRUE, 0);
	gtk_widget_modify_bg(databox, GTK_STATE_NORMAL, &color_background);

	if (MAX_MARKERS) {
		marker_type = MARKER_OFF;
		for (i = 0; i <= MAX_MARKERS; i++) {
			markers[i].graph = NULL;
			markers[i].active = (i <= 4);
		}
	}

	add_grid();

	gtk_widget_set_size_request(table, 400, 400);

	g_builder_connect_signal(builder, "zoom_in", "clicked",
		G_CALLBACK(zoom_in), databox);
	g_builder_connect_signal(builder, "zoom_out", "clicked",
		G_CALLBACK(zoom_out), databox);
	g_builder_connect_signal(builder, "zoom_fit", "clicked",
		G_CALLBACK(zoom_fit), databox);
	g_signal_connect(G_OBJECT(show_grid), "toggled",
		G_CALLBACK(show_grid_toggled), databox);
	g_signal_connect(enable_auto_scale, "toggled",
		G_CALLBACK(enable_auto_scale_cb), NULL);

	g_signal_connect(plot_domain, "changed",
		G_CALLBACK(check_valid_setup), NULL);

	g_builder_connect_signal(builder, "channel_list_view", "button_release_event",
		G_CALLBACK(check_valid_setup), NULL);
	g_builder_connect_signal(builder, "channel_list_view", "key-release-event",
		G_CALLBACK(check_valid_setup), NULL);

	capture_button_hid = g_signal_connect(capture_button, "toggled",
		G_CALLBACK(capture_button_clicked), NULL);

	g_signal_connect(G_OBJECT(window), "delete-event",
			G_CALLBACK(application_quit), NULL);

	g_builder_bind_property(builder, "capture_button", "active",
			"channel_list_view", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
			"input_device_list", "sensitive", G_BINDING_INVERT_BOOLEAN);

	g_builder_bind_property(builder, "capture_button", "active",
			"capture_domains", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
			"fft_size", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
			"plot_type", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
			"sample_count", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
			"time_interval", "sensitive", G_BINDING_INVERT_BOOLEAN);

	capture_button_bind = g_object_bind_property_full(capture_button, "active", capture_button,
			"stock-id", 0, capture_button_icon_transform, NULL, NULL, NULL);
	g_object_bind_property_full(time_interval_widget, "value", sample_count_widget,
		"value", G_BINDING_BIDIRECTIONAL, time_to_samples, samples_to_time, NULL, NULL);

	init_device_list();
	load_plugins(notebook);
	plugin_setup_validation_fct = find_setup_check_fct_by_devname(current_device);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(sample_count_widget), 500);
	check_valid_setup();
	rx_update_labels();

	gtk_widget_show(window);
	gtk_widget_show_all(capture_graph);

}

void usage (char *program)
{
	printf("%s: the IIO visualization and control tool\n", program);
	printf( " Copyright (C) Analog Devices, Inc. and others\n"
		" This is free software; see the source for copying conditions.\n"
		" There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A\n"
		" PARTICULAR PURPOSE.\n\n");

	/* please keep this list sorted in alphabetal order */
	printf( "Command line options:\n"
		"\t-p\tload specific profile\n");

	printf("\nEnvironmental variables:\n"
		"\tOSC_FORCE_PLUGIN\tforce loading of a specfic plugin\n");

	exit(-1);
}

gint main(gint argc, char *argv[])
{
	int c;
	char *profile = NULL;

	opterr = 0;
	while ((c = getopt (argc, argv, "p:")) != -1)
	switch (c) {
		case 'p':
			profile = strdup(optarg);
			break;
		case '?':
			usage(argv[0]);
			break;
		default:
			printf("Unknown command line option\n");
			usage(argv[0]);
			break;
	}

	g_thread_init (NULL);
	gdk_threads_init ();
	gtk_init(&argc, &argv);

	signal(SIGTERM, sigterm);
	signal(SIGINT, sigterm);
	signal(SIGHUP, sigterm);

	gdk_threads_enter();
	init_application();
	c = load_default_profile(profile);
	if (c == 0)
		gtk_main();
	else
		application_quit();

	gdk_threads_leave();

	if (c == 0 || c == -ENOTTY)
		return 0;
	else
		return -1;
}
