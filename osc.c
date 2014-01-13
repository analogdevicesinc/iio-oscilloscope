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

#include <fftw3.h>

#include "ini/ini.h"
#include "osc.h"
#include "oscplot.h"
#include "datatypes.h"
#include "int_fft.h"
#include "config.h"
#include "osc_plugin.h"

extern int count_char_in_string(char c, const char *s);

GSList *plugin_list = NULL;

struct _device_list *device_list = NULL;
unsigned num_devices = 0;
gint capture_function = 0;
gfloat plugin_fft_corr = 0.0;
static GList *plot_list = NULL;
static const char *current_device;
static int num_capturing_plots;
G_LOCK_DEFINE(buffer_full);
static gboolean stop_capture;
static struct plugin_check_fct *setup_check_functions;
static int num_check_fcts = 0;
static GSList *ini_capture_sections = NULL;
static GSList *dplugin_list = NULL;
GtkWidget  *notebook;

static void gfunc_save_plot_data_to_ini(gpointer data, gpointer user_data);
static void gfunc_create_plot_with_ini_data(gpointer data, gpointer user_data);
static void plugin_restore_ini_state(char *plugin_name, gboolean detached);

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
		ch_info = tr->channel_parent2->extra_field;
		in_data_c = ch_info->data_ref;
		for (cnt = 0, i = 0; cnt < fft_size; cnt++) {
			/* normalization and scaling see fft_corr */
			fft->in_c[cnt][0] = in_data[i] * fft->win[cnt];
			fft->in_c[cnt][1] = in_data_c[i++] * fft->win[cnt];
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
		
		mag = 10 * log10((fft->out[j][0] * fft->out[j][0] +
				fft->out[j][1] * fft->out[j][1]) / (fft->m * fft->m)) +
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
	}
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

void fft_transform_function(Transform *tr, gboolean init_transform)
{
	struct extra_info *ch_info = tr->channel_parent->extra_field;
	struct _fft_settings *settings = tr->settings;
	struct _device_list *device = ch_info->device_parent;
	unsigned axis_length;
	unsigned num_samples = device->sample_count;
	double corr;
	int i;

	if (init_transform) {
		axis_length = settings->fft_size * settings->fft_alg_data.num_active_channels / 2;
		Transform_resize_x_axis(tr, axis_length);
		Transform_resize_y_axis(tr, axis_length);
		tr->y_axis_size = axis_length;
		if (settings->fft_alg_data.num_active_channels == 2)
			corr = device->adc_freq / 2;
		else
			corr = 0;
		for (i = 0; i < axis_length; i++) {
			tr->x_axis[i] = i * device->adc_freq / num_samples - corr;
			tr->y_axis[i] = FLT_MAX;
		}
		
		/* Compute FFT normalization and scaling offset */
		settings->fft_alg_data.fft_corr = 20 * log10(2.0 / (1 << (tr->channel_parent->bits_used - 1)));
		return;
	}
	do_fft(tr);
}

void constellation_transform_function(Transform *tr, gboolean init_transform)
{
	struct extra_info *ch_info = tr->channel_parent2->extra_field;
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

static void gfunc_update_rx_lbl_plot(gpointer data, gpointer user_data)
{
	GtkWidget *plot = data;

	osc_plot_update_rx_lbl(OSC_PLOT(plot));
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

static int sign_extend(unsigned int val, unsigned int bits)
{
	unsigned int shift = 32 - bits;
	return ((int )(val << shift)) >> shift;
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

unsigned int set_channel_attr_enable(const char *device_name, struct iio_channel_info *channel, unsigned enable)
{
	char buf[512];
	FILE *f;
	int ret;
	
	set_dev_paths(device_name);
	snprintf(buf, sizeof(buf), "%s/scan_elements/%s_en", dev_name_dir(), channel->name);
	f = fopen(buf, "w");
	if (f) {
		fprintf(f, "%u\n", enable);
		fclose(f);
		
		f = fopen(buf, "r");
		ret = fscanf(f, "%u", &enable);
		if (ret != 1)
			enable = 0;
		fclose(f);
	} else {
		enable = 0;
	}
	
	return enable;
}

static void disable_all_channels(void)
{
	int i, j;
	
	for (i = 0; i < num_devices; i++)
		for (j = 0; j < device_list[i].num_channels; j++)
			set_channel_attr_enable(device_list[i].device_name, &device_list[i].channel_list[j], 0);
}

static void close_active_buffers(void)
{
	int i;
	
	for (i = 0; i < num_devices; i++)
		if (device_list[i].buffer_fd >= 0) {
			current_device = device_list[i].device_name;
			buffer_close(device_list[i].buffer_fd);
			device_list[i].buffer_fd = -1;
			device_list[i].data_buffer.data_copy = NULL;
		}
	disable_all_channels();
}

static void stop_sampling(void)
{
	stop_capture = TRUE;
	close_active_buffers();
	G_UNLOCK(buffer_full);
}

static void abort_sampling(void)
{
	stop_sampling();
	close_all_plots();
}

static bool is_oneshot_mode(void)
{
	if (strncmp(current_device, "cf-ad9", 5) == 0)
		return true;
	if (strncmp(current_device, "ad-mc-", 5) == 0)
		return true;
	
	return false;
}

static int sample_iio_data_continuous(int buffer_fd, struct buffer *buf)
{
	int ret;
	
	ret = read(buffer_fd, buf->data + buf->available, buf->size - buf->available);

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

static  int sample_iio_data_oneshot(struct buffer *buf)
{
	int fd, ret;
	
	fd = buffer_open(buf->size, 0);
	if (fd < 0)
		return fd;
	
	ret = sample_iio_data_continuous(fd, buf);
	
	buffer_close(fd);
	
	return ret;
}

static int sample_iio_data(struct _device_list *device)
{
	int ret;
	struct buffer *buf = &device->data_buffer;
	
	if (is_oneshot_mode())
		ret = sample_iio_data_oneshot(buf);
	else
		ret = sample_iio_data_continuous(device->buffer_fd, buf);
	
	if ((buf->data_copy) && (buf->available == buf->size)) {
		memcpy(buf->data_copy, buf->data, buf->size);
		buf->data_copy = NULL;
		G_UNLOCK(buffer_full);
	}
		
	return ret;
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
 */

void * plugin_get_device_by_reference(const char * device_name)
{
	int i;
	
	for (i = 0; i < num_devices; i++) {
		if (strcmp(device_name, device_list[i].device_name) == 0)
			return (void *)&device_list[i];
	}
	
	return NULL;
}

int plugin_data_capture_size(void *device)
{
	return ((struct _device_list *)device)->data_buffer.size;
}

int plugin_data_capture_num_active_channels(void *device)
{
	return ((struct _device_list *)device)->num_active_channels;
}

int plugin_data_capture_bytes_per_sample(void *device)
{
	return ((struct _device_list *)device)->bytes_per_sample;
}

void plugin_data_capture_demux(void *device, void *buf, gfloat **cooked, unsigned int num_samples,
	unsigned int num_channels)

{
	demux_data_stream(buf, cooked, num_samples, 0, num_samples,
		((struct _device_list *)device)->channel_list, num_channels);
}

int plugin_data_capture(void *device, void *buf)
{
	struct _device_list *_device = (struct _device_list *)device;
	
	/* only one consumer at a time */
	if (_device->data_buffer.data_copy)
		return false;

	_device->data_buffer.data_copy = buf;
	return true;
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

	plugin_list = g_slist_append (plugin_list, (gpointer) plugin);
	plugin->init(notebook);
	
	d_plugin = malloc(sizeof(struct detachable_plugin));
	d_plugin->plugin = plugin;
	dplugin_list = g_slist_append(dplugin_list, (gpointer)d_plugin);
	
	plugin_make_detachable(d_plugin);

	printf("Loaded plugin: %s\n", plugin->name);
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
}

static void plugin_state_ini_save(gpointer data, gpointer user_data)
{
	struct detachable_plugin *p = (struct detachable_plugin *)data;
	FILE *fp = (FILE *)user_data;
	
	fprintf(fp, "plugin.%s.detached=%d\n", p->plugin->name, p->detached_state);
}

static gboolean capture_proccess(void)
{
	unsigned int n;
	int ret;
	int i;

	for (i = 0; i < num_devices; i++) {
		if (device_list[i].bytes_per_sample == 0)
			continue;
		current_device = device_list[i].device_name;
		ret = sample_iio_data(&device_list[i]);
		if (ret < 0) {
			abort_sampling();
			fprintf(stderr, "Failed to capture samples: %s\n", strerror(-ret));
			return FALSE;
		}
	}

	for (i = 0; i < num_devices; i++) {
		if (device_list[i].bytes_per_sample == 0)
			continue;	
		
		n = device_list[i].data_buffer.available / device_list[i].bytes_per_sample;
		demux_data_stream(device_list[i].data_buffer.data, device_list[i].channel_data, n, device_list[i].current_sample,
			device_list[i].sample_count, device_list[i].channel_list, device_list[i].num_channels);
		device_list[i].current_sample = (device_list[i].current_sample + n) % device_list[i].sample_count;
		device_list[i].data_buffer.available -= n * device_list[i].bytes_per_sample;
		if (device_list[i].data_buffer.available != 0) {
			memmove(device_list[i].data_buffer.data,
				device_list[i].data_buffer.data + n * device_list[i].bytes_per_sample,
				device_list[i].data_buffer.available);
		}
	}
	
	update_all_plots();
	if (stop_capture == TRUE)
		capture_function = 0;
	
	return !stop_capture;
}

static void resize_device_data(struct _device_list *device)
{
	struct iio_channel_info *channel;
	struct extra_info *ch_info;
	unsigned enable;
	int i;
	int k;
	
	/* Check the enable status of the channels from all windows and update the channels enable attributes. */
	device->bytes_per_sample = 0;
	device->num_active_channels = 0;
	for (i = 0; i < device->num_channels; i++) {
		channel = &device->channel_list[i];
		ch_info = channel->extra_field;
		enable = (ch_info->shadow_of_enabled > 0) ? 1 : 0;
		channel->enabled = enable;
		enable = set_channel_attr_enable(device->device_name, channel, enable);
		if (enable) {
			device->bytes_per_sample += channel->bytes;
			device->num_active_channels++;
		}
	}

	/* Reallocate memory for the active channels of the device */
	device->data_buffer.size = device->sample_count * device->bytes_per_sample;
	device->data_buffer.data = g_renew(int8_t, device->data_buffer.data, device->data_buffer.size);
	device->data_buffer.available = 0;
	device->current_sample = 0;
	k = 0;
	for (i = 0; i < device->num_channels; i++) {
		channel = &device->channel_list[i];
		if (channel->enabled) {
			ch_info = (struct extra_info *)channel->extra_field;
			if (ch_info->data_ref)
				g_free(ch_info->data_ref);
			ch_info->data_ref = (gfloat *)g_new0(gfloat, device->sample_count);
			/* Copy the channel data address to <channel_data> variable */
			device->channel_data[k++] = ch_info->data_ref;
		}
	}
}

static int capture_setup(void)
{
	int i;
	
	for (i = 0; i < num_devices; i++) {
		device_list[i].sample_count = device_list[i].shadow_of_sample_count;
		resize_device_data(&device_list[i]);
		current_device = device_list[i].device_name;
		if ((!is_oneshot_mode()) && (device_list[i].bytes_per_sample != 0)) {
			device_list[i].buffer_fd = buffer_open(device_list[i].sample_count, O_NONBLOCK);
			if (device_list[i].buffer_fd < 0)
				return -1;
		}
	}
		
	return 0;
}

static void capture_start(void)
{
	if (capture_function) {
		stop_capture = FALSE;
	}
	else {
		stop_capture = FALSE;
		capture_function = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 50, (GSourceFunc) capture_proccess, NULL, NULL);
	}
}

static void start(OscPlot *plot, gboolean start_event)
{	
	int i;
	
	if (start_event) {
		num_capturing_plots++;
		/* Stop the capture process to allow settings to be updated */
		stop_capture = TRUE;
		for (i = 0; i < num_devices; i++)
			device_list[0].data_buffer.data_copy = NULL;
		G_UNLOCK(buffer_full);
		close_active_buffers();
		
		/* Start the capture process */
		G_UNLOCK(buffer_full);
		capture_setup();
		capture_start();
		restart_all_running_plots();
	} else {
		num_capturing_plots--;
		if (num_capturing_plots == 0) {
			stop_capture = TRUE;
			for (i = 0; i < num_devices; i++)
				if (!device_list[0].data_buffer.data_copy)
					device_list[0].data_buffer.data_copy = NULL;
			G_UNLOCK(buffer_full);
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

static GtkWidget * plot_create_and_init(void)
{
	GtkWidget *plot;
	
	plot = osc_plot_new();
	plot_list = g_list_append(plot_list, plot);
	g_signal_connect(plot, "osc-capture-event", G_CALLBACK(start), NULL);
	g_signal_connect(plot, "osc-destroy-event", G_CALLBACK(plot_destroyed_cb), NULL);
	gtk_widget_show(plot);
	
	return plot;
}

static void btn_capture_cb(GtkButton *button, gpointer user_data)
{
	plot_create_and_init();	
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

#define DEFAULT_PROFILE_NAME ".multiosc_profile.ini"

void application_quit (void)
{
	const char *home_dir = getenv("HOME");
	char buf[1024];
	
	/* Before we shut down, let's save the profile */
	sprintf(buf, "%s/%s", home_dir, DEFAULT_PROFILE_NAME);
	capture_profile_save(buf);
	save_all_plugins(buf, NULL);
	
	stop_capture = TRUE;
	G_UNLOCK(buffer_full);
	close_active_buffers();
	
	g_list_free(plot_list);
	g_slist_free(dplugin_list);
	free_setup_check_fct_list();
	
	gtk_main_quit();
}

void sigterm (int signum)
{
	application_quit();
}

static void set_sample_count_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	struct _device_list *dev_list = user_data;
	GtkBuilder *builder = dev_list->settings_dialog_builder;
	GtkAdjustment *sample_count;
	GtkWidget *response_btn;
	
	if (response_id == GTK_RESPONSE_OK) {
		/* By passing the focus from the spinbutton to others, the spinbutton gets updated */
		response_btn = gtk_dialog_get_widget_for_response(dialog, response_id);
		gtk_widget_grab_focus(response_btn);
		/* Get data from widget and store it */
		sample_count = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_sample_count"));
		dev_list->shadow_of_sample_count = gtk_adjustment_get_value(sample_count);
	}
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

static struct _device_list *add_device(struct _device_list *dev_list, const char *device)
{
	struct extra_info *ch_info;
	int dev_name_len;
	int ret;
	int n;
	int j;
	
	/* Add device */
	dev_list = (struct _device_list *)realloc(dev_list, sizeof(*dev_list) * num_devices);
	set_dev_paths(device);
	
	/* Init device */
	n = num_devices - 1;
	
	dev_name_len = strlen(device) + 1;
	dev_list[n].device_name = (char *)malloc(sizeof(char) * dev_name_len);
	snprintf(dev_list[n].device_name, dev_name_len, "%s", device);
	
	ret = build_channel_array(dev_name_dir(), &dev_list[n].channel_list, &dev_list[n].num_channels);
	if (ret)
		return NULL;
		
	for (j = 0; j < dev_list[n].num_channels; j++) {
		ch_info  = (struct extra_info *)malloc(sizeof(struct extra_info));
		ch_info->device_parent = NULL; /* Don't add the device parrent yet(dev_list addresses may change due to realloc) */
		ch_info->data_ref = NULL;
		ch_info->shadow_of_enabled = 0;
		dev_list[n].channel_list[j].extra_field = ch_info;
	}
	
	dev_list[n].data_buffer.data = NULL;
	dev_list[n].data_buffer.data_copy = NULL;
	dev_list[n].sample_count = 16384;
	dev_list[n].shadow_of_sample_count = 16384;
	dev_list[n].channel_data = (gfloat **)malloc(sizeof(gfloat *) * dev_list[n].num_channels);
	dev_list[n].buffer_fd = -1;
	dev_list[n].lo_freq = 0;
	
	return dev_list;
}

static void init_device_list(void)
{
	char *devices = NULL, *device;
	unsigned int num;
	struct iio_channel_info *channel;
	int i, j;
	
	num = find_iio_names(&devices, "iio:device");
	if (devices != NULL) {
		device = devices;
		for (; num > 0; num--) {
			if (is_input_device(device)) {
				num_devices++;
				device_list = add_device(device_list, device);
			}
			device += strlen(device) + 1;
		}
	}
	free(devices);
	
	/* Disable all channels. Link parent references to channels*/
	for (i = 0;  i < num_devices; i++) {
		for (j = 0; j < device_list[i].num_channels; j++) {
			channel = &device_list[i].channel_list[j];
			set_channel_attr_enable(device_list[i].device_name, channel, 0);
			channel->enabled = 0;
			((struct extra_info *)channel->extra_field)->device_parent = &device_list[i];
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

static void create_sample_count_dialogs(void)
{
	GtkBuilder *builder = NULL;
	GtkWidget *dialog;
	int i;
	
	for (i = 0; i < num_devices; i++) {
		builder = gtk_builder_new();
		if (!gtk_builder_add_from_file(builder, "./osc.glade", NULL))
			gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "multi_plot_osc.glade", NULL);
		
		device_list[i].settings_dialog_builder = builder;
		dialog = GTK_WIDGET(gtk_builder_get_object(builder, "Sample_count_dialog"));
		g_signal_connect(dialog, "response", G_CALLBACK(set_sample_count_cb), &device_list[i]);
		g_signal_connect(dialog, "key_press_event", G_CALLBACK(save_sample_count_cb), NULL);
		
		gtk_window_set_title(GTK_WINDOW(dialog), (gchar *)device_list[i].device_name);
	}
}

static double read_sampling_frequency(const char *device)
{
	double freq = 1.0;
	int ret;

	if (set_dev_paths(device) < 0)
		return -1.0f;

	if (iio_devattr_exists(device, "in_voltage_sampling_frequency")) {
		read_devattr_double("in_voltage_sampling_frequency", &freq);
		if (freq < 0)
			freq = ((double)4294967296) + freq;
	} else if (iio_devattr_exists(device, "sampling_frequency")) {
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
	int i;

	for (i = 0; i < num_devices; i++) {
		device_list[i].adc_freq = read_sampling_frequency(device_list[i].device_name);
		if (device_list[i].adc_freq >= 1000000) {
			sprintf(device_list[i].adc_scale, "M");
			device_list[i].adc_freq /= 1000000;
		} else if(device_list[i].adc_freq >= 1000) {
			sprintf(device_list[i].adc_scale, "k");
			device_list[i].adc_freq /= 1000;
		} else if(device_list[i].adc_freq >= 0) {
			sprintf(device_list[i].adc_scale, " ");
		} else {
			sprintf(device_list[i].adc_scale, "?");
			device_list[i].adc_freq = 0;
		}
		
		if (strcmp(device_list[i].device_name, "cf-ad9643-core-lpc") == 0) {
			set_dev_paths("adf4351-rx-lpc");
			read_devattr_double("out_altvoltage0_frequency", &device_list[i].lo_freq);
		} else if (strcmp(device_list[i].device_name, "cf-ad9361-lpc") == 0) {
			set_dev_paths("ad9361-phy");
			read_devattr_double("out_altvoltage0_RX_LO_frequency", &device_list[i].lo_freq);
		} else {
			device_list[i].lo_freq = 0;
		}
		if (device_list[i].lo_freq)
			device_list[i].lo_freq /= 1000000.0;
	}
	
	g_list_foreach(plot_list, gfunc_update_rx_lbl_plot, NULL);
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

	if (!strstr(buf, "[MultiOsc]"))
		return FALSE;

	return TRUE;
}

static void load_default_profile (char *filename)
{
	const char *home_dir = getenv("HOME");
	char buf[1024];
	int checkok = 0;

	if (!filename) {
		sprintf(buf, "%s/%s", home_dir, DEFAULT_PROFILE_NAME);
	} else {
		strcpy (buf, filename);
		if (!check_inifile(buf))
			sprintf(buf, "%s%s", home_dir, DEFAULT_PROFILE_NAME);
		else
			checkok = 1;
	}

	if (!checkok && !check_inifile(buf))
		return;

	printf("Loading profile : %s\n", buf);
	capture_profile_load(buf);
	restore_all_plugins(buf, NULL);
}

static void init_application (void)
{
	GtkBuilder *builder = NULL;
	GtkWidget  *window;
	GtkWidget  *btn_capture;
	
	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "./osc.glade", NULL)) {
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "multi_plot_osc.glade", NULL);
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
	btn_capture = GTK_WIDGET(gtk_builder_get_object(builder, "button_capture"));

	/* Connect signals. */
	g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(application_quit), NULL);
	g_signal_connect(G_OBJECT(btn_capture), "clicked", G_CALLBACK(btn_capture_cb), NULL);
   
	dialogs_init(builder);
	trigger_dialog_init(builder);
	init_device_list();
	load_plugins(notebook);
	create_sample_count_dialogs();
	rx_update_labels();
	gtk_widget_show(window);	
}
static char *prev_section;

static int profile_read_handler(void *user, const char *section, const char *name, const char *value)
{
	int elem_type;
	gchar **elems = NULL;
	
	/* Get data from [MultOsc] section */
	if (!strcmp(section, "MultiOsc")) {
		elem_type = count_char_in_string('.', name);
		switch(elem_type) {
			case 2:
				elems = g_strsplit(name, ".", 3);
				if (!strcmp(elems[0], "plugin")) {
					if (!strcmp(elems[2], "detached"))
						plugin_restore_ini_state(elems[1], atoi(value));
					else goto unhandled;
				} else {
					goto unhandled;
				}
				break;
				unhandled:
				printf("Unhandled token in ini file, \n"
					"\tSection %s\n\ttoken: %s\n\tvalue: %s\n",
					section, name, value);
				break;
			default:
				printf("Unhandled token in ini file, \n"
					"\tSection %s\n\ttoken: %s\n\tvalue: %s\n",
					section, name, value);
				break;
		};
		return 0;
	}
	
	/* Check if a new "Capture" section has been reached */
	if (strcmp(section, prev_section) != 0) { 
		if (strncmp(section, "MultiOsc_Capture_Configuration", strlen("MultiOsc_Capture_Configuration")) != 0)
			return 0;
		g_free(prev_section);
		/* Remember the last section */
		prev_section = g_strdup(section);
		/* Store the name of the section */
		ini_capture_sections = g_slist_append(ini_capture_sections, g_strdup(section));
	}
	
	return 0;
}

static void gfunc_save_plot_data_to_ini(gpointer data, gpointer user_data)
{
	OscPlot *plot = OSC_PLOT(data);
	char *filename = (char *)user_data;
	
	osc_plot_save_to_ini(plot, filename);
}

static void gfunc_create_plot_with_ini_data(gpointer data, gpointer user_data)
{
	GtkWidget  *plot;
	char *filename = (char *)user_data;
	char *section = (char *)data;
	
	plot = plot_create_and_init();
	osc_plot_load_ini_section(OSC_PLOT(plot), filename, section);
}

void capture_profile_save(char *filename)
{
	FILE *fp;
	
	/* Create(or empty) the file. The plots will append data to the file.*/
	fp = fopen(filename, "w");
	if (!fp) {
		fprintf(stderr, "Failed to open %s : %s\n", filename, strerror(errno));
		return;
	}
	/* Create MultiOsc Section */
	fprintf(fp, "[MultiOsc]\n");
	
	/* Save plugin attached status */
	g_slist_foreach(dplugin_list, plugin_state_ini_save, fp);
	
	fclose(fp);
	
	/* All opened "Capture" windows save their own configurations */
	g_list_foreach(plot_list, gfunc_save_plot_data_to_ini, filename);
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
	GSList *found_plugin;
	GtkWidget *button;
	printf("restoring plugin: %s\n", plugin_name);
	found_plugin = g_slist_find_custom(dplugin_list,
		(gconstpointer)plugin_name, plugin_names_cmp);
	if (found_plugin == NULL) {
		printf("Invalid plugin: %s\n", plugin_name);
		return;
	}

	dplugin = found_plugin->data;
	button = dplugin->detach_attach_button;
	if ((dplugin->detached_state) ^ (detached))
		g_signal_emit_by_name(button, "clicked", dplugin);
}

void capture_profile_load(char *filename)
{	
	if (ini_capture_sections != NULL) {
		g_slist_free(ini_capture_sections);
		ini_capture_sections = NULL;
	}
	close_all_plots();
	destroy_all_plots();
	prev_section = g_strdup("");
	ini_parse(filename, profile_read_handler, NULL);
	g_free(prev_section);
	g_slist_foreach(ini_capture_sections, gfunc_create_plot_with_ini_data, filename);
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

	g_thread_init (NULL);
	gdk_threads_init ();
	gtk_init (&argc, &argv);

	signal(SIGTERM, sigterm);
	signal(SIGINT, sigterm);
	signal(SIGHUP, sigterm);
	
	gdk_threads_enter();
	init_application();
	load_default_profile(profile);
	gtk_main();
	gdk_threads_leave();
	
	return 0;
}

