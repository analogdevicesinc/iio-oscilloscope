/**
 * Copyright (C) 2015 Analog Devices, Inc.
 *
 */

#ifndef __AD9361_MULTICHIP_SNYC__
#define __AD9361_MULTICHIP_SNYC__

#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include <iio.h>

#define MAX_AD9361_SYNC_DEVS	4

/* FLAGS */

#define FIXUP_INTERFACE_TIMING	1
#define CHECK_SAMPLE_RATES	2
#define MCS_IS_DEBUG_ATTR	4

int ad9361_multichip_sync(struct iio_device **devices, int num, unsigned flags);

#endif /* __AD9361_MULTICHIP_SNYC__ */
