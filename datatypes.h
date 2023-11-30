/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#ifndef __DATA_TYPES__
#define __DATA_TYPES__

#include <glib.h>
#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <errno.h>
#include <stdbool.h>
#include <fftw3.h>

#include <iio.h>

#define INITIAL_UPDATE TRUE
#define NORMAL_UPDATE FALSE

struct detachable_plugin {
	const struct osc_plugin *plugin;
	gboolean detached_state;
	GtkWidget *detach_attach_button;
	GtkWidget *window;
	gint xpos;
	gint ypos;
};

/* Types of transforms */
enum {
	NO_TRANSFORM_TYPE,
	TIME_TRANSFORM,
	FFT_TRANSFORM,
	CONSTELLATION_TRANSFORM,
	COMPLEX_FFT_TRANSFORM,
	CROSS_CORRELATION_TRANSFORM,
	FREQ_SPECTRUM_TRANSFORM,
	TRANSFORMS_TYPES_COUNT
};

enum plot_channel_constraints {
	CONSTR_CHN_INITIAL_ENABLED = 1 << 0,  /* The channel initial state is owerwritten to ENABLE state */
	CONSTR_CHN_UNTOGGLEABLE = 1 << 1,     /* The channel state cannot be changed and is displayed as grey-out */
};

typedef struct _transform Transform;
typedef struct _tr_list TrList;

struct extra_info {
	struct iio_device *dev;
	gfloat *data_ref;
	off_t offset;
	int shadow_of_enabled;
	bool may_be_enabled;
	double lo_freq;
	unsigned int constraints;
};

struct extra_dev_info {
	bool input_device;
	struct iio_buffer *buffer;
	unsigned int sample_count;
	unsigned int buffer_size;
	unsigned int channel_trigger;
	bool channel_trigger_enabled;
	bool trigger_falling_edge;
	float trigger_value;
	double adc_freq;
	char adc_scale;
	gfloat **channels_data_copy;
	GSList *plots_sample_counts;
};

struct buffer {
	void *data;
	void *data_copy;
	unsigned int available;
	unsigned int size;
};

struct plot_params{
	int plot_id;
	unsigned int sample_count;
};

struct _fft_alg_data{
	gfloat fft_corr;
	double *in;
	double *win;
	int m;			/* size of fft; -1 if not initialized */
	fftw_complex *in_c;
	fftw_complex *out;
	fftw_plan plan_forward;
	int cached_fft_size;
	int cached_num_active_channels;
	int num_active_channels;
};

struct _transform {
	GtkDataboxGraph *graph;
	int type_id;
	GSList *plot_channels;
	int plot_channels_type;
	gfloat *x_axis;
	gfloat *y_axis;
	unsigned x_axis_size;
	unsigned y_axis_size;
	bool destroy_x_axis;
	bool destroy_y_axis;
	GdkRGBA *graph_color;
	bool has_the_marker;
	void *settings;
	bool (*transform_function)(Transform *tr, gboolean init_transform);
};

struct _tr_list {
	Transform **transforms;
	int size;
};

struct _time_settings {
	gfloat *data_source;
	unsigned int num_samples;
	gfloat max_x_axis;
	gboolean apply_inverse_funct;
	gboolean apply_multiply_funct;
	gboolean apply_add_funct;
	gfloat multiply_value;
	gfloat add_value;
};

struct _fft_settings {
	gfloat *real_source;
	gfloat *imag_source;
	unsigned int fft_size;
	gchar *fft_win;
	unsigned int fft_avg;
	gfloat fft_pwr_off;
	struct _fft_alg_data fft_alg_data;
	struct marker_type *markers;
	struct marker_type **markers_copy;
	GMutex *marker_lock;
	enum marker_types *marker_type;
	bool window_correction;
};

struct _constellation_settings {
	gfloat *x_source;
	gfloat *y_source;
	unsigned int num_samples;
};

struct _cross_correlation_settings {
	gfloat *i0_source;
	gfloat *q0_source;
	gfloat *i1_source;
	gfloat *q1_source;
	unsigned int num_samples;
	gfloat max_x_axis;
	unsigned int avg;
	int revert_xcorr;
	fftw_complex *signal_a;
	fftw_complex *signal_b;
	fftw_complex *xcorr_data;
	struct marker_type *markers;
	struct marker_type **markers_copy;
	GMutex *marker_lock;
	enum marker_types *marker_type;
};

struct _freq_spectrum_settings {
	gfloat *real_source;
	gfloat *imag_source;
	gfloat *freq_axis_source;
	gfloat *magn_axis_source;
	gchar *fft_win;
	unsigned freq_axis_size;
	unsigned magn_axis_size;
	unsigned fft_index;
	unsigned fft_count;
	double freq_sweep_start;
	double filter_bandwidth;
	unsigned int fft_size;
	unsigned int fft_avg;
	gfloat fft_pwr_off;
	unsigned fft_lower_clipping_limit;
	unsigned fft_upper_clipping_limit;
	struct _fft_alg_data *ffts_alg_data;
	gfloat fft_corr;
	unsigned int *maxXaxis;
	gfloat *maxYaxis;
	struct marker_type *markers;
	struct marker_type **markers_copy;
	GMutex *marker_lock;
	enum marker_types *marker_type;
	bool window_correction;
};

Transform* Transform_new(int tr_type);
void Transform_destroy(Transform *tr);
void Transform_resize_x_axis(Transform *tr, int new_size);
void Transform_resize_y_axis(Transform *tr, int new_size);
gfloat* Transform_get_x_axis_ref(Transform *tr);
gfloat* Transform_get_y_axis_ref(Transform *tr);
void Transform_attach_settings(Transform *tr, void *settings);
void Transform_attach_function(Transform *tr, bool (*f)(Transform *tr , gboolean init_transform));
void Transform_setup(Transform *tr);
bool Transform_update_output(Transform *tr);

TrList* TrList_new(void);
void TrList_destroy(TrList *list);
void TrList_add_transform(TrList *list, Transform *tr);
void TrList_remove_transform(TrList *list, Transform *tr);

#endif /* __DATA_TYPES__ */
