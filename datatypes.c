#include <malloc.h>
#include "datatypes.h"

Transform* Transform_new(void)
{
	Transform *tr = (Transform *)malloc(sizeof(Transform));
	
	tr->in_data = NULL;
	tr->out_data = NULL;
	tr->in_data_size = 0;
	tr->out_data_size = 0;
	
	return tr;
}

void Transform_destroy(Transform *tr)
{
	free(tr->out_data);
	free(tr);
}

void Transform_resize_out_buffer(Transform *tr, int new_size)
{
	tr->out_data_size = (new_size >= 0) ? new_size : 0;
	tr->out_data = (gfloat *)realloc(tr->out_data, sizeof(gfloat) * tr->out_data_size);
}

void Transform_set_in_data_ref(Transform *tr, gfloat *data_ref)
{
	tr->in_data = data_ref;
}

gfloat* Transform_get_out_data_ref(Transform *tr)
{
	return tr->out_data;
}

void Transform_attach_function(Transform *tr, void (*f)(gfloat *in_data, gfloat *out_data))
{
	tr->transform_function = f;
}

void Transform_update_output(Transform *tr)
{
	tr->transform_function(tr->in_data, tr->out_data);
}

TrList* TrList_new(void)
{
	TrList *list = (TrList *)malloc(sizeof(TrList));
	
	list->transforms = NULL;
	list->size = 0;
	
	return list;
}

void TrList_destroy(TrList *list)
{
	free(list->transforms);
	free(list);
}

void TrList_add_transform(TrList *list, Transform *tr)
{
	list->size++;
	list->transforms = (Transform **)realloc(list->transforms, sizeof(Transform *) * list->size);
	list->transforms[list->size - 1] = tr;
}

void TrList_remove_transform(TrList *list, Transform *tr)
{
	int n = 0;
	int i = 0;
	
	/* Find the transform that needs to be deleted */
	while ((n < list->size) && (list->transforms[n] != tr)) {
		n++;
	}
	/* Shift the remaining list one position, so that the transform is deleted */
	for (i = n; i < (list->size - 1); i++) {
		list->transforms[i] = list->transforms[i + 1];
	}
	list->size--;
	list->transforms = (Transform **)realloc(list->transforms, sizeof(Transform *) * list->size);
}