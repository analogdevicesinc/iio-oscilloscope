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

#define IIO_ATTR_MAX_BYTES 16384

GArray * get_iio_devices_starting_with(struct iio_context *ctx, const char *sequence);

GArray * get_iio_channels_naturally_sorted(struct iio_device *dev);

int str_natural_cmp(const char *s1, const char *s2);

#endif  /* __IIO_UTILS__ */
