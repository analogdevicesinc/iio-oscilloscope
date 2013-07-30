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

#include "iio_utils.h"

typedef struct _transform Transform;
typedef struct _tr_list TrList;

struct _device_list {
	char *device_name;
	struct iio_channel_info *channel_list;
	unsigned num_channels;
	unsigned sample_count;
	void *settings_dialog_builder;
};

struct _transform {
	gfloat *in_data;
	gfloat *out_data;
	gfloat *x_axis;
	
	unsigned *in_data_size;
	unsigned out_data_size;
	
	void *graph;
	
	void *settings;
	
	void (*transform_function)(Transform *tr, gboolean init_transform);
};

struct _tr_list {
	Transform **transforms;
	int size;
};

struct _fft_settings {
	int fft_size;
	int fft_avg;
	gfloat fft_pwr_off;
};

struct _constellation_settings {
	struct iio_channel_info *ch0;
	struct iio_channel_info *ch1;
};

Transform* Transform_new(void);
void Transform_destroy(Transform *tr);
void Transform_resize_out_buffer(Transform *tr, int new_size);
void Transform_set_in_data_ref(Transform *tr, gfloat *data_ref, unsigned *in_data_size);
gfloat* Transform_get_out_data_ref(Transform *tr);
gfloat* Transform_get_x_axis_ref(Transform *tr);
void Transform_attach_settings(Transform *tr, void *settings);
void Transform_attach_function(Transform *tr, void (*f)(Transform *tr , gboolean init_transform));
void Transform_setup(Transform *tr);
void Transform_update_output(Transform *tr);

TrList* TrList_new(void);
void TrList_destroy(TrList *list);
void TrList_add_transform(TrList *list, Transform *tr);
void TrList_remove_transform(TrList *list, Transform *tr);

#endif /* __DATA_TYPES__ */
