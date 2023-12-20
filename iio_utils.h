/**
 * Copyright (C) 2019 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#ifndef __IIO_UTILS__
#define __IIO_UTILS__

#include "iio.h"
#include <gmodule.h>

struct _GtkToggleToolButton;
struct _GtkWidget;

#define IIO_ATTR_MAX_BYTES 16384

GArray * get_iio_devices_starting_with(struct iio_context *ctx, const char *sequence);

GArray * get_iio_channels_naturally_sorted(struct iio_device *dev);

int str_natural_cmp(const char *s1, const char *s2);

void handle_toggle_section_cb(struct _GtkToggleToolButton *btn, struct _GtkWidget *section);

const char *get_iio_device_label_or_name(const struct iio_device *dev);
bool iio_attr_not_found(struct iio_device *dev, struct iio_channel *chn, const char *attr_name);

#endif  /* __IIO_UTILS__ */
