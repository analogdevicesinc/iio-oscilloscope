#ifndef __LIBINI2_H__
#define __LIBINI2_H__

#include <iio.h>
#include <stdio.h>
#include <stdint.h>

void update_from_ini(const char *ini_file,
		const char *driver_name, struct iio_device *dev,
		const char * const *whitelist, size_t list_len);

void save_to_ini(FILE *f, const char *driver_name, struct iio_device *dev,
		const char * const *whitelist, size_t list_len);

char * read_token_from_ini(const char *ini_file,
		const char *driver_name, const char *token);

int foreach_in_ini(const char *ini_file,
		int (*cb)(int, const char *, const char *, const char *));

int ini_unroll(const char *input, const char *output);

void write_driver_name_to_ini(FILE *f, const char *driver_name);

#endif /* __LIBINI2_H__ */
