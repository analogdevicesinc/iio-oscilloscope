/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdlib.h>
#include <string.h>
#include "datatypes.h"

Transform* Transform_new(int type)
{
	Transform *tr = (Transform *)calloc(1, sizeof(Transform));

	tr->type_id = (type > NO_TRANSFORM_TYPE &&
			type < TRANSFORMS_TYPES_COUNT) ? type
			: NO_TRANSFORM_TYPE;

	return tr;
}

void Transform_destroy(Transform *tr)
{
	if (tr) {
		if (tr->x_axis && tr->destroy_x_axis) {
			free(tr->x_axis);
			tr->x_axis = NULL;
		}
		if (tr->y_axis && tr->destroy_y_axis) {
			free(tr->y_axis);
			tr->y_axis = NULL;
		}
		if (tr->settings) {
			free(tr->settings);
			tr->settings = NULL;
		}
		if (tr->plot_channels) {
			g_slist_free(tr->plot_channels);
			tr->plot_channels = NULL;
		}
		free(tr);
	}
}

void Transform_resize_x_axis(Transform *tr, int new_size)
{
	tr->destroy_x_axis = true;

	if (new_size > 0) {
		tr->x_axis_size = new_size;
		tr->x_axis = (gfloat *) realloc(tr->x_axis, sizeof(gfloat) * new_size);
	} else {
		if (tr->x_axis)
			free(tr->x_axis);
		tr->x_axis_size = 0;
		tr->x_axis = NULL;
	}
}

void Transform_resize_y_axis(Transform *tr, int new_size)
{
	if (tr->destroy_y_axis == false)
		tr->y_axis = NULL;
	tr->destroy_y_axis = true;

	if (new_size > 0) {
		tr->y_axis_size = new_size;
		tr->y_axis = (gfloat *) realloc(tr->y_axis, sizeof(gfloat) * new_size);
		memset(tr->y_axis, 0, sizeof(gfloat) * new_size);
	} else {
		if (tr->y_axis)
			free(tr->y_axis);
		tr->y_axis_size = 0;
		tr->y_axis = NULL;
	}
}

gfloat* Transform_get_x_axis_ref(Transform *tr)
{
	return tr->x_axis;
}

gfloat* Transform_get_y_axis_ref(Transform *tr)
{
	return tr->y_axis;
}

void Transform_attach_settings(Transform *tr, void *settings)
{
	tr->settings = settings;
}

void Transform_attach_function(Transform *tr, bool (*f)(Transform *tr , gboolean init_transform))
{
	tr->transform_function = f;
}

void Transform_setup(Transform *tr)
{
	tr->transform_function(tr, TRUE);
}

bool Transform_update_output(Transform *tr)
{
	return tr->transform_function(tr, FALSE);
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
	if (list->size) {
		list->transforms = (Transform **) realloc(list->transforms,
				sizeof(Transform *) * list->size);
	} else {
		free(list->transforms);
		list->transforms = NULL;
	}
}
