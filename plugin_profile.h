/**
 * Copyright (C) 2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 */

#ifndef __PLUGIN_PROFILE_H__
#define __PLUGIN_PROFILE_H__

#include <gtk/gtk.h>
#include <iio.h>

/* Profile Element Types */
#define PROFILE_DEVICE_ELEMENT  1
#define PROFILE_DEBUG_ELEMENT   2
#define PROFILE_PLUGIN_ELEMENT  3

typedef struct profile_element {
	int type;
	char *name;
	struct iio_device *device;
	struct iio_channel *channel;
	const char *attribute;
} ProfileElement;

void profile_elements_init(GSList** list);

void profile_elements_free(GSList **list);

int profile_elements_add_dev_attr(GSList **list, struct iio_device *dev,
		struct iio_channel *chn, const char *attribute);

int profile_elements_add_debug_attr(GSList **list, struct iio_device *dev,
		const char *attribute);

int profile_elements_add_plugin_attr(GSList **list, const char *attribute);

#endif
