/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __OSC_H__
#define __OSC_H__
#define IIO_THREADS

#include <gtkdatabox.h>
#include <iio.h>

#include "oscplot.h"

#define MULTI_OSC "MultiOsc"
#define CAPTURE_CONF MULTI_OSC"_Capture_Configuration"

#define SAVE_CSV 2
#define SAVE_PNG 3
#define SAVE_MAT 4
#define SAVE_VSA 5

extern GtkWidget *capture_graph;
extern gint capture_function;
extern bool str_endswith(const char *str, const char *needle);

#define TMP_INI_FILE "/tmp/.%s.tmp"
#ifndef MAX_MARKERS
#define MAX_MARKERS 10
#endif

#define OFF_MRK    "Markers Off"
#define PEAK_MRK   "Peak Markers"
#define FIX_MRK    "Fixed Markers"
#define SINGLE_MRK "Single Tone Markers"
#define DUAL_MRK   "Two Tone Markers"
#define IMAGE_MRK  "Image Markers"
#define ADD_MRK    "Add Marker"
#define REMOVE_MRK "Remove Marker"

struct marker_type {
	gfloat x;
	gfloat y;
	int bin;
	bool active;
	GtkDataboxGraph *graph;
};

enum marker_types {
	MARKER_OFF,
	MARKER_PEAK,
	MARKER_FIXED,
	MARKER_ONE_TONE,
	MARKER_TWO_TONE,
	MARKER_IMAGE,
	MARKER_NULL
};

#define TIME_PLOT 0
#define FFT_PLOT 1
#define XY_PLOT 2
#define XCORR_PLOT 3

void rx_update_labels(void);
void dialogs_init(GtkBuilder *builder);
void trigger_dialog_init(GtkBuilder *builder);
void trigger_settings_for_device(GtkBuilder *builder, const char *device);
void application_quit (void);

void add_ch_setup_check_fct(char *device_name, void *fp);
void *find_setup_check_fct_by_devname(const char *dev_name);
bool is_input_device(const struct iio_device *dev);
bool is_output_device(const struct iio_device *dev);

struct iio_context * get_context_from_osc(void);
const void * plugin_get_device_by_reference(const char *device_name);
int plugin_data_capture_size(const char *device);
int plugin_data_capture_with_domain(const char *device, gfloat ***cooked_data,
			struct marker_type **markers_cp, int domain);
int plugin_data_capture_num_active_channels(const char *device);
int plugin_data_capture_bytes_per_sample(const char *device);
OscPlot * plugin_find_plot_with_domain(int domain);
enum marker_types plugin_get_plot_marker_type(OscPlot *plot, const char *device);
void plugin_set_plot_marker_type(OscPlot *plot, const char *device, enum marker_types type);
gdouble plugin_get_plot_fft_avg(OscPlot *plot, const char *device);
OscPlot * plugin_get_new_plot(void);
void plugin_data_capture_revert_xcorr(OscPlot *, int);
void capture_profile_save(const char *filename);
void main_setup_before_ini_load(void);
void main_setup_after_ini_load(void);
int main_profile_handler(const char *section, const char *name, const char *value);
int capture_profile_handler(const char *section, const char *name, const char *value);
void save_all_plugins(const char *filename, gpointer user_data);
int restore_all_plugins(const char *filename, gpointer user_data);

GtkWidget * create_nonblocking_popup(GtkMessageType type,
			const char *title, const char *str, ...);
gint create_blocking_popup(GtkMessageType type, GtkButtonsType button,
			const char *title, const char *str, ...);
gint fru_connect(void);

struct iio_context * osc_create_context(void);

#endif
