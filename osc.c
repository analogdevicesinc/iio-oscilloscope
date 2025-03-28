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
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include <iio.h>

#include "compat.h"
#include "libini2.h"
#include "osc.h"
#include "datatypes.h"
#include "config.h"
#include "osc_plugin.h"
#include "iio_utils.h"

GSList *plugin_list = NULL;

gint capture_function = 0;
static bool restart_capture = FALSE;
static GList *plot_list = NULL;
static int num_capturing_plots;
G_LOCK_DEFINE_STATIC(buffer_full);
static gboolean stop_capture;
static struct plugin_check_fct *setup_check_functions = NULL;
static int num_check_fcts = 0;
static GSList *plugin_lib_list = NULL;
static GSList *dplugin_list = NULL;
static struct osc_plugin *spect_analyzer_plugin = NULL;
static OscPreferences *osc_preferences = NULL;
GtkWidget *notebook;
GtkWidget *infobar;
GtkWidget *tooltips_en;
GtkWidget *versioncheck_en;
GtkWidget *main_window;

struct iio_context *ctx = NULL;
static unsigned int num_devices = 0;
bool ctx_destroyed_by_do_quit;
bool ini_capture_timeout_loaded = FALSE;
unsigned int ini_capture_timeout = 0;

static void gfunc_save_plot_data_to_ini(gpointer data, gpointer user_data);
static void plugin_restore_ini_state(const char *plugin_name,
		const char *attribute, int value);
static void plot_init(GtkWidget *plot);
static void plot_destroyed_cb(OscPlot *plot);
static void capture_profile_save(const char *filename);
static int load_profile(const char *filename, bool load_plugins);
static int capture_setup(void);
static void capture_start(void);
static void stop_sampling(void);

static char * dma_devices[] = {
	"ad9122",
	"ad9144",
	"ad9250",
	"ad9361",
	"ad9643",
	"ad9680",
	"ad9371"
};

#define DMA_DEVICES_COUNT (sizeof(dma_devices) / sizeof(dma_devices[0]))

#ifdef __APPLE__
const void *memrchr(const void *src, int c, size_t length)
{
	const unsigned char *cp;

	if (length != 0) {
		cp = (const unsigned char *)src + length;
		do {
			if (*(--cp) == (unsigned char)c)
				return (const void *)cp;
		} while (--length != 0);
	}
	return NULL;
}
#endif

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
	unsigned int i;

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

unsigned global_enabled_channels_mask(struct iio_device *dev)
{
	unsigned mask = 0;
	int scan_i = 0;
	unsigned int i = 0;

	for (; i < iio_device_get_channels_count(dev); i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);

		if (iio_channel_is_scan_element(chn)) {
			if (iio_channel_is_enabled(chn))
				mask |= 1 << scan_i;
			scan_i++;
		}
	}

	return mask;
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

static void update_plot(struct iio_buffer *buf)
{
	GList *node;

	for (node = plot_list; node; node = g_list_next(node)) {
		OscPlot *plot = (OscPlot *) node->data;

		if (osc_plot_get_buffer(plot) == buf) {
			osc_plot_data_update(plot);
		}
	}
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
	GtkWidget *tab_detach_btn;
	const struct osc_plugin *plugin = d_plugin->plugin;
	const char *plugin_name = plugin->name;

	tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	tab_label = gtk_label_new(plugin_name);
	// TO DO: since "gtk-disconnect" is no longer available, maybe use a custom image
	tab_detach_btn = (GtkWidget *)gtk_tool_button_new(
		gtk_image_new_from_icon_name("window-new", GTK_ICON_SIZE_SMALL_TOOLBAR), NULL);

	gtk_widget_set_size_request(tab_detach_btn, 25, 25);

	gtk_container_add(GTK_CONTAINER(tab_box), tab_label);
	gtk_container_add(GTK_CONTAINER(tab_box), tab_detach_btn);

	gtk_box_set_spacing(GTK_BOX(tab_box), 10);

	gtk_widget_show_all(tab_box);

	gtk_notebook_set_tab_label(GTK_NOTEBOOK(notebook), page, tab_box);
	g_signal_connect(tab_detach_btn, "clicked",
		G_CALLBACK(detach_plugin), (gpointer)d_plugin);

	return tab_detach_btn;
}

static void plugin_make_detachable(struct detachable_plugin *d_plugin)
{
	GtkWidget *page;
	int num_pages = 0;

	num_pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
	page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), num_pages - 1);

	d_plugin->window = NULL;
	d_plugin->detached_state = FALSE;
	d_plugin->detach_attach_button = plugin_tab_add_detach_btn(page, d_plugin);
}

static void attach_plugin(GtkWidget *window, struct detachable_plugin *d_plugin)
{
	GtkWidget *plugin_page;
	GtkWidget *detach_btn;
	const struct osc_plugin *plugin = d_plugin->plugin;
	gint plugin_page_index;

	GtkWidget *hbox;
	GList *hbox_elems;
	GList *first;

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
		plugin->update_active_page((struct osc_plugin *)plugin, plugin_page_index, FALSE);
	d_plugin->detached_state = FALSE;
	d_plugin->detach_attach_button = detach_btn;
	d_plugin->window = NULL;
}

static void debug_window_delete_cb(GtkWidget *w, GdkEvent *e, gpointer data)
{
	attach_plugin(w, (struct detachable_plugin *)data);
}

static GtkWidget * extract_label_from_box(GtkWidget *box)
{
	GList *children;
	GList *first;
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
	GtkWidget *window;
	GtkWidget *hbox;
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
		fprintf(stderr, "Could not find %s plugin in the notebook\n",
				plugin_name);
		return;
	}

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if (plugin->get_preferred_size) {
		int width = -1, height = -1;

		plugin->get_preferred_size(plugin, &width, &height);
		gtk_window_set_default_size(GTK_WINDOW(window), width, height);
	}

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	gtk_window_set_title(GTK_WINDOW(window), page_name);
	gtk_container_remove(GTK_CONTAINER(notebook), page);
	gtk_container_add(GTK_CONTAINER(hbox), page);
	gtk_container_add(GTK_CONTAINER(window), hbox);

	g_signal_connect(window, "delete-event",
			G_CALLBACK(debug_window_delete_cb), (gpointer)d_plugin);

	if (plugin->update_active_page)
		plugin->update_active_page((struct osc_plugin *)plugin, -1, TRUE);
	d_plugin->detached_state = TRUE;
	d_plugin->detach_attach_button = NULL;
	d_plugin->window = window;

	gtk_widget_show(window);
	gtk_widget_show(hbox);
}

static const char * device_name_check(const char *name)
{
	struct iio_device *dev;

	if (!name)
		return NULL;

	dev = iio_context_find_device(ctx, name);
	if (!dev)
		return NULL;

	return get_iio_device_label_or_name(dev);
}

/*
 * helper functions for plugins which want to look at data
 */

struct iio_context *get_context_from_osc(void)
{
	return ctx;
}

/*
 * Helper function to move window to valid coordinate on screen
 */
void move_gtk_window_on_screen(GtkWindow   *window,
		  gint         x,
		  gint         y)
{
	// get screen dimensions
	GdkWindow *gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
	GdkScreen *screen = gdk_window_get_screen(gdk_window);
	GdkDisplay *display = gdk_screen_get_display(screen);
	GdkMonitor *monitor = gdk_display_get_monitor_at_window(display, gdk_window);
	GdkRectangle geometry;
	gdk_monitor_get_geometry(monitor, &geometry);
	gint screen_w = geometry.width;
	gint screen_h = geometry.height;

	gint window_w;
	gint window_h;
	gtk_window_get_size(window, &window_w, &window_h);

	/* Make sure the window fully fits inside the screen area */
	if ( (x >= 0 && x + window_w <= screen_w) &&
		(y >= 0 && y + window_h <= screen_h) ) {
		gtk_window_move(window, x, y);
	} else {
		gtk_window_set_position(window, GTK_WIN_POS_CENTER);
	}
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

int plugin_data_capture_of_plot(OscPlot *plot, const char *device, gfloat ***cooked_data,
			struct marker_type **markers_cp)
{
	struct iio_device *dev, *tmp_dev = NULL;
	struct extra_dev_info *dev_info;
	struct marker_type *markers_copy = NULL;
	GMutex *markers_lock;
	unsigned int i, j;
	bool new = FALSE;
	const char *tmp = NULL;

	if (device == NULL)
		dev = NULL;
	else
		dev = iio_context_find_device(ctx, device);

	if (plot) {
		tmp = osc_plot_get_active_device(plot);
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
	if (osc_plot_running_state(plot) == FALSE)
		return -ENXIO;
	if (osc_plot_get_marker_type(plot) == MARKER_OFF ||
			osc_plot_get_marker_type(plot) == MARKER_NULL)
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

	if (plot) {
		markers_copy = (struct marker_type *)osc_plot_get_markers_copy(plot);
		markers_lock = osc_plot_get_marker_lock(plot);
	}

	if (markers_cp) {
		if (!plot) {
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
		osc_plot_set_markers_copy(plot, *markers_cp);

		/* Wait til the copy is complete */
		g_mutex_lock(markers_lock);

		/* if the lock is released, but the copy is still here
		 * that's because someone else broke the lock
		 */
		 if (markers_copy) {
			osc_plot_set_markers_copy(plot, NULL);
			 return -EINTR;
		 }
	}
 	return 0;

capture_malloc_fail:
	fprintf(stderr, "%s:%s malloc failed\n", __FILE__, __func__);
	return -ENOMEM;
}

OscPlot * plugin_get_new_plot(void)
{
	return OSC_PLOT(new_plot_cb(NULL, NULL));
}

void plugin_osc_stop_capture(void)
{
	stop_sampling();
}

void plugin_osc_start_capture(void)
{
	capture_setup();
	capture_start();
}

bool plugin_osc_running_state(void)
{
	return !!capture_function;
}

void plugin_osc_stop_all_plots(void)
{
	close_all_plots();
}

static bool force_plugin(const char *name)
{
	const char *force_plugin = getenv("OSC_FORCE_PLUGIN");
	const char *pos;

	if (!force_plugin)
		return false;

	if (strcmp(force_plugin, "all") == 0)
		return true;

#ifdef __GLIBC__
	pos = strcasestr(force_plugin, name);
#else
	pos = strstr(force_plugin, name);
#endif
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

static void close_plugins(const char *ini_fn)
{
	GSList *node;

	for (node = dplugin_list; node; node = g_slist_next(node)) {
		struct detachable_plugin *d_plugin = node->data;
		struct osc_plugin *plugin = (struct osc_plugin *)d_plugin->plugin;

		if (d_plugin->window)
			gtk_widget_destroy(d_plugin->window);

		if (plugin) {
			printf("Closing plugin: %s\n", plugin->name);
			if (plugin->destroy)
				plugin->destroy(plugin, ini_fn);

			if (plugin->dynamically_created)
				g_free(plugin);
		}

		g_free(d_plugin);
	}

	/* Closing the shared library handles after we're done wit the plugins */
	for (node = plugin_lib_list; node; node = g_slist_next(node)) {
		void *lib_handle = node->data;
		dlclose(lib_handle);
	}

	g_slist_free(dplugin_list);
	dplugin_list = NULL;

	g_slist_free(plugin_list);
	plugin_list = NULL;

	g_slist_free(plugin_lib_list);
	plugin_lib_list = NULL;
}

static void close_plugin(struct osc_plugin *plugin)
{
	plugin_list = g_slist_remove(plugin_list, plugin);
	plugin_lib_list = g_slist_remove(plugin_lib_list, plugin->handle);
	if (plugin->dynamically_created) {
		dlclose(plugin->handle);
		g_free(plugin);
	} else {
		dlclose(plugin->handle);
	}
}

static struct osc_plugin * get_plugin_from_name(const char *name)
{
	GSList *node;

	for (node = plugin_list; node; node = g_slist_next(node)) {
		struct osc_plugin *plugin = node->data;
		if (plugin && !strcmp(plugin->name, name))
			return plugin;
	}

	return NULL;
}

bool plugin_installed(const char *name)
{
	return !!get_plugin_from_name(name);
}

void * plugin_dlsym(const char *name, const char *symbol)
{
	GSList *node;
	struct osc_plugin *plugin = NULL;
	void *fcn;
	char *buf;
#ifndef __MINGW__
	Dl_info info;
#endif

	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin && !strcmp(plugin->name, name)) {
			dlerror();
			fcn = dlsym(plugin->handle, symbol);
			buf = dlerror();
			if (buf) {
				fprintf(stderr, "%s:%s(): found plugin %s, error looking up %s\n"
						"\t%s\n", __FILE__, __func__, name, symbol, buf);
#ifndef __MINGW__
				if (dladdr(__builtin_return_address(0), &info))
					fprintf(stderr, "\tcalled from %s:%s()\n", info.dli_fname, info.dli_sname);
#endif
			}
			return fcn;
		}
	}

	fprintf(stderr, "%s:%s : No plugin with matching name %s\n", __FILE__, __func__, name);
#ifndef __MINGW__
	if (dladdr(__builtin_return_address(0), &info))
		fprintf(stderr, "\tcalled from %s:%s()\n", info.dli_fname, info.dli_sname);
#endif
	return NULL;
}

void osc_plugin_context_free_resources(struct osc_plugin_context *ctx)
{
	if (ctx->plugin_name)
		g_free(ctx->plugin_name);
	if (ctx->required_devices)
		g_list_free_full(ctx->required_devices, (GDestroyNotify)g_free);
}

bool str_endswith(const char *str, const char *needle)
{
	const char *pos;
	pos = strstr(str, needle);
	if (pos == NULL)
		return false;
	return *(pos + strlen(needle)) == '\0';
}

static void * init_plugin(void *data)
{
	GtkWidget *widget;
	struct {
		struct osc_plugin *plugin;
		GtkWidget *notebook;
		const char *ini_fn;
	} *params = data;

	widget = params->plugin->init(params->plugin, params->notebook, params->ini_fn);
	free(data);
	return widget;
}

static void load_plugin_finish(GtkNotebook *notebook,
		GtkWidget *widget, struct osc_plugin *plugin)
{
	struct detachable_plugin *d_plugin;
	gint page;

	page = gtk_notebook_append_page(notebook, widget, NULL);
	gtk_notebook_set_tab_label_text(notebook, widget, plugin->name);

	if (plugin->update_active_page)
		plugin->update_active_page(plugin, page, FALSE);

	d_plugin = malloc(sizeof(*d_plugin));
	d_plugin->plugin = plugin;
	dplugin_list = g_slist_append(dplugin_list, (gpointer) d_plugin);
	plugin_make_detachable(d_plugin);
}

static void generic_dac_handle(struct osc_plugin *generic_dac, void *generic_lib, const char *ini_fn) {
	struct params {
		struct osc_plugin *plugin;
		GtkWidget *notebook;
		const char *ini_fn;
	} *params;

	if (!generic_dac || !generic_lib)
		return;

	plugin_list = g_slist_append (plugin_list, (gpointer) generic_dac);
	plugin_lib_list = g_slist_append(plugin_lib_list, generic_lib);

	params = malloc(sizeof(*params));
	params->plugin = generic_dac;
	params->notebook = notebook;
	params->ini_fn = ini_fn;

	GtkWidget *widget = init_plugin(params);
	if (widget) {
		load_plugin_finish(GTK_NOTEBOOK(notebook), widget, generic_dac);
		printf("Loaded plugin: %s\n", generic_dac->name);
	}
}

static void load_plugin_complete(gpointer data, gpointer user_data)
{
	struct osc_plugin *plugin = data;
	GtkWidget *widget;

	/* not sure if plugin can actually be null but... */
	if (!plugin || !plugin->thd)
		return;

	widget = g_thread_join(plugin->thd);
	if (!widget) {
		close_plugin(plugin);
		return;
	}

	load_plugin_finish(GTK_NOTEBOOK(notebook), widget, plugin);
	printf("Loaded plugin: %s\n", plugin->name);
}

#ifdef __MINGW__
	static const bool load_in_parallel = false;
#else
	static const bool load_in_parallel = false;
#endif

static void start_plugin(gpointer data, gpointer user_data)
{
	struct osc_plugin *plugin = data;
	struct params {
		struct osc_plugin *plugin;
		GtkWidget *notebook;
		const char *ini_fn;
	} *params;

	params = malloc(sizeof(*params));
	if (!params) {
		close_plugin(plugin);
		return;
	}

	params->plugin = plugin;
	params->notebook = notebook;
	params->ini_fn = user_data;

	/* Call plugin->init() in a thread to speed up boot time */
	if (load_in_parallel) {
		plugin->thd = g_thread_new(plugin->name, init_plugin, params);
	} else {
		GtkWidget *widget = init_plugin(params);
		if (!widget) {
			close_plugin(plugin);
			return;
		}
		load_plugin_finish(GTK_NOTEBOOK(notebook), widget, plugin);
	}
}

static void load_plugins(GtkWidget *notebook, const char *ini_fn)
{
	typedef GArray* (*get_plugins_info_func)(void);
	typedef struct osc_plugin* (*create_plugin_func)(struct osc_plugin_context *);

	struct osc_plugin *plugin;
	struct dirent *ent;
	char *plugin_dir = "plugins";
	char buf[512];
	DIR *d;
	struct osc_plugin *generic_dac = NULL;
	void *generic_lib = NULL;

	/* Check the local plugins folder first */
	d = opendir(plugin_dir);
	if (!d) {
		plugin_dir = OSC_PLUGIN_PATH;
		d = opendir(plugin_dir);
	}

	while ((ent = readdir(d))) {
		void *lib;

		if (!is_dirent_reqular_file(ent))
			continue;
#ifdef __MINGW__
		if (!str_endswith(ent->d_name, ".dll"))
			continue;
#elif __APPLE__
		 if (!str_endswith(ent->d_name, ".dylib"))
			continue;
#else
		if (!str_endswith(ent->d_name, ".so"))
			continue;
#endif
		snprintf(buf, sizeof(buf), "%s/%s", plugin_dir, ent->d_name);

		/* Don't load obsolete plugins to avoid any potential issues */
		if (!strncmp("fmcomms1.", ent->d_name, strlen("fmcomms1."))) {
			continue;
		}

		lib = dlopen(buf, RTLD_LOCAL | RTLD_LAZY);
		if (!lib) {
			fprintf(stderr, "Failed to load plugin \"%s\": %s\n",
					ent->d_name, dlerror());
			continue;
		}

		plugin = dlsym(lib, "plugin");
		if (!plugin) {
			/* Try for plugins that, first, return info on how they should be constructed */
			get_plugins_info_func get_plugins_info =
				dlsym(lib, "get_data_for_possible_plugin_instances");
			create_plugin_func create_plugin =
				dlsym(lib, "create_plugin");
			if (!get_plugins_info || !create_plugin) {
				fprintf(stderr, "Failed to load plugin \"%s\": "
					"Could not find plugin\n", ent->d_name);
				continue;
			}

			GArray *plugin_info = get_plugins_info();
			if (plugin_info->len == 0) {
				dlclose(lib);
				continue;
			}

			/* Use the information retrieved from the plugin to create instances of it */
			guint i = 0;
			for (; i < plugin_info->len; i++) {
				struct osc_plugin_context *plugin_ctx = g_array_index(plugin_info, struct osc_plugin_context *, i);
				plugin = create_plugin(plugin_ctx);
				printf("Found plugin: %s\n", plugin->name);
				plugin->handle = lib;
				plugin_list = g_slist_append(plugin_list, (gpointer) plugin);
				plugin_lib_list = g_slist_append(plugin_lib_list, lib);

				osc_plugin_context_free_resources(plugin_ctx);
				g_free(plugin_ctx);
			}
			g_array_free(plugin_info, FALSE);

		} else {
			printf("Found plugin: %s\n", plugin->name);

			if (!plugin->identify(plugin) && !force_plugin(plugin->name)) {
				dlclose(lib);
				continue;
			}

			plugin->handle = lib;
			if (!strcmp(plugin->name, "DAC Data Manager")) {
				generic_dac = plugin;
				generic_lib = lib;
				continue;
			}
			plugin_list = g_slist_append (plugin_list, (gpointer) plugin);
			plugin_lib_list = g_slist_append(plugin_lib_list, lib);
		}
	}
	closedir(d);

	// Initialize all plugins that were previously loaded
	g_slist_foreach(plugin_list, start_plugin, (gpointer) ini_fn);

	if (!load_in_parallel) {
		generic_dac_handle(generic_dac, generic_lib, ini_fn);
		return;
	}

	/* Wait for all init functions to finish */
	g_slist_foreach(plugin_list, load_plugin_complete, NULL);
	generic_dac_handle(generic_dac, generic_lib, ini_fn);
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
	} else if (size == 4) {
		int32_t val;
		iio_channel_convert(chn, &val, sample);
		if (format->is_signed)
			*(info->data_ref + info->offset++) = (gfloat) val;
		else
			*(info->data_ref + info->offset++) = (gfloat) (uint32_t)val;
	} else {
		int64_t val;
		iio_channel_convert(chn, &val, sample);
		if (format->is_signed)
			*(info->data_ref + info->offset++) = (gfloat) val;
		else
			*(info->data_ref + info->offset++) = (gfloat) (uint64_t)val;
	}

	return size;
}

static off_t get_trigger_offset(const struct iio_channel *chn,
		bool falling_edge, float trigger_value)
{
	struct extra_info *info = iio_channel_get_data(chn);
	size_t i;

	if (iio_channel_is_enabled(chn)) {
		for (i = info->offset / 2; i >= 1; i--) {
			if (!falling_edge && info->data_ref[i - 1] < trigger_value &&
					info->data_ref[i] >= trigger_value)
				return i * sizeof(gfloat);
			if (falling_edge && info->data_ref[i - 1] >= trigger_value &&
					info->data_ref[i] < trigger_value)
				return i * sizeof(gfloat);
		}
	}
	return 0;
}

static void apply_trigger_offset(const struct iio_channel *chn, off_t offset)
{
	if (offset) {
		struct extra_info *info = iio_channel_get_data(chn);

		memmove(info->data_ref, (const char *)info->data_ref + offset,
				info->offset * sizeof(gfloat) - offset);
	}
}

static bool device_is_oneshot(struct iio_device *dev)
{
	const char *name = iio_device_get_name(dev);

	if (strncmp(name, "ad-mc-", 5) == 0)
		return true;

	return false;
}

static gboolean capture_process(void *data)
{
	unsigned int i;

	if (stop_capture == TRUE)
		goto capture_stop_check;

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct extra_dev_info *dev_info = iio_device_get_data(dev);
		unsigned int i, sample_size = iio_device_get_sample_size(dev);
		unsigned int nb_channels = iio_device_get_channels_count(dev);
		ssize_t sample_count = dev_info->sample_count;
		struct iio_channel *chn;
		off_t offset = 0;

		if (dev_info->input_device == false)
			continue;

		if (sample_size == 0)
			continue;

		if (dev_info->buffer == NULL || device_is_oneshot(dev)) {
			dev_info->buffer_size = sample_count;
			dev_info->buffer = iio_device_create_buffer(dev,
				sample_count, false);
			if (!dev_info->buffer) {
				fprintf(stderr, "Error: Unable to create buffer: %s\n", strerror(errno));
				goto capture_stop_check;
			}
		}

		/* Reset the data offset for all channels */
		for (i = 0; i < nb_channels; i++) {
			struct iio_channel *ch = iio_device_get_channel(dev, i);
			struct extra_info *info = iio_channel_get_data(ch);
			info->offset = 0;
		}

		while (true) {
			ssize_t ret = iio_buffer_refill(dev_info->buffer);
			if (ret < 0) {
				fprintf(stderr, "Error while reading data: %s\n", strerror(-ret));
				if (ret == -EPIPE) {
					restart_capture = true;
				}
				stop_sampling();
				goto capture_stop_check;
			}

			ret /= iio_buffer_step(dev_info->buffer);
			if (ret >= sample_count) {
				iio_buffer_foreach_sample(
						dev_info->buffer, demux_sample, NULL);

				if (ret >= sample_count * 2) {
					printf("Decreasing buffer size\n");
					iio_buffer_destroy(dev_info->buffer);
					dev_info->buffer_size /= 2;
					dev_info->buffer = iio_device_create_buffer(dev,
							dev_info->buffer_size, false);
				}
				break;
			}

			printf("Increasing buffer size\n");
			iio_buffer_destroy(dev_info->buffer);
			dev_info->buffer_size *= 2;
			dev_info->buffer = iio_device_create_buffer(dev,
					dev_info->buffer_size, false);
		}

		if (dev_info->channel_trigger_enabled) {
			chn = iio_device_get_channel(dev, dev_info->channel_trigger);
			if (!iio_channel_is_enabled(chn))
				dev_info->channel_trigger_enabled = false;
		}

		/* We find the sample that meets the trigger condition, then we grab samples
		   before and after it so that the trigger sample gets to be displayed in the
		   middle of the plot. When that's not possible, we display the sample as the
		   first sample of the plot. */
		if (dev_info->channel_trigger_enabled) {
			struct extra_info *info = iio_channel_get_data(chn);
			offset = get_trigger_offset(chn, dev_info->trigger_falling_edge,
					dev_info->trigger_value);
			const off_t quatter_of_capture_interval = info->offset / 4;
			if (offset / (off_t)sizeof(gfloat) < quatter_of_capture_interval) {
				offset = 0;
			} else if (offset) {
				offset -= quatter_of_capture_interval * sizeof(gfloat);
				for (i = 0; i < nb_channels; i++) {
					chn = iio_device_get_channel(dev, i);
					if (iio_channel_is_enabled(chn))
						apply_trigger_offset(chn, offset);
				}
			}
		}

		if (dev_info->channels_data_copy) {
			for (i = 0; i < nb_channels; i++) {
				struct iio_channel *ch = iio_device_get_channel(dev, i);
				struct extra_info *info = iio_channel_get_data(ch);
				memcpy(dev_info->channels_data_copy[i], info->data_ref,
					sample_count * sizeof(gfloat));
			}
			dev_info->channels_data_copy = NULL;
			G_UNLOCK(buffer_full);
		}

		if (device_is_oneshot(dev)) {
			iio_buffer_destroy(dev_info->buffer);
			dev_info->buffer = NULL;
		}

		if (!dev_info->channel_trigger_enabled || offset)
			update_plot(dev_info->buffer);
	}

	update_plot(NULL);

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
	double freq = 400.0, freq_sum = 0.0;
	int ret = -1;
	unsigned int i, nb_channels = iio_device_get_channels_count(dev);
	const char *attr;
	char buf[1024];

	for (i = 0; i < nb_channels; i++) {
		struct iio_channel *ch = iio_device_get_channel(dev, i);

		if (iio_channel_is_output(ch) || strncmp(iio_channel_get_id(ch),
					"voltage", sizeof("voltage") - 1))
			continue;

		ret = iio_channel_attr_read(ch, "sampling_frequency",
				buf, sizeof(buf));
		if (ret > 0 && iio_channel_is_enabled(ch)) {
			sscanf(buf, "%lf", &freq);
			freq_sum += 1 / freq;
		}

	}

	if (ret < 0)
		ret = iio_device_attr_read(dev, "sampling_frequency",
				buf, sizeof(buf));
	if (ret < 0) {
		const struct iio_device *trigger;
		ret = iio_device_get_trigger(dev, &trigger);
		if (ret == 0 && trigger) {
			attr = iio_device_find_attr(trigger, "sampling_frequency");
			if (!attr)
				attr = iio_device_find_attr(trigger, "frequency");
			if (attr)
				ret = iio_device_attr_read(trigger, attr, buf,
					sizeof(buf));
			else
				ret = -ENOENT;
		}
	}

	if (ret > 0)
		sscanf(buf, "%lf", &freq);
	else if (ret > 0 && freq_sum != 0)
		freq = 1 / freq_sum;

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

		/* We capture a double amount o data. Then we look for a trigger
		   condition in a half of this interval. This way, no matter where
		   the trigger sample is we have enough samples after the triggered
		   one to display on the plot. */
		if (dev_info->channel_trigger_enabled)
			sample_count *= 2;

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
		dev_info->buffer = NULL;
		dev_info->sample_count = sample_count;

		iio_device_set_data(dev, dev_info);

		freq = read_sampling_frequency(dev);
		if (freq > 0) {
			/* 2 x capture time + 1s */
			timeout = sample_count * 1000 / freq;
			if (dev_info->channel_trigger_enabled)
				timeout *= 2;
			timeout += 1000;
			if (timeout > min_timeout)
				min_timeout = timeout;
		}

		rx_update_device_sampling_freq(get_iio_device_label_or_name(dev), freq);
	}

	if (ctx) {
		if (ini_capture_timeout_loaded) {
			min_timeout = ini_capture_timeout;
		}
		iio_context_set_timeout(ctx, min_timeout);
	}

	return 0;
}

static void capture_process_ended(gpointer data)
{
	if (restart_capture == true) {
		fprintf(stderr, "Sample acquisition stopped\n");
		restart_capture = false;

		/* Wait 100 msec then restart acquisition */
		g_usleep(G_USEC_PER_SEC * 0.1);

		capture_setup();
		fprintf(stderr, "Restarting acquisition\n");
		capture_start();
	}
}

static void capture_start(void)
{
	if (capture_function) {
		stop_capture = FALSE;
	}
	else {
		stop_capture = FALSE;
		capture_function = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 50, capture_process, NULL, capture_process_ended);
	}
}

static void start(OscPlot *plot, gboolean start_event)
{
	if (start_event) {
		num_capturing_plots++;
		/* Stop the capture process to allow settings to be updated */
		stop_capture = TRUE;

		G_TRYLOCK(buffer_full);

		/* Make sure the capture process in the Spectrum Analyzer plugin
		 * is not running */
		if (spect_analyzer_plugin)
			spect_analyzer_plugin->handle_external_request(spect_analyzer_plugin, "Stop");

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
	osc_plot_set_quit_callback(OSC_PLOT(plot), (void (*)(void *))application_quit, NULL);
	gtk_widget_show(plot);
}

GtkWidget * new_plot_cb(GtkMenuItem *item, gpointer user_data)
{
	GtkWidget *new_plot;

	if (osc_preferences && osc_preferences->plot_preferences)
		new_plot = osc_plot_new_with_pref(ctx, osc_preferences->plot_preferences);
	else
		new_plot = osc_plot_new(ctx);

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
	num_check_fcts = 0;
	setup_check_functions = NULL;
}

static gboolean idle_timeout_check(gpointer ptr)
{
	int ret;
	struct iio_context *new_ctx = (struct iio_context *) ptr;
	struct iio_device *dev;
	const struct iio_device *trig;

	if (new_ctx != ctx)
		return FALSE;

	/* Just get the first dev. We only run this if there are devices in the IIO context */
	dev = iio_context_get_device(new_ctx, 0);
	/* We use iio_device_get_trigger here just as a way to ping the
	 * IIOD server. */
	ret = iio_device_get_trigger(dev, &trig);
	if (ret == -EPIPE) {
		gtk_widget_set_visible(infobar, true);
		return FALSE;
	} else {
		return TRUE;
	}
}

static gchar * get_default_profile_name(void)
{
	return g_build_filename(
			getenv("HOME") ?: getenv("LOCALAPPDATA"),
			DEFAULT_PROFILE_NAME, NULL);
}

static void do_quit(bool reload)
{
	unsigned int i, nb = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));
	gchar *path = NULL;

	/* Before we shut down, let's save the profile */
	if (!reload) {
		path = get_default_profile_name();
		capture_profile_save(path);
	}

	stop_capture = TRUE;
	G_TRYLOCK(buffer_full);
	G_UNLOCK(buffer_full);
	close_active_buffers();

	close_all_plots();
	destroy_all_plots();

	g_list_free(plot_list);
	free_setup_check_fct_list();
	osc_plot_reset_numbering();

	if (!reload && gtk_main_level())
		gtk_main_quit();

	for (i = 0; i < nb; i++)
	while (true) {
		GtkNotebook *book = GTK_NOTEBOOK(notebook);
		int page = gtk_notebook_get_current_page(book);
		if (page < 0)
			break;

		gtk_notebook_remove_page(book, page);
	}

	/* This can't be done until all the windows are detroyed with main_quit
	 * otherwise, the widgets need to be updated, but they don't exist anymore
	 */
	close_plugins(path);
	g_free(path);

	if (!reload && ctx) {
		iio_context_destroy(ctx);
		ctx = NULL;
		ctx_destroyed_by_do_quit = true;
	}

	math_expression_objects_clean();

	if (osc_preferences) {
		osc_preferences_delete(osc_preferences);
		osc_preferences = NULL;
	}
}

void application_reload(struct iio_context *new_ctx, bool load_profile)
{
	if (!new_ctx) {
		fprintf(stderr, "Invalid new context!\n");
		return;
	}

	do_quit(true);
	if (ctx)
		iio_context_destroy(ctx);

	ctx = new_ctx;
	do_init(new_ctx);
	if (load_profile)
		load_default_profile(NULL, true);
}

void application_quit (void)
{
	do_quit(false);
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

static void init_device_list(struct iio_context *_ctx)
{
	unsigned int i, j;

	num_devices = iio_context_get_devices_count(_ctx);

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(_ctx, i);
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

		rx_update_device_sampling_freq(
			get_iio_device_label_or_name(dev), USE_INTERN_SAMPLING_FREQ);
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

/*
 * Allows plugins to set the "adc_freq" field of the "extra_dev_info"
 * struct for the given iio device. Mostly is meant for updating the
 * "adc_freq" field with the sampling freq iio attribute value. This can
 * be done by passing a negative value to the "freq" parameter.
 * @device - name of the device
 * @freq   - value of the sampling frequency or a negative value to use
 *           the iio attribute value instead.
 */
bool rx_update_device_sampling_freq(const char *device, double freq)
{
	struct iio_device *dev;
	struct extra_dev_info *info;

	g_return_val_if_fail(device, false);

	dev = iio_context_find_device(ctx, device);
	if (!dev) {
		fprintf(stderr, "Device: %s not found!\n", device);
		return false;
	}

	info = iio_device_get_data(dev);
	if (!info) {
		fprintf(stderr, "Device: %s extra info not found!\n", device);
		return false;
	}

	if (freq >= 0)
		info->adc_freq = freq;
	else
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

	GList *node;

	for (node = plot_list; node; node = g_list_next(node))
		osc_plot_update_rx_lbl(OSC_PLOT(node->data), NORMAL_UPDATE);

	return true;
}

/*
 * Allows plugins to set the "lo_freq" field of the "struct extra_info"
 * for the given iio channel. Only input channels are affected.
 * @device  - name of the device
 * @channel - name of the channel
 *          - use "all" value to target all iio scan_element channels
 * @lo_freq - value of the Local Oscillcator frequency (Hz)
 */
bool rx_update_channel_lo_freq(const char *device, const char *channel,
	double lo_freq)
{
	struct iio_device *dev;
	struct iio_channel *chn;
	struct extra_info *chn_info;

	g_return_val_if_fail(device, false);
	g_return_val_if_fail(channel, false);

	dev = iio_context_find_device(ctx, device);
	if (!dev) {
		fprintf(stderr, "Device: %s not found\n!", device);
		return false;
	}

	if (!strcmp(channel, "all")) {
		bool success = true;
		unsigned int i = 0;
		for (; i < iio_device_get_channels_count(dev); i++) {
			chn = iio_device_get_channel(dev, i);
			if (!iio_channel_is_scan_element(chn) ||
					iio_channel_is_output(chn)) {
				continue;
			}
			chn_info = iio_channel_get_data(chn);
			if (chn_info) {
				chn_info->lo_freq = lo_freq;
			} else {
				fprintf(stderr, "Channel: %s extra info "
					"not found!\n", channel);
				success = false;
			}
		}
		return success;
	}

	chn = iio_device_find_channel(dev, channel, false);
	if (!chn) {
		fprintf(stderr, "Channel: %s not found!\n", channel);
		return false;
	}

	chn_info = iio_channel_get_data(chn);
	if (!chn_info) {
		fprintf(stderr, "Channel: %s extra info not found!\n", channel);
		return false;
	}

	chn_info->lo_freq = lo_freq;

	return true;
}

/* Before we really start, let's load the last saved profile */
bool check_inifile(const char *filepath)
{
	struct stat sts;
	FILE *fd;
	char buf[1024];
	size_t i;

	buf[1023] = '\0';

	if (stat(filepath, &sts) == -1)
		return FALSE;

	if (!S_ISREG(sts.st_mode))
		return FALSE;

	fd = fopen(filepath, "r");
	if (!fd)
		return FALSE;

	i = fread(buf, 1, sizeof(buf) - 1, fd);
	fclose(fd);

	if (i == 0 )
		return FALSE;

	if (!strstr(buf, "["OSC_INI_SECTION"]"))
		return FALSE;

	return TRUE;
}

int load_default_profile(char *filename, bool load_plugins)
{
	int ret = 0;

	/* Don't load anything */
	if (filename && !strcmp(filename, "-"))
		return 0;

	if (filename && check_inifile(filename)) {
		ret = load_profile(filename, load_plugins);
	} else {
		gchar *path = get_default_profile_name();

		/* if this is bad, we don't load anything and
		 * return success, so we still run */
		if (check_inifile(path))
			ret = load_profile(path, load_plugins);

		g_free(path);
	}

	return ret;
}

static OscPreferences * aggregate_osc_preferences_from_plugins(GSList *plist)
{
	/* TO DO: implement this function to combine preferences from all
	plugins. If two preferences are conflicting, signal that there is an
	error in the design of the plugins and return NULL, otherwise return
	the merged preferences from all plugins. For now we just get the first
	preference that we encoutner. */

	GSList *node;
	struct osc_plugin *p;

	for (node = plist; node; node = g_slist_next(node)) {
		p = node->data;
		if (p->get_preferences_for_osc) {
			return p->get_preferences_for_osc(p);
		}
	}

	return NULL;
}

static void plugins_get_preferred_size(GSList *plist, int *width, int *height)
{
	GSList *node;
	struct osc_plugin *p;
	int w, h, max_w = -1, max_h = -1;

	for (node = plist; node; node = g_slist_next(node)) {
		p = node->data;
		if (p->get_preferred_size) {
			p->get_preferred_size(p, &w, &h);
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

void create_default_plot(void)
{
	if (ctx && !!iio_context_get_devices_count(ctx) &&
			g_list_length(plot_list) == 0)
		new_plot_cb(NULL, NULL);
}

void do_init(struct iio_context *new_ctx)
{
	init_device_list(new_ctx);
	load_plugins(notebook, NULL);
	osc_preferences = aggregate_osc_preferences_from_plugins(plugin_list);

	int width = -1, height = -1;
	plugins_get_preferred_size(plugin_list, &width, &height);
	window_size_readjust(GTK_WINDOW(main_window), width, height);

	if (!strcmp(iio_context_get_name(new_ctx), "network") &&
	    iio_context_get_devices_count(new_ctx)) {
		gtk_widget_set_visible(infobar, false);
		g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 1000,
				idle_timeout_check, new_ctx, NULL);
	}

	spect_analyzer_plugin = get_plugin_from_name("Spectrum Analyzer");
}

OscPlot * osc_find_plot_by_id(int id)
{
	GList *node;

	for (node = plot_list; node; node = g_list_next(node))
		if (id == osc_plot_get_id((OscPlot *) node->data))
			return (OscPlot *) node->data;

	return NULL;
}

/*
 * Check for settings in sections [MultiOsc_Capture_Configuration1,2,..]
 * Handler should return zero on success, a negative number on error.
 */
static int capture_profile_handler(int line, const char *section,
		const char *name, const char *value)
{
	OscPlot *plot;
	int plot_id;

	if (strncmp(section, CAPTURE_INI_SECTION, sizeof(CAPTURE_INI_SECTION) - 1))
		return 1;

	if (!ctx || !iio_context_get_devices_count(ctx))
		return 0;

	plot_id = atoi(section + sizeof(CAPTURE_INI_SECTION) - 1);

	plot = osc_find_plot_by_id(plot_id);
	if (!plot) {
		plot = plugin_get_new_plot();
		osc_plot_set_id(plot, plot_id);
		osc_plot_set_visible(plot, false);
	}

	/* Parse the line from ini file */
	return osc_plot_ini_read_handler(plot, line, section, name, value);
}

static void gfunc_save_plot_data_to_ini(gpointer data, gpointer user_data)
{
	OscPlot *plot = OSC_PLOT(data);
	char *filename = (char *)user_data;

	osc_plot_save_to_ini(plot, filename);
}

static void capture_profile_save(const char *filename)
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

	fprintf(fp, "tooltips_enable=%d\n",
		gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(tooltips_en)));
	fprintf(fp, "startup_version_check=%d\n",
		gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(versioncheck_en)));
	if (ctx) {
		if (!strcmp(iio_context_get_name(ctx), "network")) {
			char *ip_addr = (char *) iio_context_get_description(ctx);
			ip_addr = strtok(ip_addr, " ");
			fprintf(fp, "remote_ip_addr=%s\n", ip_addr);
		} else if (!strcmp(iio_context_get_name(ctx), "usb")) {
			if (usb_get_serialnumber(ctx))
				fprintf(fp, "uri=%s\n", usb_get_serialnumber(ctx));
		} else {
			fprintf(stderr, "%s: unknown context %s\n",
				__func__, iio_context_get_name(ctx));
		}
	}
	if (ini_capture_timeout_loaded) {
		fprintf(fp, "capture_timeout=%d\n", ini_capture_timeout);
	}

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

static void plugin_restore_ini_state(const char *plugin_name,
		const char *attribute, int value)
{
	struct detachable_plugin *dplugin;
	GSList *found_plugin;
	GtkWindow *plugin_window;
	GtkWidget *button;

	found_plugin = g_slist_find_custom(dplugin_list,
		(gconstpointer)plugin_name, plugin_names_cmp);
	if (found_plugin == NULL)
		return;

	dplugin = found_plugin->data;
	button = dplugin->detach_attach_button;

	if (!strcmp(attribute, "detached")) {
		if ((dplugin->detached_state) ^ (value))
			g_signal_emit_by_name(button, "clicked", dplugin);
	} else if (!strcmp(attribute, "x_pos")) {
		plugin_window = GTK_WINDOW(gtk_widget_get_toplevel(button));
		if (dplugin->detached_state == true) {
			dplugin->xpos = value;
			move_gtk_window_on_screen(plugin_window, dplugin->xpos, dplugin->ypos);
		}
	} else if (!strcmp(attribute, "y_pos")) {
		plugin_window = GTK_WINDOW(gtk_widget_get_toplevel(button));
		if (dplugin->detached_state == true) {
			dplugin->ypos = value;
			move_gtk_window_on_screen(plugin_window, dplugin->xpos, dplugin->ypos);
		}
	}
}

void save_complete_profile(const char *filename)
{
	GSList *node;

	capture_profile_save(filename);

	for (node = plugin_list; node; node = g_slist_next(node)) {
		struct osc_plugin *plugin = node->data;
		if (plugin->save_profile)
			plugin->save_profile(plugin, filename);
	}
}

static int handle_osc_param(int line, const char *name, const char *value)
{
	gchar **elems;

	if (!strcmp(name, "tooltips_enable")) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(tooltips_en),
				!!atoi(value));
		return 0;
	} else if (!strcmp(name, "startup_version_check")) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(versioncheck_en),
				!!atoi(value));
		return 0;
	}

	if (!strcmp(name, "test") || !strcmp(name, "window_x_pos") ||
			!strcmp(name, "window_y_pos") || !strcmp(name, "remote_ip_addr")) {
		printf("Ignoring token \'%s\' when loading sequentially\n", name);
		return 0;
	}

	elems = g_strsplit(name, ".", 3);
	if (elems && !strcmp(elems[0], "plugin")) {
		plugin_restore_ini_state(elems[1], elems[2], !!atoi(value));
		g_strfreev(elems);
		return 0;
	}

	g_strfreev(elems);

	create_blocking_popup(GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
			"Unhandled attribute",
			"Unhandled attribute in main section, line %i:\n"
			"%s = %s\n", line, name, value);
	fprintf(stderr, "Unhandled attribute in main section, line %i: "
			"%s = %s\n", line, name, value);
	return -1;
}

static int load_profile_sequential_handler(int line, const char *section,
		const char *name, const char *value)
{
	struct osc_plugin *plugin = get_plugin_from_name(section);
	if (plugin) {
		if (plugin->handle_item)
			return plugin->handle_item(plugin, line, name, value);
		else {
			fprintf(stderr, "Unknown plugin for %s\n", section);
			return 1;
		}
	}

	if (!strncmp(section, CAPTURE_INI_SECTION, sizeof(CAPTURE_INI_SECTION) - 1))
		return capture_profile_handler(line, section, name, value);

	if (!strcmp(section, OSC_INI_SECTION))
		return handle_osc_param(line, name, value);

	create_blocking_popup(GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
			"Unhandled INI section",
			"Unhandled INI section: [%s]\n", section);
	fprintf(stderr, "Unhandled INI section: [%s]\n", section);
	return 1;
}

static int load_profile_sequential(const char *filename)
{
	gchar *new_filename;
	char buf[32];
	int ret = 0;

	snprintf(buf, sizeof(buf), "osc_%u.ini", getpid());

	new_filename = g_build_filename(getenv("TEMP") ?: P_tmpdir, buf, NULL);
	unlink(new_filename);

	ret = ini_unroll(filename, new_filename);
	if (ret < 0)
		goto err_unlink;

	if (!ctx)
		connect_dialog(false);
	if (!ctx)
		goto err_unlink;

	printf("Loading profile sequentially from %s\n", new_filename);
	ret = foreach_in_ini(new_filename, load_profile_sequential_handler);
	if (ret < 0) {
		fprintf(stderr, "Sequential loading of profile aborted.\n");
		application_quit();
	} else {
		fprintf(stderr, "Sequential loading completed.\n");
	}

err_unlink:
	unlink(new_filename);
	g_free(new_filename);

	return ret;
}

static int load_profile(const char *filename, bool load_plugins)
{
	int ret = 0;
	GSList *node;
	gint x_pos = 0, y_pos = 0;
	char *value;

	close_all_plots();
	destroy_all_plots();

	value = read_token_from_ini(filename, OSC_INI_SECTION, "remote_ip_addr");
	/* IP addresses specified on the command line via the -c option
	 * override profile settings.
	 */
	if (value && !(ctx && !strcmp(iio_context_get_name(ctx), "network"))) {
		struct iio_context *new_ctx = iio_create_network_context(value);
		if (new_ctx) {
			application_reload(new_ctx, false);
		} else {
			fprintf(stderr, "Failed connecting to remote device: %s\n", value);
			/* Abort parsing the rest of the profile as there is
			 * probably a lot of device specific stuff in it.
			 */
			free(value);
			return 0;
		}
		free(value);
	}

	value = read_token_from_ini(filename, OSC_INI_SECTION, "uri");
	/* URI addresses specified on the command line via the -u option
	 * override profile settings
	 */
	if (value && !(ctx && (!strcmp(iio_context_get_name(ctx), "uri")))) {
		struct iio_context *new_ctx;
		struct iio_scan_context *ctxs = iio_create_scan_context(NULL, 0);
		struct iio_context_info **info;
		char *pid_vid = value;
		char *serial = strchr(value, ' ');
		const char *tmp;
		int i;

		if (!serial)
			goto nope;
		usb_set_serialnumber(value);

		pid_vid[serial - pid_vid] = 0;
		serial++;

		if (!ctxs)
			goto nope;
		ret = iio_scan_context_get_info_list(ctxs, &info);
		if (ret < 0)
			goto nope_ctxs;
		if (!ret)
			goto nope_list;

		for (i = 0; i < ret; i++) {
			tmp = iio_context_info_get_description(info[i]);
			/* find the correct PID/VID plus serial number*/
			if (strstr(tmp, pid_vid) && strstr(tmp, serial)) {
				new_ctx = iio_create_context_from_uri(
						iio_context_info_get_uri(info[i]));
				if (new_ctx) {
					application_reload(new_ctx, false);
					break;
				} else {
					fprintf(stderr, "Failed connecting to uri: %s\n", value);
					free(value);
					return 0;
				}
			}

		}
nope_list:
		iio_context_info_list_free(info);
nope_ctxs:
		iio_scan_context_destroy(ctxs);
nope:
		free(value);
		ret = 0;
	}

	value = read_token_from_ini(filename, OSC_INI_SECTION, "test");
	if (value) {
		free(value);
		ret = load_profile_sequential(filename);
		return ret;
	}

	value = read_token_from_ini(filename,
			OSC_INI_SECTION, "tooltips_enable");
	if (value) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(tooltips_en),
				!!atoi(value));
		free(value);
	}

	value = read_token_from_ini(filename,
			OSC_INI_SECTION, "startup_version_check");
	if (value) {
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(versioncheck_en),
				!!atoi(value));
		free(value);
	}

	value = read_token_from_ini(filename, OSC_INI_SECTION, "window_x_pos");
	if (value) {
		x_pos = atoi(value);
		free(value);
	}

	value = read_token_from_ini(filename, OSC_INI_SECTION, "window_y_pos");
	if (value) {
		y_pos = atoi(value);
		free(value);
	}

	move_gtk_window_on_screen(GTK_WINDOW(main_window), x_pos, y_pos);

	value = read_token_from_ini(filename, OSC_INI_SECTION, "capture_timeout");
	if (value) {
		ini_capture_timeout_loaded = true;
		ini_capture_timeout = atoi(value);
		free(value);
	}

	foreach_in_ini(filename, capture_profile_handler);

	for (node = plugin_list; node; node = g_slist_next(node)) {
		struct osc_plugin *plugin = node->data;
		char buf[1024];

		if (load_plugins && plugin->load_profile)
			plugin->load_profile(plugin, filename);

		snprintf(buf, sizeof(buf), "plugin.%s.detached", plugin->name);
		value = read_token_from_ini(filename, OSC_INI_SECTION, buf);
		if (!value)
			continue;

		plugin_restore_ini_state(plugin->name, "detached", !!atoi(value));
		free(value);
	}

	return ret;
}

void load_complete_profile(const char *filename)
{
	load_profile(filename, true);
}

struct iio_context * osc_create_context(void)
{
	if (!ctx)
		return iio_create_default_context();
	else
		return iio_context_clone(ctx) ?: ctx;
}

void osc_destroy_context(struct iio_context *_ctx)
{
	if (_ctx != ctx)
		iio_context_destroy(_ctx);
}

/* Wait while processing GTK events for a given number of milliseconds. Used
 * for waiting for certain gtk events to settle when performing various widget
 * related test procedures.
 */
void osc_process_gtk_events(unsigned int msecs)
{
	struct timespec ts_current, ts_end;
	unsigned long long nsecs;
	clock_gettime(CLOCK_MONOTONIC, &ts_current);
	nsecs = ts_current.tv_nsec + (msecs * pow(10.0, 6));
	ts_end.tv_sec = ts_current.tv_sec + (nsecs / pow(10.0, 9));
	ts_end.tv_nsec = nsecs % (unsigned long long) pow(10.0, 9);
	while (timespeccmp(&ts_current, &ts_end, >) == 0) {
		gtk_main_iteration();
		clock_gettime(CLOCK_MONOTONIC, &ts_current);
	}
}

/* Test something, according to:
 * test.device.attribute.type = min max
 */
int osc_test_value(struct iio_context *_ctx, int line,
		const char *attribute, const char *value)
{
	struct iio_device *dev;
	struct iio_channel *chn;
	const char *attr;
	long long min_i = -1, max_i = -1, val_i;
	double min_d, max_d, val_d;
	unsigned int i;
	int ret = -EINVAL;

	gchar **elems = g_strsplit(attribute, ".", 4);
	if (!elems)
		goto err_popup;

	if (!elems[0] || strcmp(elems[0], "test"))
		goto cleanup;

	for (i = 1; i < 4; i++)
		if (!elems[i])
			goto cleanup;

	dev = iio_context_find_device(_ctx, elems[1]);
	if (!dev) {
		ret = -ENODEV;
		goto cleanup;
	}

	ret = iio_device_identify_filename(dev, elems[2], &chn, &attr);
	if (ret < 0)
		goto cleanup;

	if (!strcmp(elems[3], "int")) {
		ret = sscanf(value, "%lli %lli", &min_i, &max_i);
		if (ret != 2) {
			ret = -EINVAL;
			goto cleanup;
		}

		if (chn)
			ret = iio_channel_attr_read_longlong(chn, attr, &val_i);
		else
			ret = iio_device_attr_read_longlong(dev, attr, &val_i);
		if (ret < 0)
			goto cleanup;

		printf("Line %i: (%s = %s): value = %lli\n",
				line, attribute, value, val_i);
		ret = val_i >= min_i && val_i <= max_i;
		if (!ret)
			create_blocking_popup(GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
					"Test failure",
					"Test failed! Line: %i\n\n"
					"Test was: %s = %lli %lli\n"
					"Value read = %lli\n",
					line, attribute, min_i, max_i, val_i);

	} else if (!strcmp(elems[3], "double")) {
		gchar *end1, *end2;
		min_d = g_ascii_strtod(value, &end1);
		if (end1 == value) {
			ret = -EINVAL;
			goto cleanup;
		}

		max_d = g_ascii_strtod(end1, &end2);
		if (end1 == end2) {
			ret = -EINVAL;
			goto cleanup;
		}

		if (chn)
			ret = iio_channel_attr_read_double(chn, attr, &val_d);
		else
			ret = iio_device_attr_read_double(dev, attr, &val_d);
		if (ret < 0)
			goto cleanup;

		printf("Line %i: (%s = %s): value = %lf\n",
				line, attribute, value, val_d);
		ret = val_d >= min_d && val_d <= max_d;
		if (!ret)
			create_blocking_popup(GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
					"Test failure",
					"Test failed! Line: %i\n\n"
					"Test was: %s = %f %f\n"
					"Value read = %f\n",
					line, attribute, min_d, max_d, val_d);

	} else {
		ret = -EINVAL;
		goto cleanup;
	}

	if (ret < 0) {
		fprintf(stderr, "Unable to test \"%s\": %s\n",
				attribute, strerror(-ret));
	} else if (ret == 0) {
		ret = -1;
		fprintf(stderr, "*** Test failed! ***\n");
	} else {
		fprintf(stderr, "Test passed.\n");
	}

	g_strfreev(elems);
	return ret;

cleanup:
	g_strfreev(elems);
err_popup:
	create_blocking_popup(GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
			"INI parsing failure",
			"Unable to parse line: %i\n\n%s = %lli %lli\n",
			line, attribute, min_i, max_i);
	fprintf(stderr, "Unable to parse line: %i: %s = %lli %lli\n",
			line, attribute, min_i, max_i);
	return ret;
}

int osc_identify_attrib(struct iio_context *_ctx, const char *attrib,
		struct iio_device **dev, struct iio_channel **chn,
		const char **attr, bool *debug)
{
	struct iio_device *device;
	int i;
	bool is_debug;
	int ret = -EINVAL;

	gchar *dev_name, *filename, **elems = g_strsplit(attrib, ".", 3);
	if (!elems)
		return -EINVAL;

	is_debug = !strcmp(elems[0], "debug");

	for (i = 0; i < 2 + is_debug; i++)
		if (!elems[i])
			goto cleanup;

	if (is_debug) {
		dev_name = elems[1];
		filename = elems[2];
	} else {
		dev_name = elems[0];
		filename = elems[1];
	}

	device = iio_context_find_device(_ctx, dev_name);
	if (!device) {
		ret = -ENODEV;
		goto cleanup;
	}

	ret = iio_device_identify_filename(device, filename, chn, attr);
	if (!ret) {
		*debug = is_debug;
		*dev = device;
	}

cleanup:
	g_strfreev(elems);
	return ret;
}

static int osc_read_nonenclosed_value(struct iio_context *_ctx,
		const char *value, long long *out)
{
	struct iio_device *dev;
	struct iio_channel *chn;
	const char *attr;
	char *pend;
	bool debug;
	int ret = osc_identify_attrib(_ctx, value, &dev, &chn, &attr, &debug);

	if (ret == -EINVAL) {
		*out = strtoll(value, &pend, 10);
		/* if we are pointing to the end of the string, we are fine */
		if ((strlen(value) + value) == pend) {
			return 0;
		}
	}
	if (ret < 0)
		return ret;

	if (chn)
		ret = iio_channel_attr_read_longlong(chn, attr, out);
	else if (debug)
		ret = iio_device_debug_attr_read_longlong(dev, attr, out);
	else
		ret = iio_device_attr_read_longlong(dev, attr, out);
	return ret < 0 ? ret : 0;
}

static int osc_read_enclosed_value(struct iio_context *_ctx,
		const char *value, long long *out)
{
	const char *plus = strstr(value, " + "),
	      *minus = strstr(value, " - ");
	size_t len = strlen(value);
	gchar *ptr, *val;
	long long val_left, val_right;
	int ret;

	if (value[0] != '{' || value[len - 1] != '}')
		return -EINVAL;

	if (!plus && !minus) {
		ptr = g_strndup(value + 1, len - 2);
		ret = osc_read_nonenclosed_value(_ctx, ptr, out);
		g_free(ptr);
		return ret;
	}

	ptr = strchr(value + 1, '}');
	if (!ptr)
		return -EINVAL;

	val = g_strndup(value + 2, ptr - value - 2);
	ret = osc_read_nonenclosed_value(_ctx, val, &val_left);
	g_free(val);
	if (ret < 0)
		return ret;

	ptr = strchr(value + 2, '{');
	if (!ptr)
		return -EINVAL;

	val = g_strndup(ptr + 1, value + len - ptr - 3);
	ret = osc_read_nonenclosed_value(_ctx, val, &val_right);
	g_free(val);
	if (ret < 0)
		return ret;

	if (plus)
		*out = val_left + val_right;
	else
		*out = val_left - val_right;
	return 0;
}

int osc_read_value(struct iio_context *_ctx,
		const char *value, long long *out)
{
	if (value[0] == '{') {
		return osc_read_enclosed_value(_ctx, value, out);
	} else {
		char *end;
		long long result = strtoll(value, &end, 10);
		if (value == end)
			return -EINVAL;

		*out = result;
		return 0;
	}
}

FILE * osc_get_log_file(const char *path)
{
	FILE *f;
	const char *wave = strstr(path, "~/") ?: strstr(path, "~\\");
	if (wave) {
		gchar *tmp = g_build_filename(
				getenv("HOME") ?: getenv("LOCALAPPDATA"),
				wave + 2, NULL);
		f = fopen(tmp, "a");
		g_free(tmp);
	} else {
		f = fopen(path, "a");
	}

	return f;
}

/* Log the value of a parameter in a text file:
 * log.device.filename = output_file
 */
int osc_log_value(struct iio_context *_ctx,
		const char *attribute, const char *value)
{
	int ret;
	struct iio_device *dev;
	struct iio_channel *chn;
	const char *attr;
	char buf[1024];
	bool debug;
	FILE *f;

	if (strncmp(attribute, "log.", sizeof("log.") - 1)) {
		ret = -EINVAL;
		goto err_ret;
	}

	ret = osc_identify_attrib(_ctx,
			attribute + sizeof("log.") - 1,
			&dev, &chn, &attr, &debug);
	if (ret < 0)
		goto err_ret;

	if (chn)
		ret = iio_channel_attr_read(chn, attr, buf, sizeof(buf));
	else if (debug)
		ret = iio_device_debug_attr_read(dev, attr, buf, sizeof(buf));
	else
		ret = iio_device_attr_read(dev, attr, buf, sizeof(buf));
	if (ret < 0)
		goto err_ret;

	f = osc_get_log_file(value);
	if (!f) {
		ret = -errno;
		goto err_ret;
	}

	fprintf(f, "%s, ", buf);
	fclose(f);
	return 0;

err_ret:
	fprintf(stderr, "Unable to log \"%s\": %s\n",
			attribute, strerror(-ret));
	return ret;
}

int osc_plugin_default_handle(struct iio_context *_ctx,
		int line, const char *attrib, const char *value,
		int (*driver_handle)(struct osc_plugin *plugin, const char *, const char *),
		struct osc_plugin *plugin)
{
	struct iio_device *dev;
	struct iio_channel *chn;
	const char *attr;
	bool debug;
	int ret;

	if (!strncmp(attrib, "test.", sizeof("test.") - 1)) {
		ret = osc_test_value(_ctx, line, attrib, value);
		return ret < 1 ? -1 : 0;
	}

	if (!strncmp(attrib, "log.", sizeof("log.") - 1))
		return osc_log_value(_ctx, attrib, value);

	ret = osc_identify_attrib(_ctx, attrib, &dev, &chn, &attr, &debug);
	if (ret < 0) {
		if (driver_handle)
			return driver_handle(plugin, attrib, value);
		else {
			fprintf(stderr, "Error parsing ini file; key:'%s' value:'%s'\n",
					attrib, value);
			return ret;
		}
	}

	if (value[0] == '{') {
		long long lval;
		ret = osc_read_value(_ctx, value, &lval);
		if (ret < 0) {
			fprintf(stderr, "Unable to read value: %s\n", value);
			return ret;
		}

		if (chn)
			ret = iio_channel_attr_write_longlong(chn, attr, lval);
		else if (debug)
			ret = iio_device_debug_attr_write_longlong(dev, attr, lval);
		else
			ret = iio_device_attr_write_longlong(dev, attr, lval);
	} else if (chn)
		ret = iio_channel_attr_write(chn, attr, value);
	else if (debug)
		ret = iio_device_debug_attr_write(dev, attr, value);
	else
		ret = iio_device_attr_write(dev, attr, value);

	if (ret < 0) {
		fprintf(stderr, "Unable to write '%s' to %s:%s\n", value,
				chn ? iio_channel_get_name(chn) : iio_device_get_name(dev),
				attr);
	}

	return ret < 0 ? ret : 0;
}

int osc_load_glade_file(GtkBuilder *builder, const char *fname)
{
	char path[256];
	snprintf(path, sizeof(path), "glade/%s.glade", fname);
	if (gtk_builder_add_from_file(builder, path, NULL))
		return 0;
	snprintf(path, sizeof(path), OSC_GLADE_FILE_PATH "%s.glade", fname);
	if (gtk_builder_add_from_file(builder, path, NULL))
		return 0;
	fprintf(stderr, "Could not find/load '%s.glade' file\n", fname);
	return -1;
}

int osc_load_objects_from_glade_file(GtkBuilder *builder, const char *fname, gchar **object_ids)
{
	char path[256];
	snprintf(path, sizeof(path), "glade/%s.glade", fname);
	if (gtk_builder_add_objects_from_file(builder, path, object_ids, NULL))
		return 0;
	snprintf(path, sizeof(path), OSC_GLADE_FILE_PATH "%s.glade", fname);
	if (gtk_builder_add_objects_from_file(builder, path, object_ids, NULL))
		return 0;
	fprintf(stderr, "Could not find '%s.glade' file", fname);
	return -1;
}

/*
 * Looks for all devices starting with @dev_id. With that, informs how many plugins can be
 * instantiated and gives context for each possible plugin instance.
 */
GArray* get_data_for_possible_plugin_instances_helper(const char *dev_id, const char *plugin)
{
	GArray *data = g_array_new(FALSE, TRUE, sizeof(struct osc_plugin_context *));
	struct iio_context *osc_ctx = get_context_from_osc();
	GArray *devices = get_iio_devices_starting_with(osc_ctx, dev_id);
	guint i = devices->len;

	/*
	 * Let's go backwards as devices are sorted in descending order and we want
	 * devices to pop up in the tabs in ascending order. We also need to make sure
	 * to set the name right so that we are actually controlling the right instance
	 * of the dev.
	 */
	while (i-- > 0) {
		struct osc_plugin_context *context = g_new0(struct osc_plugin_context, 1);
		struct iio_device *dev = g_array_index(devices, struct iio_device*, i);
		char *full_name;
		const char *id;

		if (devices->len > 1)
			full_name = g_strdup_printf("%s-%i", plugin, devices->len - i);
		else
			full_name = g_strdup(plugin);

		id = get_iio_device_label_or_name(dev);

		context->required_devices = g_list_append(context->required_devices, g_strdup(id));
		context->plugin_name = full_name;
		g_array_append_val(data, context);
	}

	g_array_free(devices, FALSE);

	return data;
}
