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
#include <errno.h>
#include <stdbool.h>
#include <fftw3.h>

#include <iio.h>

#define FORCE_UPDATE TRUE
#define NORMAL_UPDATE FALSE

struct detachable_plugin {
	const struct osc_plugin *plugin;
	gboolean detached_state;
	GtkWidget *detach_attach_button;
};

/* Types of transforms */
enum {
	NO_TRANSFORM_TYPE,
	TIME_TRANSFORM,
	FFT_TRANSFORM,
	CONSTELLATION_TRANSFORM,
	COMPLEX_FFT_TRANSFORM,
	CROSS_CORRELATION_TRANSFORM,
	TRANSFORMS_TYPES_COUNT
};

typedef struct _transform Transform;
typedef struct _tr_list TrList;

struct extra_info {
	struct iio_device *dev;
	gfloat *data_ref;
	off_t offset;
	int shadow_of_enabled;
	bool may_be_enabled;
};

struct extra_dev_info {
	bool input_device;
	struct iio_buffer *buffer;
	unsigned int sample_count;
	double adc_freq, lo_freq;
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
	unsigned int plot_id;
	unsigned int sample_count;
};

struct _fft_alg_data{
	gfloat fft_corr;
	double *in;
	double *win;
	unsigned int m;
	fftw_complex *in_c;
	fftw_complex *out;
	fftw_plan plan_forward;
	int cached_fft_size;
	int cached_num_active_channels;
	int num_active_channels;
};

struct _transform {
	int type_id;
	struct iio_channel *channel_parent,
			   *channel_parent2,
			   *channel_parent3,
			   *channel_parent4;
	gfloat **in_data;
	gfloat *x_axis;
	gfloat *y_axis;
	unsigned x_axis_size;
	unsigned y_axis_size;
	bool destroy_x_axis;
	bool destroy_y_axis;
	bool local_output_buf;
	GdkColor *graph_color;
	bool has_the_marker;
	void *settings;
	void (*transform_function)(Transform *tr, gboolean init_transform);
};

struct _tr_list {
	Transform **transforms;
	int size;
};

struct _time_settings {
	unsigned int num_samples;
	gboolean apply_inverse_funct;
	gboolean apply_multiply_funct;
	gboolean apply_add_funct;
	gfloat multiply_value;
	gfloat add_value;
};

struct _fft_settings {
	unsigned int fft_size;
	unsigned int fft_avg;
	gfloat fft_pwr_off;
	struct _fft_alg_data fft_alg_data;
	struct marker_type *markers;
	struct marker_type **markers_copy;
	GMutex *marker_lock;
	enum marker_types *marker_type;
};

struct _constellation_settings {
	unsigned int num_samples;
};

struct _cross_correlation_settings {
	unsigned int num_samples;
	int revert_xcorr;
	fftw_complex *signal_a;
	fftw_complex *signal_b;
	fftw_complex *xcorr_data;
	struct marker_type *markers;
	struct marker_type **markers_copy;
	GMutex *marker_lock;
	enum marker_types *marker_type;
};

Transform* Transform_new(int tr_type);
void Transform_destroy(Transform *tr);
void Transform_resize_x_axis(Transform *tr, int new_size);
void Transform_resize_y_axis(Transform *tr, int new_size);
void Transform_set_in_data_ref(Transform *tr, gfloat **data_ref);
gfloat* Transform_get_x_axis_ref(Transform *tr);
gfloat* Transform_get_y_axis_ref(Transform *tr);
void Transform_attach_settings(Transform *tr, void *settings);
void Transform_attach_function(Transform *tr, void (*f)(Transform *tr , gboolean init_transform));
void Transform_setup(Transform *tr);
void Transform_update_output(Transform *tr);

TrList* TrList_new(void);
void TrList_destroy(TrList *list);
void TrList_add_transform(TrList *list, Transform *tr);
void TrList_remove_transform(TrList *list, Transform *tr);

#endif /* __DATA_TYPES__ */
