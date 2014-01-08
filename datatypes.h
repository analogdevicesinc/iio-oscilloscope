/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#ifndef __DATA_TYPES__
#define __DATA_TYPES__

#include <glib.h>
#include <errno.h>
#include <stdbool.h>
#include <gtkdatabox.h>
#include <fftw3.h>

#include "iio_utils.h"

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

/* Types of transforms */
enum {
	NO_TRANSFORM_TYPE,
	TIME_TRANSFORM,
	FFT_TRANSFORM,
	CONSTELLATION_TRANSFORM,
	COMPLEX_FFT_TRANSFORM,
	TRANSFORMS_TYPES_COUNT
};

typedef struct _transform Transform;
typedef struct _tr_list TrList;

struct extra_info {
	struct _device_list *device_parent;
	gfloat *data_ref;
	int shadow_of_enabled;
};

struct buffer {
	void *data;
	void *data_copy;
	unsigned int available;
	unsigned int size;
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

struct _device_list {
	char *device_name;
	struct iio_channel_info *channel_list;
	unsigned int num_channels;
	unsigned int sample_count;
	unsigned int shadow_of_sample_count;
	double adc_freq;
	double lo_freq;
	char adc_scale[10];
	void *settings_dialog_builder;
	struct buffer data_buffer;
	unsigned int num_active_channels;
	unsigned int bytes_per_sample;
	unsigned int current_sample;
	gfloat **channel_data;
	int buffer_fd;
};

struct _transform {
	int type_id;
	struct iio_channel_info *channel_parent;
	struct iio_channel_info *channel_parent2;
	gfloat **in_data;
	gfloat *x_axis;
	gfloat *y_axis;
	unsigned *in_data_size;
	unsigned x_axis_size;
	unsigned y_axis_size;	
	bool destroy_x_axis;
	bool destroy_y_axis;
	bool local_output_buf;
	void *graph;
	int *graph_color;
	void *iter_in_treestore;
	int* integer_id;
	bool graph_active;
	bool has_the_marker;
	bool has_invalid_setup;
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
	enum marker_types *marker_type;
};

struct _constellation_settings {
	unsigned int num_samples;
};

Transform* Transform_new(int tr_type);
void Transform_destroy(Transform *tr);
void Transform_resize_x_axis(Transform *tr, int new_size);
void Transform_resize_y_axis(Transform *tr, int new_size);
void Transform_set_in_data_ref(Transform *tr, gfloat **data_ref, unsigned *in_data_size);
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
