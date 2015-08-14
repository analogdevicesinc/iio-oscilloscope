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
#include <complex.h>
#include <iio.h>

#include "oscplot.h"

#define DEFAULT_PROFILE_NAME ".osc_profile.ini"
#define OSC_INI_SECTION "IIO Oscilloscope"
#define CAPTURE_INI_SECTION OSC_INI_SECTION " - Capture Window"

#define SAVE_CSV 0
#define SAVE_MAT 1
#define SAVE_VSA 2
#define SAVE_PNG 3

extern GtkWidget *capture_graph;
extern gint capture_function;
extern bool str_endswith(const char *str, const char *needle);
extern void math_expression_objects_clean(void);

/* Max 1 Meg (2^20) */
#define MAX_SAMPLES 1048576
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

#ifndef timespeccmp
#define timespeccmp(tsp, usp, cmp) \
	(((tsp)->tv_sec == (usp)->tv_sec) ? \
		((tsp)->tv_nsec cmp (usp)->tv_nsec) : \
		((tsp)->tv_sec cmp (usp)->tv_sec))
#endif

#ifdef DEBUG
#define DBG(fmt, arg...)  printf("DEBUG: %s: " fmt "\n" , __FUNCTION__ , ## arg)
#else
#define DBG(D...)
#endif

struct marker_type {
	gfloat x;
	gfloat y;
	int bin;	/* need to keep this signed, due to the way we calc harmonics */
	bool active;
	char label[6];
	float complex vector;
	float angle;
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
#define SPECTRUM_PLOT 4

#define USE_INTERN_SAMPLING_FREQ -1.0

bool rx_update_device_sampling_freq(const char *device, double freq);
bool rx_update_channel_lo_freq(const char *device, const char *channel,
	double lo_freq);
void dialogs_init(GtkBuilder *builder);
void trigger_dialog_init(GtkBuilder *builder);
void trigger_settings_for_device(GtkBuilder *builder, const char *device);
void application_quit (void);

bool dma_valid_selection(const char *device, unsigned mask, unsigned channel_count);
unsigned global_enabled_channels_mask(struct iio_device *dev);
void add_ch_setup_check_fct(char *device_name, void *fp);
void *find_setup_check_fct_by_devname(const char *dev_name);
bool is_input_device(const struct iio_device *dev);
bool is_output_device(const struct iio_device *dev);

struct iio_context * get_context_from_osc(void);
const void * plugin_get_device_by_reference(const char *device_name);
int plugin_data_capture_size(const char *device);
int plugin_data_capture_of_plot(OscPlot *plot, const char *device,
			gfloat ***cooked_data, struct marker_type **markers_cp);
int plugin_data_capture_num_active_channels(const char *device);
int plugin_data_capture_bytes_per_sample(const char *device);
OscPlot * plugin_find_plot_with_domain(int domain);
enum marker_types plugin_get_plot_marker_type(OscPlot *plot, const char *device);
void plugin_set_plot_marker_type(OscPlot *plot, const char *device, enum marker_types type);
gdouble plugin_get_plot_fft_avg(OscPlot *plot, const char *device);
OscPlot * plugin_get_new_plot(void);
void plugin_osc_stop_capture(void);
void plugin_osc_start_capture(void);
bool plugin_osc_running_state(void);

void save_complete_profile(const char *filename);
void load_complete_profile(const char *filename);

GtkWidget * create_nonblocking_popup(GtkMessageType type,
			const char *title, const char *str, ...);
gint create_blocking_popup(GtkMessageType type, GtkButtonsType button,
			const char *title, const char *str, ...);
gint fru_connect(void);
gint connect_dialog(bool load_profile);

void application_reload(struct iio_context *ctx, bool load_profile);

struct iio_context * osc_create_context(void);

void osc_process_gtk_events(unsigned int msecs);
int osc_test_value(struct iio_context *ctx,
		int line, const char *attribute, const char *value);
int osc_identify_attrib(struct iio_context *ctx, const char *attrib,
		struct iio_device **dev, struct iio_channel **chn,
		const char **attr, bool *debug);
int osc_read_value(struct iio_context *ctx,
		const char *value, long long *out);
FILE * osc_get_log_file(const char *path);
int osc_log_value(struct iio_context *ctx,
		const char *attribute, const char *value);
int osc_plugin_default_handle(struct iio_context *ctx,
		int line, const char *attrib, const char *value,
		int (*driver_handle)(const char *, const char *));

/* Private functions */
extern int load_default_profile(char *filename, bool load_plugins);
extern void do_init(struct iio_context *new_ctx);
extern void create_default_plot(void);
extern GtkWidget * new_plot_cb(GtkMenuItem *item, gpointer user_data);
extern bool check_inifile(const char *filepath);

#endif
