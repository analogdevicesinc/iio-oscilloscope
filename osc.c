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

#include <fftw3.h>

#include "oscplot.h"
#include "datatypes.h"
#include "int_fft.h"
#include "config.h"
#include "osc_plugin.h"
#include "osc.h"

extern char dev_dir_name[512];
struct _device_list *device_list = NULL;
unsigned num_devices = 0;
gint capture_function_id = 0;
static GList *plot_list = NULL;
static const char *current_device;
static int num_capturing_plots;
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

static double win_hanning(int j, int n)
{
	double a = 2.0 * M_PI / (n - 1), w;

	w = 0.5 * (1.0 - cos(a * j));

	return (w);
}

static void do_fft(Transform *tr)
{
	struct _fft_settings *settings = tr->settings;
	struct _fft_alg_data *fft = &settings->fft_alg_data;
	gfloat *in_data = *tr->in_data;
	gfloat *out_data = tr->y_axis;
	unsigned int fft_size = settings->fft_size;
	unsigned int m = fft_size / 2;
	int i, j, k;
	int cnt;
	gfloat mag;
	double avg, pwr_offset;

	gfloat *markX = settings->markX;
	gfloat *markY = settings->markY;
	unsigned int maxx[MAX_MARKERS + 1];
	gfloat maxY[MAX_MARKERS + 1];

	if ((fft->cached_fft_size == -1) || (fft->cached_fft_size != fft_size)) {

		if (fft->cached_fft_size != -1) {
			fftw_destroy_plan(fft->plan_forward);
			fftw_free(fft->in);
			fftw_free(fft->win);
			fftw_free(fft->out);
		}

		fft->in = fftw_malloc(sizeof(double) * fft_size);
		fft->win = fftw_malloc(sizeof(double) * fft_size);
		fft->out = fftw_malloc(sizeof(fftw_complex) * (m + 1));
		fft->plan_forward = fftw_plan_dft_r2c_1d(fft_size, fft->in, fft->out, FFTW_ESTIMATE);

		for (i = 0; i < fft_size; i ++)
			fft->win[i] = win_hanning(i, fft_size);

		fft->cached_fft_size = fft_size;
	}

	for (cnt = 0, i = 0; i < fft_size; i++) {
		/* normalization and scaling see fft_corr */
		fft->in[cnt] = in_data[i] * fft->win[cnt];
		cnt++;
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

	for (i = 0; i < m; ++i) {
		mag = 10 * log10((fft->out[i][0] * fft->out[i][0] +
				fft->out[i][1] * fft->out[i][1]) / (m * m)) +
			fft->fft_corr +
			pwr_offset;
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
		if (MAX_MARKERS && tr->has_the_marker) {
			if (i == 0) {
				maxx[0] = 0;
				maxY[0] = out_data[0];
			} else {
				for (j = 0; j <= MAX_MARKERS; j++) {
					if  ((out_data[i - 1] > maxY[j]) &&
						((!((out_data[i - 2] > out_data[i - 1]) &&
						 (out_data[i - 1] > out_data[i]))) &&
						 (!((out_data[i - 2] < out_data[i - 1]) &&
						 (out_data[i - 1] < out_data[i]))))) {
						for (k = MAX_MARKERS; k > j; k--) {
							maxY[k] = maxY[k - 1];
							maxx[k] = maxx[k - 1];
						}
						maxY[j] = out_data[i - 1];
						maxx[j] = i - 1;
						break;
					}
				}
			}
		}
	}
	if (MAX_MARKERS && tr->has_the_marker)
		for (j = 0; j <= MAX_MARKERS; j++) {
			markX[j] = (gfloat)tr->x_axis[maxx[j]];
			markY[j] = (gfloat)out_data[maxx[j]];
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
		tr->y_axis = *tr->in_data;

		return;
	}
}

void fft_transform_function(Transform *tr, gboolean init_transform)
{
	struct extra_info *ch_info = tr->channel_parent->extra_field;
	struct _fft_settings *settings = tr->settings;
	struct _device_list *device = ch_info->device_parent;
	unsigned axis_length = settings->fft_size / 2;
	unsigned num_samples = device->sample_count;
	int i;

	if (init_transform) {		
		Transform_resize_x_axis(tr, axis_length);
		Transform_resize_y_axis(tr, axis_length);
		tr->y_axis_size = axis_length;
		for (i = 0; i < axis_length; i++) {
			tr->x_axis[i] = i * device->adc_freq / num_samples;
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

static void gfunc_restart_plot(gpointer data, gpointer user_data)
{
	GtkWidget *plot = data;
	
	osc_plot_restart(OSC_PLOT(plot));
}

static void update_all_plots(void)
{
	g_list_foreach(plot_list, gfunc_update_plot, NULL);
}

static void restart_all_running_plots(void)
{
	g_list_foreach(plot_list, gfunc_restart_plot, NULL);
}

static int sign_extend(unsigned int val, unsigned int bits)
{
	unsigned int shift = 32 - bits;
	return ((int )(val << shift)) >> shift;
}

static void demux_data_stream(void *data_in, unsigned int num_samples,
	unsigned int offset, unsigned int data_out_size,
	struct iio_channel_info *channels, unsigned int num_channels)
{
	struct extra_info *ch_info;
	unsigned int i, j, n;
	unsigned int val;
	gfloat *ch_data;
	
	for (i = 0; i < num_samples; i++) {
		n = (offset + i) % data_out_size;
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
			ch_info = channels[j].extra_field;
			ch_data = (gfloat *)ch_info->data_ref;
			if (channels[j].is_signed)
				ch_data[n] = sign_extend(val, channels[j].bits_used);
			else
				ch_data[n] = val;
		}
	}
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

static void close_active_buffers(void)
{
	int i;
	
	for (i = 0; i < num_devices; i++)
		if (device_list[i].buffer_fd >= 0) {
			current_device = device_list[i].device_name;
			buffer_close(device_list[i].buffer_fd);
			device_list[i].buffer_fd = -1;
		}
}

static void abort_sampling(void)
{
	close_active_buffers();
	/* !!! The line that follows should be replaced with a stop function for all plots !!! */
	//gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(capture_button), FALSE);
}

unsigned int set_channel_attr_enable(const char *device_name, struct iio_channel_info *channel, unsigned enable)
{
	char buf[512];
	FILE *f;
	int ret;
	
	set_dev_paths(device_name);
	snprintf(buf, sizeof(buf), "%s/scan_elements/%s_en", dev_dir_name, channel->name);
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

static bool is_oneshot_mode(void)
{
	if (strncmp(current_device, "cf-ad9", 5) == 0)
		return true;
	
	return false;
}

static int sample_iio_data_continuous(int buffer_fd, struct buffer *buf)
{
	int ret;
	
	ret = read(buffer_fd, buf->data + buf->available, buf->size - buf->available);
	if (ret <= 0)
		return ret;
		
	buf->available += ret;
	
	return 0;
}

static  int sample_iio_data_oneshot(struct buffer *buf)
{
	int fd, ret;
	
	fd = buffer_open(buf->size);
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
		
	return ret;
}

/*
 * helper functions for plugins which want to look at data
 */
int plugin_data_capture_size(void)
{
	//return data_buffer.size;
}

int plugin_data_capture_num_active_channels(void)
{
	//return num_active_channels;
}

int plugin_data_capture_bytes_per_sample(void)
{
	//return bytes_per_sample;
}

void plugin_data_capture_demux(void *buf, gfloat **cooked, unsigned int num_samples,
	unsigned int num_channels)

{
	//demux_data_stream(buf, cooked, num_samples, 0, num_samples, channels, num_channels);
}

int plugin_data_capture(void *buf)
{
	/* only one consumer at a time */
	//if (data_buffer.data_copy)
		//return false;

	//data_buffer.data_copy = buf;
	//return true;
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
}

static gboolean capture_function(void)
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
		demux_data_stream(device_list[i].data_buffer.data, n, device_list[i].current_sample,
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
	usleep(50000);
	
	return TRUE;
}

static void resize_device_data(struct _device_list *device)
{
	struct iio_channel_info *channel;
	struct extra_info *ch_info;
	unsigned enable;
	int i;
	
	/* Check the enable status of the channels from all windows and update the channels enable attributes. */
	device->bytes_per_sample = 0;
	for (i = 0; i < device->num_channels; i++) {
		channel = &device->channel_list[i];
		ch_info = channel->extra_field;
		enable = (ch_info->shadow_of_enabled > 0) ? 1 : 0;
		channel->enabled = enable;
		enable = set_channel_attr_enable(device->device_name, channel, enable);
		if (enable) {
			device->bytes_per_sample += channel->bytes;
		}		
	}

	/* Reallocate memory for the active channels of the device */
	device->data_buffer.size = device->sample_count * device->bytes_per_sample;
	device->data_buffer.data = g_renew(int8_t, device->data_buffer.data, device->data_buffer.size);
	device->data_buffer.available = 0;
	device->current_sample = 0;
	for (i = 0; i < device->num_channels; i++) {
		channel = &device->channel_list[i];
		if (channel->enabled) {
			ch_info = (struct extra_info *)channel->extra_field;
			if (ch_info->data_ref)
				g_free(ch_info->data_ref);
			ch_info->data_ref = (gfloat *)g_new0(gfloat, device->sample_count);
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
			device_list[i].buffer_fd = buffer_open(device_list[i].sample_count);
			if (device_list[i].buffer_fd < 0)
				return -1;
		}
	}
		
	return 0;
}

static void capture_start(void)
{
	capture_function_id = g_idle_add((GSourceFunc) capture_function, NULL);
}

static void start(OscPlot *plot, gboolean start_event, gpointer databox)
{	
	if (start_event) {
		num_capturing_plots++;
		/* Stop the capture process to allow settings to be updated */
		if (capture_function_id > 0) {
			g_source_remove(capture_function_id);
			capture_function_id = 0;
		}
		close_active_buffers();
		
		/* Start the capture process */
		capture_setup();
		capture_start();
		restart_all_running_plots();
	} else {
		num_capturing_plots--;
		if (num_capturing_plots == 0)
			if (capture_function_id > 0) {
			g_source_remove(capture_function_id);
			capture_function_id = 0;
		}
	}
}

static void btn_capture_cb(GtkButton *button, gpointer user_data)
{
	GtkWidget *plot;
	
	plot = osc_plot_new();
	plot_list = g_list_append(plot_list, plot);
	g_signal_connect(plot, "capture-event", G_CALLBACK(start), NULL);
	gtk_widget_show(plot);
}

void application_quit (void)
{
	if (capture_function_id > 0) {
		g_source_remove(capture_function_id);
		capture_function_id = 0;
	}
	close_active_buffers();
	
	g_list_free(plot_list);
	
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
	
	if (response_id == 1) { /* OK button was pressed */
		sample_count = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_sample_count"));
		dev_list->shadow_of_sample_count = gtk_adjustment_get_value(sample_count);
	}
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
	
	ret = build_channel_array(dev_dir_name, &dev_list[n].channel_list, &dev_list[n].num_channels);
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
	dev_list[n].sample_count = 400;
	dev_list[n].shadow_of_sample_count = 400;
	dev_list[n].buffer_fd = -1;
		
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
	
	/* Disable all channels and parent references to channels*/
	for (i = 0;  i < num_devices; i++) {
		for (j = 0; j < device_list[i].num_channels; j++) {
			channel = &device_list[i].channel_list[j];
			set_channel_attr_enable(device_list[i].device_name, channel, 0);
			channel->enabled = 0;
			((struct extra_info *)channel->extra_field)->device_parent = &device_list[i];
		}
	}
}

static void create_sample_count_dialogs(void)
{
	GtkBuilder *builder = NULL;
	GError *error = NULL;    
	GtkWidget *dialog;
	int i;
	
	for (i = 0; i < num_devices; i++) {
		builder = gtk_builder_new();
		if (!gtk_builder_add_from_file(builder, "osc.glade", &error)) {
			g_warning("%s", error->message);
			g_free(error);
		}
		device_list[i].settings_dialog_builder = builder;
		dialog = GTK_WIDGET(gtk_builder_get_object(builder, "Sample_count_dialog"));
		g_signal_connect(dialog, "response", G_CALLBACK(set_sample_count_cb), &device_list[i]);
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
			sprintf(device_list[i].adc_scale, "MHz");
			device_list[i].adc_freq /= 1000000;
		} else if(device_list[i].adc_freq >= 1000) {
			sprintf(device_list[i].adc_scale, "kHz");
			device_list[i].adc_freq /= 1000;
		} else if(device_list[i].adc_freq >= 0) {
			sprintf(device_list[i].adc_scale, "Hz");
		} else {
			sprintf(device_list[i].adc_scale, "???");
			device_list[i].adc_freq = 0;
		}
	}
}

static void init_application (void)
{
	GtkBuilder *builder = NULL;
	GError     *error   = NULL;    
	GtkWidget  *window;
	GtkWidget  *notebook;
	GtkWidget  *btn_capture;
	
	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file(builder, "osc.glade", &error)) {
		g_warning("%s", error->message);
		g_free(error);
	}
	window = GTK_WIDGET(gtk_builder_get_object(builder, "main_menu"));
	notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook"));
	btn_capture = GTK_WIDGET(gtk_builder_get_object(builder, "button_capture"));

	/* Connect signals. */
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(application_quit), NULL);
	g_signal_connect(G_OBJECT(btn_capture), "clicked", G_CALLBACK(btn_capture_cb), NULL);
   
	dialogs_init(builder);
	trigger_dialog_init(builder);
	init_device_list();
	load_plugins(notebook);
	create_sample_count_dialogs();
	rx_update_labels();
	gtk_widget_show(window);	
}

gint main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	signal(SIGTERM, sigterm);
	signal(SIGINT, sigterm);
	signal(SIGHUP, sigterm);
	init_application();
	gtk_main();
	
	return 0;
}

