/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <gtk/gtk.h>
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
#include <string.h>
#include <dirent.h>

#include <complex.h>
#include <fftw3.h>
#include <iio.h>

#include "ini/ini.h"
#include "osc.h"
#include "datatypes.h"
#include "int_fft.h"
#include "config.h"
#include "osc_plugin.h"

extern int count_char_in_string(char c, const char *s);
extern char *get_filename_from_path(const char *path);

GSList *plugin_list = NULL;

gint capture_function = 0;
gfloat plugin_fft_corr = 0.0;
static GtkWidget *main_window;
static GtkWidget *tooltips_en;
static gint window_x_pos, window_y_pos;
static GList *plot_list = NULL;
static int num_capturing_plots;
G_LOCK_DEFINE_STATIC(buffer_full);
static gboolean stop_capture;
static struct plugin_check_fct *setup_check_functions = NULL;
static int num_check_fcts = 0;
static GSList *dplugin_list = NULL;
GtkWidget  *notebook;

struct iio_context *ctx;
unsigned int num_devices = 0;

static void gfunc_save_plot_data_to_ini(gpointer data, gpointer user_data);
static int plugin_restore_ini_state(char *plugin_name, char *attribute, const char *value);
static GtkWidget * new_plot_cb(GtkMenuItem *item, gpointer user_data);
static void plot_init(GtkWidget *plot);
static void plot_destroyed_cb(OscPlot *plot);

static char * dma_devices[] = {
	"ad9122",
	"ad9144",
	"ad9250",
	"ad9361",
	"ad9643",
	"ad9680"
};

#define DMA_DEVICES_COUNT (sizeof(dma_devices) / sizeof(dma_devices[0]))

static const char * get_adi_part_code(const char *device_name)
{
	const char *ad = NULL;

	if (!device_name)
		return NULL;
	ad = strstr(device_name, "ad");
	if (!ad || strlen(ad) < strlen("adxxxx"))
		return NULL;
	if (g_ascii_isdigit(ad[2]) &&
		g_ascii_isdigit(ad[3]) &&
		g_ascii_isdigit(ad[4]) &&
		g_ascii_isdigit(ad[5])) {
	  return ad;
	}

	return NULL;
}

bool dma_valid_selection(const char *device, unsigned mask, unsigned channel_count)
{
	static const unsigned long eight_channel_masks[] = {
		0x01, 0x02, 0x04, 0x08, 0x03, 0x0C, /* 1 & 2 chan */
		0x10, 0x20, 0x40, 0x80, 0x30, 0xC0, /* 1 & 2 chan */
		0x33, 0xCC, 0xC3, 0x3C, 0x0F, 0xF0, /* 4 chan */
		0xFF,                               /* 8chan */
		0x00
	};
	static const unsigned long four_channel_masks[] = {
		0x01, 0x02, 0x04, 0x08, 0x03, 0x0C, 0x0F,
		0x00
	};
	bool ret = true;
	int i;

	device = get_adi_part_code(device);
	if (!device)
		return true;

	for (i = 0; i < DMA_DEVICES_COUNT; i++) {
		if (!strncmp(device, dma_devices[i], strlen(dma_devices[i])))
			break;
	}

	/* Skip validation for devices that are not in the list */
	if (i == DMA_DEVICES_COUNT)
		return true;

	if (channel_count == 8) {
		ret = false;
		for (i = 0;  i < sizeof(eight_channel_masks) / sizeof(eight_channel_masks[0]); i++)
			if (mask == eight_channel_masks[i])
				return true;
	} else if (channel_count == 4) {
		ret = false;
		for (i = 0;  i < sizeof(four_channel_masks) / sizeof(four_channel_masks[0]); i++)
			if (mask == four_channel_masks[i])
				return true;
	}

	return ret;
}

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

static double win_hanning(int j, int n)
{
	double a = 2.0 * M_PI / (n - 1), w;

	w = 0.5 * (1.0 - cos(a * j));

	return (w);
}

static void do_fft(Transform *tr)
{
	struct extra_info *ch_info;
	struct _fft_settings *settings = tr->settings;
	struct _fft_alg_data *fft = &settings->fft_alg_data;
	struct marker_type *markers = settings->markers;
	enum marker_types marker_type = MARKER_OFF;
	gfloat *in_data = *tr->in_data;
	gfloat *in_data_c;
	gfloat *out_data = tr->y_axis;
	gfloat *X = tr->x_axis;
	unsigned int fft_size = settings->fft_size;
	int i, j, k;
	int cnt;
	gfloat mag;
	double avg, pwr_offset;
	unsigned int maxx[MAX_MARKERS + 1];
	gfloat maxY[MAX_MARKERS + 1];

	if (settings->marker_type)
		marker_type = *((enum marker_types *)settings->marker_type);

	if ((fft->cached_fft_size == -1) || (fft->cached_fft_size != fft_size) ||
		(fft->cached_num_active_channels != fft->num_active_channels)) {

		if (fft->cached_fft_size != -1) {
			fftw_destroy_plan(fft->plan_forward);
			fftw_free(fft->win);
			fftw_free(fft->out);
			if (fft->in != NULL)
				fftw_free(fft->in);
			if (fft->in_c != NULL)
				fftw_free(fft->in_c);
			fft->in_c = NULL;
			fft->in = NULL;
		}

		fft->win = fftw_malloc(sizeof(double) * fft_size);
		if (fft->num_active_channels == 2) {
			fft->m = fft_size;
			fft->in_c = fftw_malloc(sizeof(fftw_complex) * fft_size);
			fft->in = NULL;
			fft->out = fftw_malloc(sizeof(fftw_complex) * (fft->m + 1));
			fft->plan_forward = fftw_plan_dft_1d(fft_size, fft->in_c, fft->out, FFTW_FORWARD, FFTW_ESTIMATE);
		} else {
			fft->m = fft_size / 2;
			fft->out = fftw_malloc(sizeof(fftw_complex) * (fft->m + 1));
			fft->in_c = NULL;
			fft->in = fftw_malloc(sizeof(double) * fft_size);
			fft->plan_forward = fftw_plan_dft_r2c_1d(fft_size, fft->in, fft->out, FFTW_ESTIMATE);
		}

		for (i = 0; i < fft_size; i ++)
			fft->win[i] = win_hanning(i, fft_size);

		fft->cached_fft_size = fft_size;
		fft->cached_num_active_channels = fft->num_active_channels;
	}

	if (fft->num_active_channels == 2) {
		ch_info = iio_channel_get_data(tr->channel_parent2);
		in_data_c = ch_info->data_ref;
		for (cnt = 0, i = 0; cnt < fft_size; cnt++) {
			/* normalization and scaling see fft_corr */
			fft->in_c[cnt] = in_data[i] * fft->win[cnt] + I * in_data_c[i] * fft->win[cnt];
			i++;
		}
	} else {
		for (cnt = 0, i = 0; i < fft_size; i++) {
			/* normalization and scaling see fft_corr */
			fft->in[cnt] = in_data[i] * fft->win[cnt];
			cnt++;
		}
	}

	fftw_execute(fft->plan_forward);
	avg = (double)settings->fft_avg;
	if (avg && avg != 128 )
		avg = 1.0f / avg;

	pwr_offset = settings->fft_pwr_off;

	for (j = 0; j <= MAX_MARKERS; j++) {
		maxx[j] = 0;
		maxY[j] = -100.0f;
	}

	for (i = 0; i < fft->m; ++i) {
		if (fft->num_active_channels == 2) {
			if (i < (fft->m / 2))
				j = i + (fft->m / 2);
			else
				j = i - (fft->m / 2);
		} else {
				j = i;
		}

		if (creal(fft->out[j]) == 0 && cimag(fft->out[j]) == 0)
			fft->out[j] = FLT_MIN + I * FLT_MIN;

		mag = 10 * log10((creal(fft->out[j]) * creal(fft->out[j]) +
				cimag(fft->out[j]) * cimag(fft->out[j])) / ((unsigned long long)fft->m * fft->m)) +
			fft->fft_corr + pwr_offset + plugin_fft_corr;
		/* it's better for performance to have seperate loops,
		 * rather than do these tests inside the loop, but it makes
		 * the code harder to understand... Oh well...
		 ***/
		if (out_data[i] == FLT_MAX) {
			/* Don't average the first iterration */
			 out_data[i] = mag;
		} else if (!avg) {
			/* keep peaks */
			if (out_data[i] <= mag)
				out_data[i] = mag;
		} else if (avg == 128) {
			/* keep min */
			if (out_data[i] >= mag)
				out_data[i] = mag;
		} else {
			/* do an average */
			out_data[i] = ((1 - avg) * out_data[i]) + (avg * mag);
		}
		if (!tr->has_the_marker)
			continue;
		if (MAX_MARKERS && (marker_type == MARKER_PEAK ||
				marker_type == MARKER_ONE_TONE ||
				marker_type == MARKER_IMAGE)) {
			if (i == 0) {
				maxx[0] = 0;
				maxY[0] = out_data[0];
			} else {
				for (j = 0; j <= MAX_MARKERS && markers[j].active; j++) {
					if  ((out_data[i - 1] > maxY[j]) &&
						((!((out_data[i - 2] > out_data[i - 1]) &&
						 (out_data[i - 1] > out_data[i]))) &&
						 (!((out_data[i - 2] < out_data[i - 1]) &&
						 (out_data[i - 1] < out_data[i]))))) {
						if (marker_type == MARKER_PEAK) {
							for (k = MAX_MARKERS; k > j; k--) {
								maxY[k] = maxY[k - 1];
								maxx[k] = maxx[k - 1];
							}
						}
						maxY[j] = out_data[i - 1];
						maxx[j] = i - 1;
						break;
					}
				}
			}
		}
	}

	if (!tr->has_the_marker)
		return;

	unsigned int m = fft->m;

	if ((marker_type == MARKER_ONE_TONE || marker_type == MARKER_IMAGE) &&
		((fft->num_active_channels == 1 && maxx[0] == 0) ||
		(fft->num_active_channels == 2 && maxx[0] == m/2))) {
		unsigned int max_tmp;

		max_tmp = maxx[1];
		maxx[1] = maxx[0];
		maxx[0] = max_tmp;
	}

	if (MAX_MARKERS && marker_type != MARKER_OFF) {
		for (j = 0; j <= MAX_MARKERS && markers[j].active; j++) {
			if (marker_type == MARKER_PEAK) {
				markers[j].x = (gfloat)X[maxx[j]];
				markers[j].y = (gfloat)out_data[maxx[j]];
				markers[j].bin = maxx[j];
			} else if (marker_type == MARKER_FIXED) {
				markers[j].x = (gfloat)X[markers[j].bin];
				markers[j].y = (gfloat)out_data[markers[j].bin];
			} else if (marker_type == MARKER_ONE_TONE) {
				/* assume peak is the tone */
				if (j == 0) {
					markers[j].bin = maxx[j];
					i = 1;
				} else if (j == 1) {
					/* keep DC */
					if (tr->type_id == COMPLEX_FFT_TRANSFORM)
						markers[j].bin = m / 2;
					else
						markers[j].bin = 0;
				} else {
					/* where should the spurs be? */
					i++;
					if (tr->type_id == COMPLEX_FFT_TRANSFORM) {
						markers[j].bin = (markers[0].bin - (m / 2)) * i + (m / 2);
						if (markers[j].bin > m)
							markers[j].bin -= 2 * (markers[j].bin - m);
						if (markers[j].bin < ( m/2 ))
							markers[j].bin += 2 * ((m / 2) - markers[j].bin);
					} else {
						markers[j].bin = markers[0].bin * i;
						if (markers[j].bin > (m))
							markers[j].bin -= 2 * (markers[j].bin - (m));
						if (markers[j].bin < 0)
							markers[j].bin += -markers[j].bin;
					}
				}
				/* make sure we don't need to nudge things one way or the other */
				k = markers[j].bin;
				while (out_data[k] < out_data[k + 1]) {
					k++;
				}

				while (markers[j].bin != 0 &&
						out_data[markers[j].bin] < out_data[markers[j].bin - 1]) {
					markers[j].bin--;
				}

				if (out_data[k] > out_data[markers[j].bin])
					markers[j].bin = k;

				markers[j].x = (gfloat)X[markers[j].bin];
				markers[j].y = (gfloat)out_data[markers[j].bin];
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
				markers[j].y = (gfloat)out_data[markers[j].bin];

			}
		}
		if (*settings->markers_copy) {
			memcpy(*settings->markers_copy, settings->markers,
				sizeof(struct marker_type) * MAX_MARKERS);
			*settings->markers_copy = NULL;
			g_mutex_unlock(settings->marker_lock);
		}
	}
}

static void xcorr(fftw_complex *signala, fftw_complex *signalb, fftw_complex *result, int N)
{
	fftw_complex * signala_ext = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * (2 * N - 1));
	fftw_complex * signalb_ext = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * (2 * N - 1));
	fftw_complex * outa = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * (2 * N - 1));
	fftw_complex * outb = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * (2 * N - 1));
	fftw_complex * out = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * (2 * N - 1));
	fftw_complex scale;
	int i;

	if (!signala_ext || !signalb_ext || !outa || !outb || !out)
		return;

	fftw_plan pa = fftw_plan_dft_1d(2 * N - 1, signala_ext, outa, FFTW_FORWARD, FFTW_ESTIMATE);
	fftw_plan pb = fftw_plan_dft_1d(2 * N - 1, signalb_ext, outb, FFTW_FORWARD, FFTW_ESTIMATE);
	fftw_plan px = fftw_plan_dft_1d(2 * N - 1, out, result, FFTW_BACKWARD, FFTW_ESTIMATE);

	//zeropadding
	memset(signala_ext, 0, sizeof(fftw_complex) * (N - 1));
	memcpy(signala_ext + (N - 1), signala, sizeof(fftw_complex) * N);
	memcpy(signalb_ext, signalb, sizeof(fftw_complex) * N);
	memset(signalb_ext + N, 0, sizeof(fftw_complex) * (N - 1));

	fftw_execute(pa);
	fftw_execute(pb);

	scale = 1.0/(2 * N -1);
	for (i = 0; i < 2 * N - 1; i++)
		out[i] = outa[i] * conj(outb[i]) * scale;

	fftw_execute(px);

	fftw_destroy_plan(pa);
	fftw_destroy_plan(pb);
	fftw_destroy_plan(px);

	fftw_free(signala_ext);
	fftw_free(signalb_ext);
	fftw_free(out);
	fftw_free(outa);
	fftw_free(outb);

	fftw_cleanup();

	return;
}

void time_transform_function(Transform *tr, gboolean init_transform)
{
	struct _time_settings *settings = tr->settings;
	unsigned axis_length = settings->num_samples;
	int i;

	if (init_transform) {
		Transform_resize_x_axis(tr, axis_length);
		for (i = 0; i < axis_length; i++)
			tr->x_axis[i] = i;
		tr->y_axis_size = axis_length;

		if (settings->apply_inverse_funct || settings->apply_multiply_funct || settings->apply_add_funct) {
			Transform_resize_y_axis(tr, tr->y_axis_size);
			tr->local_output_buf = true;
		} else {
			tr->y_axis = *tr->in_data;
			tr->local_output_buf = false;
		}

		return;
	}
	if (!tr->local_output_buf)
		return;

	for (i = 0; i < tr->y_axis_size; i++) {
		if (settings->apply_inverse_funct) {
			if ((*tr->in_data)[i] != 0)
				tr->y_axis[i] = 1 / (*tr->in_data)[i];
			else
				tr->y_axis[i] = 65535;
		} else {
			tr->y_axis[i] = (*tr->in_data)[i];
		}
		if (settings->apply_multiply_funct)
			tr->y_axis[i] *= settings->multiply_value;
		if (settings->apply_add_funct)
			tr->y_axis[i] += settings->add_value;
	}
}

void cross_correlation_transform_function(Transform *tr, gboolean init_transform)
{
	struct _cross_correlation_settings *settings = tr->settings;
	unsigned axis_length = settings->num_samples;
	struct extra_info *ch_info;
	gfloat *i_0, *q_0;
	gfloat *i_1, *q_1;
	int i;

	ch_info = iio_channel_get_data(tr->channel_parent);
	i_0 = ch_info->data_ref;
	ch_info = iio_channel_get_data(tr->channel_parent2);
	q_0 = ch_info->data_ref;
	ch_info = iio_channel_get_data(tr->channel_parent3);
	i_1 = ch_info->data_ref;
	ch_info = iio_channel_get_data(tr->channel_parent4);
	q_1 = ch_info->data_ref;

	if (init_transform) {
		if (settings->signal_a) {
			fftw_free(settings->signal_a);
			settings->signal_a = NULL;
		}
		if (settings->signal_b) {
			fftw_free(settings->signal_b);
			settings->signal_b = NULL;
		}
		if (settings->xcorr_data) {
			fftw_free(settings->xcorr_data);
			settings->xcorr_data = NULL;
		}
		settings->signal_a = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * axis_length);
		settings->signal_b = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * axis_length);
		settings->xcorr_data = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * axis_length * 2);

		Transform_resize_x_axis(tr, 2 * axis_length);
		Transform_resize_y_axis(tr, 2 * axis_length);
		for (i = 0; i < 2 * axis_length - 1; i++) {
			tr->x_axis[i] = i - (gfloat)axis_length + 1;
			tr->y_axis[i] = 0;
		}
		tr->y_axis_size = 2 * axis_length - 1;

		return;
	}
	for (i = 0; i < axis_length; i++) {
		settings->signal_a[i] = q_0[i] + I * i_0[i];
		settings->signal_b[i] = q_1[i] + I * i_1[i];
	}

	if (settings->revert_xcorr)
		xcorr(settings->signal_b, settings->signal_a, settings->xcorr_data, axis_length);
	else
		xcorr(settings->signal_a, settings->signal_b, settings->xcorr_data, axis_length);

	gfloat *out_data = tr->y_axis;
	gfloat *X = tr->x_axis;
	struct marker_type *markers = settings->markers;
	enum marker_types marker_type = MARKER_OFF;
	unsigned int maxx[MAX_MARKERS + 1];
	gfloat maxY[MAX_MARKERS + 1];
	int j, k;

	if (settings->marker_type)
		marker_type = *((enum marker_types *)settings->marker_type);

	for (j = 0; j <= MAX_MARKERS; j++) {
		maxx[j] = 0;
		maxY[j] = -100.0f;
	}

	for (i = 0; i < 2 * axis_length - 1; i++) {
		tr->y_axis[i] =  2 * creal(settings->xcorr_data[i]) / (gfloat)axis_length;
		if (!tr->has_the_marker)
			continue;

		if (MAX_MARKERS && marker_type == MARKER_PEAK) {
			if (i == 0) {
				maxx[0] = 0;
				maxY[0] = out_data[0];
			} else {
				for (j = 0; j <= MAX_MARKERS && markers[j].active; j++) {
					if  ((fabs(out_data[i - 1]) > maxY[j]) &&
						((!((out_data[i - 2] > out_data[i - 1]) &&
						 (out_data[i - 1] > out_data[i]))) &&
						 (!((out_data[i - 2] < out_data[i - 1]) &&
						 (out_data[i - 1] < out_data[i]))))) {
						if (marker_type == MARKER_PEAK) {
							for (k = MAX_MARKERS; k > j; k--) {
								maxY[k] = maxY[k - 1];
								maxx[k] = maxx[k - 1];
							}
						}
						maxY[j] = fabs(out_data[i - 1]);
						maxx[j] = i - 1;
						break;
					}
				}
			}
		}
	}

	if (!tr->has_the_marker)
		return;

	if (MAX_MARKERS && marker_type != MARKER_OFF) {
		for (j = 0; j <= MAX_MARKERS && markers[j].active; j++)
			if (marker_type == MARKER_PEAK) {
				markers[j].x = (gfloat)X[maxx[j]];
				markers[j].y = (gfloat)out_data[maxx[j]];
				markers[j].bin = maxx[j];
			}
		if (*settings->markers_copy) {
			memcpy(*settings->markers_copy, settings->markers,
				sizeof(struct marker_type) * MAX_MARKERS);
			*settings->markers_copy = NULL;
			g_mutex_unlock(settings->marker_lock);
		}
	}
}

void fft_transform_function(Transform *tr, gboolean init_transform)
{
	struct iio_channel *chn = tr->channel_parent;
	struct extra_info *ch_info = iio_channel_get_data(chn);
	struct extra_dev_info *dev_info = iio_device_get_data(ch_info->dev);
	struct _fft_settings *settings = tr->settings;
	unsigned axis_length;
	unsigned num_samples = dev_info->sample_count;
	double corr;
	int i;

	if (init_transform) {
		unsigned int bits_used = iio_channel_get_data_format(chn)->bits;
		axis_length = settings->fft_size * settings->fft_alg_data.num_active_channels / 2;
		Transform_resize_x_axis(tr, axis_length);
		Transform_resize_y_axis(tr, axis_length);
		tr->y_axis_size = axis_length;
		if (settings->fft_alg_data.num_active_channels == 2)
			corr = dev_info->adc_freq / 2.0;
		else
			corr = 0;
		for (i = 0; i < axis_length; i++) {
			tr->x_axis[i] = i * dev_info->adc_freq / num_samples - corr;
			tr->y_axis[i] = FLT_MAX;
		}

		/* Compute FFT normalization and scaling offset */
		settings->fft_alg_data.fft_corr = 20 * log10(2.0 / (1 << (bits_used - 1)));

		/* Make sure that previous positions of markers are not out of bonds */
		if (settings->markers)
			for (i = 0; i <= MAX_MARKERS; i++)
				if (settings->markers[i].bin >= axis_length)
					settings->markers[i].bin = 0;

		return;
	}
	do_fft(tr);
}

void constellation_transform_function(Transform *tr, gboolean init_transform)
{
	struct extra_info *ch_info = iio_channel_get_data(tr->channel_parent2);
	gfloat *y_axis = ch_info->data_ref;
	struct _constellation_settings *settings = tr->settings;
	unsigned axis_length = settings->num_samples;

	if (init_transform) {
		tr->x_axis_size = axis_length;
		tr->y_axis_size = axis_length;
		tr->x_axis = *tr->in_data;
		tr->y_axis = y_axis;

		return;
	}
}

static void gfunc_update_plot(gpointer data, gpointer user_data)
{
	GtkWidget *plot = data;

	osc_plot_data_update(OSC_PLOT(plot));
}

static void gfunc_restart_plot(gpointer data, gpointer user_data)
{
	GtkWidget *plot = data;

	osc_plot_restart(OSC_PLOT(plot));
}

static void gfunc_close_plot(gpointer data, gpointer user_data)
{
	GtkWidget *plot = data;

	osc_plot_draw_stop(OSC_PLOT(plot));
}

static void gfunc_destroy_plot(gpointer data, gpointer user_data)
{
	GtkWidget *plot = data;

	osc_plot_destroy(OSC_PLOT(plot));
}

static void update_all_plots(void)
{
	g_list_foreach(plot_list, gfunc_update_plot, NULL);
}

static void restart_all_running_plots(void)
{
	g_list_foreach(plot_list, gfunc_restart_plot, NULL);
}

static void close_all_plots(void)
{
	g_list_foreach(plot_list, gfunc_close_plot, NULL);
}

static void destroy_all_plots(void)
{
	g_list_foreach(plot_list, gfunc_destroy_plot, NULL);
}

static void disable_all_channels(struct iio_device *dev)
{
	unsigned int i, nb_channels = iio_device_get_channels_count(dev);
	for (i = 0; i < nb_channels; i++)
		iio_channel_disable(iio_device_get_channel(dev, i));
}

static void close_active_buffers(void)
{
	unsigned int i;

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct extra_dev_info *info = iio_device_get_data(dev);
		if (info->buffer) {
			iio_buffer_destroy(info->buffer);
			info->buffer = NULL;
		}

		disable_all_channels(dev);
	}
}

static void stop_sampling(void)
{
	stop_capture = TRUE;
	close_active_buffers();
	G_TRYLOCK(buffer_full);
	G_UNLOCK(buffer_full);
}

static void detach_plugin(GtkToolButton *btn, gpointer data);

static GtkWidget* plugin_tab_add_detach_btn(GtkWidget *page, const struct detachable_plugin *d_plugin)
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
	if (plugin->get_preferred_size) {
		int width = -1, height = -1;

		plugin->get_preferred_size(&width, &height);
		gtk_window_set_default_size(GTK_WINDOW(window), width, height);
	}

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

static const char * device_name_check(const char *name)
{
	struct iio_device *dev;

	if (!name)
		return NULL;

	dev = iio_context_find_device(ctx, name);
	if (!dev)
		return NULL;

	return iio_device_get_name(dev) ?: iio_device_get_id(dev);
}

/*
 * helper functions for plugins which want to look at data
 */

struct iio_context *get_context_from_osc(void)
{
	return ctx;
}

const void * plugin_get_device_by_reference(const char * device_name)
{
	return device_name_check(device_name);
}

OscPlot * plugin_find_plot_with_domain(int domain)
{
	OscPlot *plot;
	GList *node;

	if (!plot_list)
		return NULL;

	for (node = plot_list; node; node = g_list_next(node)) {
		plot = node->data;
		if (osc_plot_get_plot_domain(plot) == domain)
			return plot;
	}

	return NULL;
}

enum marker_types plugin_get_plot_marker_type(OscPlot *plot, const char *device)
{
	if (!plot || !device)
		return MARKER_NULL;

	if (!strcmp(osc_plot_get_active_device(plot), device))
		return MARKER_NULL;

	return osc_plot_get_marker_type(plot);
}

void plugin_set_plot_marker_type(OscPlot *plot, const char *device, enum marker_types type)
{
	int plot_domain;

	if (!plot || !device)
		return;

	plot_domain = osc_plot_get_plot_domain(plot);
	if (plot_domain == FFT_PLOT || plot_domain == XY_PLOT)
		if (!strcmp(osc_plot_get_active_device(plot), device))
			return;

	osc_plot_set_marker_type(plot, type);
}

gdouble plugin_get_plot_fft_avg(OscPlot *plot, const char *device)
{
	if (!plot || !device)
		return 0;

	if (!strcmp(osc_plot_get_active_device(plot), device))
		return 0;

	return osc_plot_get_fft_avg(plot);
}

int plugin_data_capture_size(const char *device)
{
	struct extra_dev_info *info;
	struct iio_device *dev;

	if (!device)
		return 0;

	dev = iio_context_find_device(ctx, device);
	if (!dev)
		return 0;

	info = iio_device_get_data(dev);
	return info->sample_count * iio_device_get_sample_size(dev);
}

int plugin_data_capture_num_active_channels(const char *device)
{
	int nb_active = 0;
	unsigned int i, nb_channels;
	struct iio_device *dev;

	if (!device)
		return 0;

	dev = iio_context_find_device(ctx, device);
	if (!dev)
		return 0;

	nb_channels = iio_device_get_channels_count(dev);
	for (i = 0; i < nb_channels; i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);
		if (iio_channel_is_enabled(chn))
			nb_active++;
	}

	return nb_active;
}

int plugin_data_capture_bytes_per_sample(const char *device)
{
	struct iio_device *dev;

	if (!device)
		return 0;

	dev = iio_context_find_device(ctx, device);
	if (!dev)
		return 0;

	return iio_device_get_sample_size(dev);
}

int plugin_data_capture_with_domain(const char *device, gfloat ***cooked_data,
			struct marker_type **markers_cp, int domain)
{
	OscPlot *fft_plot;
	struct iio_device *dev, *tmp_dev = NULL;
	struct extra_dev_info *dev_info;
	struct marker_type *markers_copy = NULL;
	GMutex *markers_lock;
	bool is_fft_mode;
	int i, j;
	bool new = FALSE;
	const char *tmp = NULL;

	if (device == NULL)
		dev = NULL;
	else
		dev = iio_context_find_device(ctx, device);
	fft_plot = plugin_find_plot_with_domain(domain);
	if (fft_plot) {
		tmp = osc_plot_get_active_device(fft_plot);
		tmp_dev = iio_context_find_device(ctx, tmp);
	}

	/* if there isn't anything to send, clear everything */
	if (dev == NULL) {
		if (cooked_data && *cooked_data) {
			if (tmp_dev)
				for (i = 0; i < iio_device_get_channels_count(tmp_dev); i++)
					if ((*cooked_data)[i]) {
						g_free((*cooked_data)[i]);
						(*cooked_data)[i] = NULL;
					}
			g_free(*cooked_data);
			*cooked_data = NULL;
		}
		if (markers_cp && *markers_cp) {
			if (*markers_cp)
				g_free(*markers_cp);
			*markers_cp = NULL;
		}
		return -ENXIO;
	}

	if (!dev)
		return -ENXIO;
	if (osc_plot_running_state(fft_plot) == FALSE)
		return -ENXIO;
	if (osc_plot_get_marker_type(fft_plot) == MARKER_OFF ||
			osc_plot_get_marker_type(fft_plot) == MARKER_NULL)
		return -ENXIO;

	if (cooked_data) {
		dev_info = iio_device_get_data(dev);

		/* One consumer at a time */
		if (dev_info->channels_data_copy)
			return -EBUSY;

		/* make sure space is allocated */
		if (*cooked_data) {
			*cooked_data = g_renew(gfloat *, *cooked_data,
					iio_device_get_channels_count(dev));
			new = false;
		} else {
			*cooked_data = g_new(gfloat *, iio_device_get_channels_count(dev));
			new = true;
		}

		if (!*cooked_data)
			goto capture_malloc_fail;

		for (i = 0; i < iio_device_get_channels_count(dev); i++) {
			if (new)
				(*cooked_data)[i] = g_new(gfloat,
						dev_info->sample_count);
			else
				(*cooked_data)[i] = g_renew(gfloat,
						(*cooked_data)[i],
						dev_info->sample_count);
			if (!(*cooked_data)[i])
				goto capture_malloc_fail;

			for (j = 0; j < dev_info->sample_count; j++)
				(*cooked_data)[i][j] = 0.0f;
		}

		/* where to put the copy */
		dev_info->channels_data_copy = *cooked_data;

		/* Wait til the buffer is full */
		G_LOCK(buffer_full);

		/* if the lock is released, but the copy is still there
		 * that's because someone else broke the lock
		 */
		if (dev_info->channels_data_copy) {
			dev_info->channels_data_copy = NULL;
			return -EINTR;
		}
	}

	if (!fft_plot) {
		is_fft_mode = false;
	} else {
		markers_copy = (struct marker_type *)osc_plot_get_markers_copy(fft_plot);
		markers_lock = osc_plot_get_marker_lock(fft_plot);
		is_fft_mode = true;
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
		osc_plot_set_markers_copy(fft_plot, *markers_cp);

		/* Wait til the copy is complete */
		g_mutex_lock(markers_lock);

		/* if the lock is released, but the copy is still here
		 * that's because someone else broke the lock
		 */
		 if (markers_copy) {
			osc_plot_set_markers_copy(fft_plot, NULL);
			 return -EINTR;
		 }
	}
 	return 0;

capture_malloc_fail:
	printf("%s:%s malloc failed\n", __FILE__, __func__);
	return -ENOMEM;
}

OscPlot * plugin_get_new_plot(void)
{
	return OSC_PLOT(new_plot_cb(NULL, NULL));
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

static void load_plugin(const char *name, GtkWidget *notebook,
		const char *ini_fn)
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
	plugin_list = g_slist_append (plugin_list, (gpointer) plugin);
	plugin->init(notebook, ini_fn);

	d_plugin = malloc(sizeof(struct detachable_plugin));
	d_plugin->plugin = plugin;
	dplugin_list = g_slist_append(dplugin_list, (gpointer)d_plugin);

	plugin_make_detachable(d_plugin);

	printf("Loaded plugin: %s\n", plugin->name);
}

static void close_plugins(const char *ini_fn)
{
	GSList *node;
	struct osc_plugin *plugin = NULL;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin) {
			printf("Closing plugin: %s\n", plugin->name);
			if (plugin->destroy)
				plugin->destroy(ini_fn);
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

static void load_plugins(GtkWidget *notebook, const char *ini_fn)
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
		load_plugin(buf, notebook, ini_fn);
	}
}

static void plugin_state_ini_save(gpointer data, gpointer user_data)
{
	struct detachable_plugin *p = (struct detachable_plugin *)data;
	FILE *fp = (FILE *)user_data;

	fprintf(fp, "plugin.%s.detached=%d\n", p->plugin->name, p->detached_state);

	if (p->detached_state) {
		GtkWidget *plugin_window;
		gint x_pos, y_pos;

		plugin_window = gtk_widget_get_toplevel(p->detach_attach_button);
		gtk_window_get_position(GTK_WINDOW(plugin_window), &x_pos, &y_pos);
		fprintf(fp, "plugin.%s.x_pos=%d\n", p->plugin->name, x_pos);
		fprintf(fp, "plugin.%s.y_pos=%d\n", p->plugin->name, y_pos);
	}
}

static ssize_t demux_sample(const struct iio_channel *chn,
		void *sample, size_t size, void *d)
{
	struct extra_info *info = iio_channel_get_data(chn);
	struct extra_dev_info *dev_info = iio_device_get_data(info->dev);
	const struct iio_data_format *format = iio_channel_get_data_format(chn);

	/* Prevent buffer overflow */
	if (info->offset == dev_info->sample_count)
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

static bool device_is_oneshot(struct iio_device *dev)
{
	const char *name = iio_device_get_name(dev);

	if (strncmp(name, "ad-mc-", 5) == 0)
		return true;

	return false;
}

static gboolean capture_process(void)
{
	unsigned int i;

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct extra_dev_info *dev_info = iio_device_get_data(dev);
		unsigned int i, sample_size = iio_device_get_sample_size(dev);
		unsigned int nb_channels = iio_device_get_channels_count(dev);
		unsigned int sample_count = dev_info->sample_count;

		if (dev_info->input_device == false)
			continue;

		if (sample_size == 0)
			continue;

		if (device_is_oneshot(dev)) {
			dev_info->buffer = iio_device_create_buffer(dev,
				sample_count, false);
			if (!dev_info->buffer) {
				fprintf(stderr, "Unable to create buffer\n");
				goto capture_stop_check;
			}
		}

		if (dev_info->buffer == NULL)
			goto capture_stop_check;

		/* Reset the data offset for all channels */
		for (i = 0; i < nb_channels; i++) {
			struct iio_channel *ch = iio_device_get_channel(dev, i);
			struct extra_info *info = iio_channel_get_data(ch);
			info->offset = 0;
		}

		do {
			ssize_t ret, nb;
			ret = iio_buffer_refill(dev_info->buffer);
			if (ret >= 0)
				ret = iio_buffer_foreach_sample(
					dev_info->buffer, demux_sample, NULL);
			if (ret < 0) {
				fprintf(stderr, "Error while reading data: %s\n", strerror(-ret));
				stop_capture = TRUE;
				goto capture_stop_check;
			}

			nb = ret / sample_size;
			sample_count = (sample_count < nb) ? 0 : sample_count - nb;
		} while (sample_count);

		if (dev_info->channels_data_copy) {
			for (i = 0; i < nb_channels; i++) {
				struct iio_channel *ch = iio_device_get_channel(dev, i);
				struct extra_info *info = iio_channel_get_data(ch);
				memcpy(dev_info->channels_data_copy[i], info->data_ref,
					dev_info->sample_count * sizeof(gfloat));
			}
			dev_info->channels_data_copy = NULL;
			G_UNLOCK(buffer_full);
		}

		if (device_is_oneshot(dev)) {
			iio_buffer_destroy(dev_info->buffer);
			dev_info->buffer = NULL;
		}
	}

	update_all_plots();

capture_stop_check:
	if (stop_capture == TRUE)
		capture_function = 0;

	return !stop_capture;
}

static unsigned int max_sample_count_from_plots(struct extra_dev_info *info)
{
	unsigned int max_count = 0;
	struct plot_params *prm;
	GSList *node;
	GSList *list = info->plots_sample_counts;

	for (node = list; node; node = g_slist_next(node)) {
		prm = node->data;
		if (prm->sample_count > max_count)
			max_count = prm->sample_count;
	}

	return max_count;
}

static double read_sampling_frequency(const struct iio_device *dev)
{
	double freq = 400.0;
	int ret = -1;
	unsigned int i, nb_channels = iio_device_get_channels_count(dev);
	char buf[1024];

	for (i = 0; i < nb_channels; i++) {
		struct iio_channel *ch = iio_device_get_channel(dev, i);

		if (iio_channel_is_output(ch) || strncmp(iio_channel_get_id(ch),
					"voltage", sizeof("voltage") - 1))
			continue;

		ret = iio_channel_attr_read(ch, "sampling_frequency",
				buf, sizeof(buf));
		if (ret > 0)
			break;
	}

	if (ret < 0)
		ret = iio_device_attr_read(dev, "sampling_frequency",
				buf, sizeof(buf));
	if (ret < 0) {
		const struct iio_device *trigger;
		ret = iio_device_get_trigger(dev, &trigger);
		if (!ret)
			ret = iio_device_attr_read(trigger, "frequency",
					buf, sizeof(buf));
	}

	if (ret > 0)
		sscanf(buf, "%lf", &freq);

	if (freq < 0)
		freq += 4294967296.0;

	return freq;
}

static int capture_setup(void)
{
	unsigned int i, j;
	unsigned int min_timeout = 1000;
	unsigned int timeout;
	double freq;

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct extra_dev_info *dev_info = iio_device_get_data(dev);
		unsigned int nb_channels = iio_device_get_channels_count(dev);
		unsigned int sample_size, sample_count = max_sample_count_from_plots(dev_info);

		for (j = 0; j < nb_channels; j++) {
			struct iio_channel *ch = iio_device_get_channel(dev, j);
			struct extra_info *info = iio_channel_get_data(ch);
			if (info->shadow_of_enabled > 0)
				iio_channel_enable(ch);
			else
				iio_channel_disable(ch);
		}

		sample_size = iio_device_get_sample_size(dev);
		if (sample_size == 0 || sample_count == 0)
			continue;

		for (j = 0; j < nb_channels; j++) {
			struct iio_channel *ch = iio_device_get_channel(dev, j);
			struct extra_info *info = iio_channel_get_data(ch);

			if (info->data_ref)
				g_free(info->data_ref);
			info->data_ref = (gfloat *) g_new0(gfloat, sample_count);
		}

		if (dev_info->buffer)
			iio_buffer_destroy(dev_info->buffer);

		if (!device_is_oneshot(dev)) {
			dev_info->buffer = iio_device_create_buffer(dev, sample_count, false);
			if (!dev_info->buffer) {
				fprintf(stderr, "Unable to create buffer\n");
				return -1;
			}
		}
		dev_info->sample_count = sample_count;

		iio_device_set_data(dev, dev_info);

		freq = read_sampling_frequency(dev);
		if (freq > 0) {
			/* 2 x capture time + 1s */
			timeout = 2 * sample_count * 1000 / freq;
			timeout += 1000;
			if (timeout > min_timeout)
				min_timeout = timeout;
		}
	}

	if (ctx)
		iio_context_set_timeout(ctx, min_timeout);

	return 0;
}

static void capture_start(void)
{
	if (capture_function) {
		stop_capture = FALSE;
	}
	else {
		stop_capture = FALSE;
		capture_function = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 50, (GSourceFunc) capture_process, NULL, NULL);
	}
}

static void start(OscPlot *plot, gboolean start_event)
{
	if (start_event) {
		num_capturing_plots++;
		/* Stop the capture process to allow settings to be updated */
		stop_capture = TRUE;

		G_TRYLOCK(buffer_full);

		/* Start the capture process */
		capture_setup();
		capture_start();
		restart_all_running_plots();
	} else {
		G_TRYLOCK(buffer_full);
		G_UNLOCK(buffer_full);
		num_capturing_plots--;
		if (num_capturing_plots == 0) {
			stop_capture = TRUE;
			close_active_buffers();
		}
	}
}

static void plot_destroyed_cb(OscPlot *plot)
{
	plot_list = g_list_remove(plot_list, plot);
	stop_sampling();
	capture_setup();
	if (num_capturing_plots)
		capture_start();
	restart_all_running_plots();
}

static void new_plot_created_cb(OscPlot *plot, OscPlot *new_plot, gpointer data)
{
	plot_init(GTK_WIDGET(new_plot));
}

static void plot_init(GtkWidget *plot)
{
	plot_list = g_list_append(plot_list, plot);
	g_signal_connect(plot, "osc-capture-event", G_CALLBACK(start), NULL);
	g_signal_connect(plot, "osc-destroy-event", G_CALLBACK(plot_destroyed_cb), NULL);
	g_signal_connect(plot, "osc-newplot-event", G_CALLBACK(new_plot_created_cb), NULL);
	gtk_widget_show(plot);
}

static GtkWidget * new_plot_cb(GtkMenuItem *item, gpointer user_data)
{
	GtkWidget *new_plot;

	new_plot = osc_plot_new();
	osc_plot_set_visible(OSC_PLOT(new_plot), true);
	plot_init(new_plot);

	return new_plot;
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

	if(!dev_name)
		return NULL;

	for (i = 0; i < num_check_fcts; i++)
		if (strcmp(dev_name, setup_check_functions[i].dev_name) == 0)
			return setup_check_functions[i].fct_pointer;

	return NULL;
}

static void free_setup_check_fct_list(void)
{
	int i;

	for (i = 0; i < num_check_fcts; i++) {
		g_free(setup_check_functions[i].dev_name);
	}
	g_free(setup_check_functions);
}

#define DEFAULT_PROFILE_NAME ".osc_profile.ini"

void application_quit (void)
{
	const char *home_dir = getenv("HOME");
	char buf[1024];

	/* Before we shut down, let's save the profile */
	sprintf(buf, "%s/%s", home_dir, DEFAULT_PROFILE_NAME);
	capture_profile_save(buf);

	stop_capture = TRUE;
	G_TRYLOCK(buffer_full);
	G_UNLOCK(buffer_full);
	close_active_buffers();

	g_list_free(plot_list);
	free_setup_check_fct_list();

	if (gtk_main_level())
		gtk_main_quit();

	if (ctx)
		iio_context_destroy(ctx);

	/* This can't be done until all the windows are detroyed with main_quit
	 * otherwise, the widgets need to be updated, but they don't exist anymore
	 */
	close_plugins(buf);
	g_slist_free(dplugin_list);
}

void sigterm (int signum)
{
	application_quit();
}

/*
 * Check if a device has scan elements and if it is an output device (type = 0)
 * or an input device (type = 1).
 */
static bool device_type_get(const struct iio_device *dev, int type)
{
	struct iio_channel *ch;
	int nb_channels, i;

	if (!dev)
		return false;

	nb_channels = iio_device_get_channels_count(dev);
	for (i = 0; i < nb_channels; i++) {
		ch = iio_device_get_channel(dev, i);
		if (iio_channel_is_scan_element(ch) &&
			(type ? !iio_channel_is_output(ch) : iio_channel_is_output(ch)))
			return true;
	}

	return false;
}

bool is_input_device(const struct iio_device *dev)
{
	return device_type_get(dev, 1);
}

bool is_output_device(const struct iio_device *dev)
{
	return device_type_get(dev, 0);
}

static void init_device_list(void)
{
	unsigned int i, j;

	ctx = osc_create_context();
	if (!ctx)
		return;

	num_devices = iio_context_get_devices_count(ctx);

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		unsigned int nb_channels = iio_device_get_channels_count(dev);
		struct extra_dev_info *dev_info = calloc(1, sizeof(*dev_info));
		iio_device_set_data(dev, dev_info);
		dev_info->input_device = is_input_device(dev);

		for (j = 0; j < nb_channels; j++) {
			struct iio_channel *ch = iio_device_get_channel(dev, j);
			struct extra_info *info = calloc(1, sizeof(*info));
			info->dev = dev;
			iio_channel_set_data(ch, info);
		}
	}
}

#define ENTER_KEY_CODE 0xFF0D

gboolean save_sample_count_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	if (event->keyval == ENTER_KEY_CODE) {
		g_signal_emit_by_name(widget, "response", GTK_RESPONSE_OK, 0);
	}

	return FALSE;
}

static float get_rx_lo_freq(const char *dev_name)
{
	struct iio_channel *chn;
	double lo_freq = 0.0;
	struct iio_device *dev = iio_context_find_device(ctx, dev_name);
	if (!dev)
		return (float) lo_freq;

	chn = iio_device_find_channel(dev, "altvoltage0", true);
	if (!chn)
		return (float) lo_freq;

	iio_channel_attr_read_double(chn, "frequency", &lo_freq);
	return (float) lo_freq;
}

void rx_update_labels(void)
{
	unsigned int i;

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct extra_dev_info *info = iio_device_get_data(dev);
		const char *name = iio_device_get_name(dev);

		info->lo_freq = 0.0;
		info->adc_freq = read_sampling_frequency(dev);

		if (info->adc_freq >= 1000000) {
			info->adc_scale = 'M';
			info->adc_freq /= 1000000.0;
		} else if (info->adc_freq >= 1000) {
			info->adc_scale = 'k';
			info->adc_freq /= 1000.0;
		} else if (info->adc_freq >= 0) {
			info->adc_scale = ' ';
		} else {
			info->adc_scale = '?';
			info->adc_freq = 0.0;
		}

		if (!name)
			continue;

		if (!strcmp(name, "cf-ad9463-core-lpc") ||
			!strcmp(name, "axi-ad9652-lpc"))
			info->lo_freq = get_rx_lo_freq("adf4351-rx-lpc");
		else if (!strcmp(name, "cf-ad9361-lpc") ||
				!strcmp(name, "cf-ad9361-A") ||
				!strcmp(name, "cf-ad9361-B"))
			info->lo_freq = get_rx_lo_freq("ad9361-phy");

		info->lo_freq /= 1000000.0;
	}

	GList *node;

	for (node = plot_list; node; node = g_list_next(node))
		osc_plot_update_rx_lbl(OSC_PLOT(node->data), NORMAL_UPDATE);
}

/* Before we really start, let's load the last saved profile */
static bool check_inifile(char *filepath)
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

	if (!strstr(buf, "["OSC_INI_SECTION"]"))
		return FALSE;

	return TRUE;
}

static int load_default_profile (char *filename)
{
	const char *home_dir = getenv("HOME");
	char buf[1024], tmp[1024];
	int ret = 0, linecount, flag = 0;
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
	 * return success, so we still run */
		if (!check_inifile(buf))
			return 0;
		flag = 1;
	}

	if (flag)
		return 0;

	if (ret > 0) {
		sprintf(tmp, TMP_INI_FILE, get_filename_from_path(buf));
		fd = fopen(tmp, "r");
		if (!fd)
			return 0;

		linecount = 0;
		while (NULL != fgets(tmp, 1023, fd)) {
			linecount++;
			if (linecount == ret) {
				tmp[strlen(tmp) - 1] = 0;
				if (!strcmp(tmp, "stop = 1")) {
					return 0;
				}
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

static void plugins_get_preferred_size(GSList *plist, int *width, int *height)
{
	GSList *node;
	struct osc_plugin *p;
	int w, h, max_w = -1, max_h = -1;

	for (node = plist; node; node = g_slist_next(node)) {
		p = node->data;
		if (p->get_preferred_size) {
			p->get_preferred_size(&w, &h);
			if (w > max_w)
				max_w = w;
			if (h > max_h)
				max_h = h;
		}
	}
	*width = max_w;
	*height = max_h;
}

static void window_size_readjust(GtkWindow *window, int width, int height)
{
	int w = 640, h = 480;

	if (width > w)
		w = width;
	if (height > h)
		h = height;
	gtk_window_set_default_size(window, w, h);
}

static void create_default_plot(void)
{
	if (g_list_length(plot_list) == 0)
		new_plot_cb(NULL, NULL);
}

void tooltips_enable_cb (GtkCheckMenuItem *item, gpointer data)
{
	gboolean enable;
	GdkScreen *screen;
	GtkSettings *settings;

	screen = gtk_window_get_screen(GTK_WINDOW(main_window));
	settings = gtk_settings_get_for_screen(screen);
	enable = gtk_check_menu_item_get_active(item);
	g_object_set(settings, "gtk-enable-tooltips", enable, NULL);
}

static void init_application (const char *ini_fn)
{
	GtkBuilder *builder = NULL;
	GtkWidget  *window;
	GtkWidget  *btn_capture;

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "./osc.glade", NULL)) {
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "osc.glade", NULL);
	} else {
		GtkImage *logo;
		GtkAboutDialog *about;
		GdkPixbuf *pixbuf;
		GError *err = NULL;

		/* We are running locally, so load the local files */
		logo = GTK_IMAGE(gtk_builder_get_object(builder, "about_ADI_logo"));
		g_object_set(logo, "file","./icons/ADIlogo.png", NULL);
		logo = GTK_IMAGE(gtk_builder_get_object(builder, "about_IIO_logo"));
		g_object_set(logo, "file","./icons/IIOlogo.png", NULL);
		about = GTK_ABOUT_DIALOG(gtk_builder_get_object(builder, "About_dialog"));
		logo = GTK_IMAGE(gtk_builder_get_object(builder, "image_capture"));
		g_object_set(logo, "file","./icons/osc_capture.png", NULL);
		logo = GTK_IMAGE(gtk_builder_get_object(builder, "image_generator"));
		g_object_set(logo, "file","./icons/osc_generator.png", NULL);
		pixbuf = gdk_pixbuf_new_from_file("./icons/osc128.png", &err);
		if (pixbuf) {
			g_object_set(about, "logo", pixbuf,  NULL);
			g_object_unref(pixbuf);
		}
	}

	window = GTK_WIDGET(gtk_builder_get_object(builder, "main_menu"));
	notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook"));
	btn_capture = GTK_WIDGET(gtk_builder_get_object(builder, "new_capture_plot"));
	tooltips_en = GTK_WIDGET(gtk_builder_get_object(builder, "menuitem_tooltips_en"));
	main_window = window;

	/* Connect signals. */
	g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(application_quit), NULL);
	g_signal_connect(G_OBJECT(btn_capture), "activate", G_CALLBACK(new_plot_cb), NULL);
	g_signal_connect(G_OBJECT(tooltips_en), "toggled", G_CALLBACK(tooltips_enable_cb), NULL);

	dialogs_init(builder);
	init_device_list();
	if (ctx) {
		load_plugins(notebook, ini_fn);
		rx_update_labels();

		int width = -1, height = -1;
		plugins_get_preferred_size(plugin_list, &width, &height);
		window_size_readjust(GTK_WINDOW(window), width, height);
	}
	gtk_widget_show(window);
}

static char *prev_section;

/*
 * Check for settings in sections [MultiOsc_Capture_Configuration1,2,..]
 * Handler should return nonzero on success, zero on error.
 */
int capture_profile_handler(const char *section, const char *name, const char *value)
{
	static GtkWidget *plot = NULL;
	int ret = 1;

	/* Check if a new section has been reached */
	if (strcmp(section, prev_section) != 0) {
		g_free(prev_section);
		/* Remember the last section */
		prev_section = g_strdup(section);
		/* Create a capture window and parse the line from ini file*/
		if (strncmp(section, CAPTURE_INI_SECTION, strlen(CAPTURE_INI_SECTION)) == 0) {
			plot = new_plot_cb(NULL, NULL);
			osc_plot_set_visible(OSC_PLOT(plot), false);
			ret = osc_plot_ini_read_handler(OSC_PLOT(plot), section, name, value);
		}
	} else {
		/* Parse the line from ini file */
		if (strncmp(section, CAPTURE_INI_SECTION, strlen(CAPTURE_INI_SECTION)) == 0) {
			ret = osc_plot_ini_read_handler(OSC_PLOT(plot), section, name, value);
		}
	}

	return ret;
}

/*
 * Check for settings in the section for the main application
 */
int main_profile_handler(const char *section, const char *name, const char *value)
{
	int elem_type;
	gchar **elems = NULL;

	elem_type = count_char_in_string('.', name);
	switch(elem_type) {
		case 0:
			if (!strcmp(name, "window_x_pos")) {
				if (value)
					if (atoi(value)) {
						window_x_pos = atoi(value);
						gtk_window_move(GTK_WINDOW(main_window), window_x_pos, window_y_pos);
					}
			} else if (!strcmp(name, "window_y_pos")) {
				if (value)
					if (atoi(value)) {
						window_y_pos = atoi(value);
						gtk_window_move(GTK_WINDOW(main_window), window_x_pos, window_y_pos);
					}
			} else if (!strcmp(name, "tooltips_enable")) {
				if (value)
					gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(tooltips_en), !!atoi(value));
			} else {
				goto unhandled;
			}
			break;
		case 2:
			elems = g_strsplit(name, ".", 3);
			if (!strcmp(elems[0], "plugin")) {
				if (plugin_restore_ini_state(elems[1], elems[2], value) < 0)
					goto unhandled;
			} else {
				goto unhandled;
			}
			break;
		default:
			goto unhandled;
	};

	return 1;

unhandled:
	printf("Unhandled tokens in ini file, \n"
		"\tSection %s\n\tAttribute : %s\n\tValue: %s\n",
		section, name, value);

	return 0;
}

static void gfunc_save_plot_data_to_ini(gpointer data, gpointer user_data)
{
	OscPlot *plot = OSC_PLOT(data);
	char *filename = (char *)user_data;

	osc_plot_save_to_ini(plot, filename);
}

void capture_profile_save(const char *filename)
{
	FILE *fp;

	/* Create(or empty) the file. The plots will append data to the file.*/
	fp = fopen(filename, "w");
	if (!fp) {
		fprintf(stderr, "Failed to open %s : %s\n", filename, strerror(errno));
		return;
	}
	/* Create the section for the main application*/
	fprintf(fp, "[%s]\n", OSC_INI_SECTION);

	/* Save plugin attached status */
	g_slist_foreach(dplugin_list, plugin_state_ini_save, fp);

	/* Save main window position */
	gint x_pos, y_pos;
	gtk_window_get_position(GTK_WINDOW(main_window), &x_pos, &y_pos);
	fprintf(fp, "window_x_pos=%d\n", x_pos);
	fprintf(fp, "window_y_pos=%d\n", y_pos);

	fprintf(fp, "tooltips_enable=%d\n", gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(tooltips_en)));

	fclose(fp);

	/* All opened "Capture" windows save their own configurations */
	g_list_foreach(plot_list, gfunc_save_plot_data_to_ini, (gpointer)filename);
}

static gint plugin_names_cmp(gconstpointer a, gconstpointer b)
{
	struct detachable_plugin *p = (struct detachable_plugin *)a;
	char *key = (char *)b;

	return strcmp(p->plugin->name, key);
}

static int plugin_restore_ini_state(char *plugin_name, char *attribute, const char *value)
{
	struct detachable_plugin *dplugin;
	GSList *found_plugin;
	GtkWindow *plugin_window;
	GtkWidget *button;
	int ret = 0;

	found_plugin = g_slist_find_custom(dplugin_list,
		(gconstpointer)plugin_name, plugin_names_cmp);
	if (found_plugin == NULL)
		return 0;

	dplugin = found_plugin->data;
	button = dplugin->detach_attach_button;
	if (!strcmp(attribute, "detached")) {
		if (value) {
			bool detached = atoi(value);
			if ((dplugin->detached_state) ^ (detached))
				g_signal_emit_by_name(button, "clicked", dplugin);
		} else {
			ret = -1;
		}
	} else if (!strcmp(attribute, "x_pos")) {
		plugin_window = GTK_WINDOW(gtk_widget_get_toplevel(button));
		if (value) {
			if (dplugin->detached_state == true) {
				dplugin->xpos = atoi(value);
				gtk_window_move(plugin_window, dplugin->xpos, dplugin->ypos);
			}
		} else
			ret = -1;
	} else if (!strcmp(attribute, "y_pos")) {
		plugin_window = GTK_WINDOW(gtk_widget_get_toplevel(button));
		if (value) {
			if (dplugin->detached_state == true) {
				dplugin->ypos = atoi(value);
				gtk_window_move(plugin_window, dplugin->xpos, dplugin->ypos);
			}
		} else
			ret = -1;
	} else {
		ret = -1;
	}

	return ret;
}

void main_setup_before_ini_load(void)
{
	close_all_plots();
	destroy_all_plots();
	prev_section = strdup("");
}

void main_setup_after_ini_load(void)
{
	g_free(prev_section);
}

void usage(char *program)
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

gint main (int argc, char **argv)
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

	gdk_threads_init();
	gtk_init(&argc, &argv);

	signal(SIGTERM, sigterm);
	signal(SIGINT, sigterm);
	signal(SIGHUP, sigterm);

	if (profile && strcmp(profile, "-"))
		profile = NULL;

	if (profile) {
		char buf[1024];
		strncpy(buf, profile, sizeof(buf));
		profile = check_inifile(buf) ? strdup(buf) : NULL;
	}

	if (!profile) {
		char buf[1024];
		snprintf(buf, sizeof(buf), "%s/" DEFAULT_PROFILE_NAME,
				getenv("HOME"));
		if (check_inifile(buf))
			profile = strdup(buf);
	}

	gdk_threads_enter();
	init_application(profile);
	c = load_default_profile(profile);
	create_default_plot();
	if (c == 0)
		gtk_main();
	else
		application_quit();

	gdk_threads_leave();

	if (profile)
	    free(profile);

	if (c == 0 || c == -ENOTTY)
		return 0;
	else
		return -1;
}

struct iio_context * osc_create_context(void)
{
	char *host = getenv("OSC_REMOTE");
	if (host)
		return iio_create_network_context(host);
	else
		return iio_create_local_context();
}
