/**
 * Copyright (C) 2012-2015 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 */

#ifndef __FIR_FILTER_H__
#define __FIR_FILTER_H__

#include <gtk/gtk.h>

struct iio_device;

int load_fir_filter(const char *file_name,
		struct iio_device *dev1, struct iio_device *dev2,
		GtkWidget *panel, GtkFileChooser *chooser,
		GtkWidget *fir_filter_en_tx, GtkWidget *enable_fir_filter_rx,
		GtkWidget *enable_fir_filter_rx_tx,
		GtkWidget *disable_all_fir_filters, char *last_fir_filter);

#endif /* __FIR_FILTER_H__ */
