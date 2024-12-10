/**
 * Copyright (C) 2019 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#ifndef __IIO_UTILS__
#define __IIO_UTILS__

#include "iio/iio.h"
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

/* Helpers to read from iio attributes of devices */
inline int dev_attr_read_raw(struct iio_device *dev, const char *attr_name, char *dst, size_t len);
inline int dev_attr_read_double(struct iio_device *dev, const char *attr_name, double *value);
inline int dev_attr_read_longlong(struct iio_device *dev, const char *attr_name, long long *value);
inline int dev_debug_attr_read_raw(struct iio_device *dev, const char *attr_name, char *dst, size_t len);
inline int dev_debug_attr_read_longlong(struct iio_device *dev, const char *attr_name, long long *value);

/* Helpers to write to iio attributes of devices */
inline int dev_attr_write_raw(struct iio_device *dev, const char *attr_name, const char *src, size_t len);
inline int dev_attr_write_double(struct iio_device *dev, const char *attr_name, double value);
inline int dev_attr_write_longlong(struct iio_device *dev, const char *attr_name, long long value);
inline int dev_debug_attr_write_string(struct iio_device *dev, const char *attr_name, const char *value);
inline int dev_debug_attr_write_longlong(struct iio_device *dev, const char *attr_name, long long value);

/* Helpers to read from iio attributes of channels */
inline int chn_attr_read_raw(struct iio_channel *chn, const char *attr_name, char *dst, size_t len);
inline int chn_attr_read_bool(struct iio_channel *chn, const char *attr_name, bool *value);
inline int chn_attr_read_double(struct iio_channel *chn, const char *attr_name, double *value);
inline int chn_attr_read_longlong(struct iio_channel *chn, const char *attr_name, long long *value);

/* Helpers to write to iio attributes of channels */
inline int chn_attr_write_string(struct iio_channel *chn, const char *attr_name, const char *string);
inline int chn_attr_write_bool(struct iio_channel *chn, const char *attr_name, bool value);
inline int chn_attr_write_double(struct iio_channel *chn, const char *attr_name, double value);
inline int chn_attr_write_longlong(struct iio_channel *chn, const char *attr_name, long long value);

/* Helpers to iterate through all attributes */
inline void dev_attr_read_all(struct iio_device *dev,
    int (*cb)(struct iio_device *dev, const char *attr, const char *value, size_t len, void *d),
    void *data);
inline int dev_debug_attr_read_all(struct iio_device *dev,
    int (*cb)(struct iio_device *dev, const char *attr, const char *value, size_t len, void *d),
    void *data);
inline void chn_attr_read_all(struct iio_channel *chn,
    int (*cb)(struct iio_channel *chn, const char *attr, const char *value, size_t len, void *d),
    void *data);

#endif  /* __IIO_UTILS__ */
