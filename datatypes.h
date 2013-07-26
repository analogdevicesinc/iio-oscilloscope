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
	void *settings_window;
};

struct _transform {
	gfloat *in_data;
	gfloat *out_data;
	
	int in_data_size;
	int out_data_size;
	
	void *graph;
	
	void (*transform_function)(gfloat *in_data, gfloat *out_data);
};

struct _tr_list {
	Transform **transforms;
	int size;
};

Transform* Transform_new(void);
void Transform_destroy(Transform *tr);
void Transform_resize_out_buffer(Transform *tr, int new_size);
void Transform_set_in_data_ref(Transform *tr, gfloat *data_ref);
gfloat* Transform_get_out_data_ref(Transform *tr);
void Transform_attach_function(Transform *tr, void (*f)(gfloat *in_data, gfloat *out_data));
void Transform_update_output(Transform *tr);

TrList* TrList_new(void);
void TrList_destroy(TrList *list);
void TrList_add_transform(TrList *list, Transform *tr);
void TrList_remove_transform(TrList *list, Transform *tr);

#endif /* __DATA_TYPES__ */
