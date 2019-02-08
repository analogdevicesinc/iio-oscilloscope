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

GArray * get_iio_devices_starting_with(struct iio_context *ctx, const char *sequence);

#endif  /* __IIO_UTILS__ */
