/* IIO - useful set of util functionality
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
*/

#ifndef __IIO_UTILS_H__
#define __IIO_UTILS_H__

/* Made up value to limit allocation sizes */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <dirent.h>
#include <error.h>

#define IIO_MAX_NAME_LENGTH 30

#define FORMAT_SCAN_ELEMENTS_DIR "%s/scan_elements"
#define FORMAT_TYPE_FILE "%s_type"

#define ACCESS_NORM 0
#define ACCESS_DBFS 1

static const char *iio_dir = "/sys/bus/iio/devices/";
static const char *iio_debug_dir = "/sys/kernel/debug/iio/";

/**
 * iioutils_break_up_name() - extract generic name from full channel name
 * @full_name: the full channel name
 * @generic_name: the output generic channel name
 **/
static int iioutils_break_up_name(const char *full_name,
				char **generic_name)
{
	char *current;
	char *w, *r;
	char *working;
	current = strdup(full_name);
	working = strtok(current, "_\0");
	w = working;
	r = working;

	while (*r != '\0') {
		if (!isdigit(*r)) {
			*w = *r;
			w++;
		}
		r++;
	}
	*w = '\0';
	*generic_name = strdup(working);
	free(current);

	return 0;
}

enum iio_endian {
	IIO_BE,
	IIO_LE,
};

/**
 * struct iio_channel_info - information about a given channel
 * @name: channel name
 * @generic_name: general name for channel type
 * @scale: scale factor to be applied for conversion to si units
 * @offset: offset to be applied for conversion to si units
 * @index: the channel index in the buffer output
 * @bytes: number of bytes occupied in buffer output
 * @mask: a bit mask for the raw output
 * @is_signed: is the raw value stored signed
 * @enabled: is this channel enabled
 **/
struct iio_channel_info {
	char *name;
	char *generic_name;
	float scale;
	float offset;
	unsigned index;
	unsigned bytes;
	unsigned bits_used;
	unsigned shift;
	uint64_t mask;
	unsigned is_signed;
	unsigned enabled;
	enum iio_endian endianness;
	void *extra_field;
};

/**
 * iioutils_get_type() - find and process _type attribute data
 * @is_signed: output whether channel is signed
 * @bytes: output how many bytes the channel storage occupies
 * @mask: output a bit mask for the raw data
 * @device_dir: the iio device directory
 * @name: the channel name
 * @generic_name: the channel type name
 **/
static inline int iioutils_get_type(unsigned *is_signed,
			unsigned *bytes,
			unsigned *bits_used,
			unsigned *shift,
			uint64_t *mask,
			enum iio_endian *endianness,
			const char *device_dir,
			const char *name,
			const char *generic_name)
{
	FILE *sysfsfp;
	int ret, tmp;
	DIR *dp;
	char *scan_el_dir, *builtname, *builtname_generic, *filename = 0;
	char signchar;
	unsigned padint;
	const struct dirent *ent;
	char str_endianness[3];

	ret = asprintf(&scan_el_dir, FORMAT_SCAN_ELEMENTS_DIR, device_dir);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = asprintf(&builtname, FORMAT_TYPE_FILE, name);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_free_scan_el_dir;
	}
	ret = asprintf(&builtname_generic, FORMAT_TYPE_FILE, generic_name);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_free_builtname;
	}

	dp = opendir(scan_el_dir);
	if (dp == NULL) {
		ret = -errno;
		goto error_free_builtname_generic;
	}
	while (ent = readdir(dp), ent != NULL)
		/*
		 * Do we allow devices to override a generic name with
		 * a specific one?
		 */
		if ((strcmp(builtname, ent->d_name) == 0) ||
		    (strcmp(builtname_generic, ent->d_name) == 0)) {
			ret = asprintf(&filename,
					"%s/%s", scan_el_dir, ent->d_name);
			if (ret < 0) {
				ret = -ENOMEM;
				goto error_closedir;
			}
			sysfsfp = fopen(filename, "r");
			if (sysfsfp == NULL) {
				fprintf(stderr, "failed to open %s\n", filename);
				ret = -errno;
				goto error_free_filename;
			}
			tmp = fread(str_endianness, 3, 1, sysfsfp);
			if (tmp != 1) {
				ret = -ENODEV;
				goto error_free_filename;
			}
			tmp = fscanf(sysfsfp,
					"%c%u/%u>>%u", &signchar, bits_used,
					&padint, shift);
			if (tmp != 4) {
				ret = -ENODEV;
				goto error_free_filename;
			}
			*bytes = padint / 8;
			if (*bits_used == 64)
				*mask = ~0;
			else
				*mask = (1 << *bits_used) - 1;
			if (signchar == 's')
				*is_signed = 1;
			else
				*is_signed = 0;
			if (strncmp(str_endianness, "be", 2) == 0)
				*endianness = IIO_BE;
			else
				*endianness = IIO_LE;
		}
error_free_filename:
	if (filename)
		free(filename);
error_closedir:
	closedir(dp);
error_free_builtname_generic:
	free(builtname_generic);
error_free_builtname:
	free(builtname);
error_free_scan_el_dir:
	free(scan_el_dir);
error_ret:
	return ret;
}

static inline int iioutils_get_param_float(float *output,
				const char *param_name,
				const char *device_dir,
				const char *name,
				const char *generic_name)
{
	FILE *sysfsfp;
	int ret, tmp;
	DIR *dp;
	char *builtname, *builtname_generic;
	char *filename = NULL;
	const struct dirent *ent;

	ret = asprintf(&builtname, "%s_%s", name, param_name);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_ret;
	}
	ret = asprintf(&builtname_generic,
			"%s_%s", generic_name, param_name);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_free_builtname;
	}
	dp = opendir(device_dir);
	if (dp == NULL) {
		ret = -errno;
		goto error_free_builtname_generic;
	}
	while (ent = readdir(dp), ent != NULL)
		if ((strcmp(builtname, ent->d_name) == 0) ||
		    (strcmp(builtname_generic, ent->d_name) == 0)) {
			ret = asprintf(&filename,
					"%s/%s", device_dir, ent->d_name);
			if (ret < 0) {
				ret = -ENOMEM;
				goto error_closedir;
			}
			sysfsfp = fopen(filename, "r");
			if (!sysfsfp) {
				ret = -errno;
				goto error_free_filename;
			}
			tmp = fscanf(sysfsfp, "%f", output);
			if (tmp != 1) {
				*output = 0.0f;
				ret = -ENODEV;
			}
			break;
		}
error_free_filename:
	if (filename)
		free(filename);
error_closedir:
	closedir(dp);
error_free_builtname_generic:
	free(builtname_generic);
error_free_builtname:
	free(builtname);
error_ret:
	return ret;
}

/**
 * bsort_channel_array_by_index() - reorder so that the array is in index order
 *
 **/

static inline void bsort_channel_array_by_index(struct iio_channel_info **ci_array,
					 int cnt)
{

	struct iio_channel_info temp;
	int x, y;

	for (x = 0; x < cnt; x++)
		for (y = 0; y < (cnt - 1); y++)
			if ((*ci_array)[y].index > (*ci_array)[y+1].index) {
				temp = (*ci_array)[y + 1];
				(*ci_array)[y + 1] = (*ci_array)[y];
				(*ci_array)[y] = temp;
			}
}

/**
 * build_channel_array() - function to figure out what channels are present
 * @device_dir: the IIO device directory in sysfs
 * @
 **/
static inline int build_channel_array(const char *device_dir,
			struct iio_channel_info **ci_array,
			unsigned int *counter)
{
	DIR *dp;
	FILE *sysfsfp;
	int count, i, tmp;
	struct iio_channel_info *current;
	int ret;
	const struct dirent *ent;
	char *scan_el_dir;
	char *filename;

	*counter = 0;
	ret = asprintf(&scan_el_dir, FORMAT_SCAN_ELEMENTS_DIR, device_dir);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_ret;
	}
	dp = opendir(scan_el_dir);
	if (dp == NULL) {
		ret = -errno;
		goto error_free_name;
	}
	while (ent = readdir(dp), ent != NULL) {
		if (strcmp(ent->d_name + strlen(ent->d_name) - strlen("_en"),
			   "_en") == 0) {
			(*counter)++;
		}
	}
	*ci_array = malloc(sizeof(**ci_array) * (*counter));
	if (*ci_array == NULL) {
		ret = -ENOMEM;
		goto error_close_dir;
	}
	seekdir(dp, 0);
	count = 0;
	while (ent = readdir(dp), ent != NULL) {
		if (strcmp(ent->d_name + strlen(ent->d_name) - strlen("_en"),
			   "_en") == 0) {
			current = &(*ci_array)[count++];
			ret = asprintf(&filename,
					"%s/%s", scan_el_dir, ent->d_name);
			if (ret < 0) {
				ret = -ENOMEM;
				/* decrement count to avoid freeing name */
				count--;
				goto error_cleanup_array;
			}
			sysfsfp = fopen(filename, "r");
			if (sysfsfp == NULL) {
				free(filename);
				ret = -errno;
				goto error_cleanup_array;
			}
			tmp = fscanf(sysfsfp, "%u", &current->enabled);
			fclose(sysfsfp);
			if (tmp != 1)
				current->enabled = 0;

			current->scale = 1.0;
			current->offset = 0;
			current->name = strndup(ent->d_name,
						strlen(ent->d_name) -
						strlen("_en"));
			if (current->name == NULL) {
				free(filename);
				ret = -ENOMEM;
				goto error_cleanup_array;
			}
			/* Get the generic and specific name elements */
			ret = iioutils_break_up_name(current->name,
						&current->generic_name);
			if (ret) {
				free(filename);
				goto error_cleanup_array;
			}
			ret = asprintf(&filename,
					"%s/%s_index",
					scan_el_dir,
					current->name);
			if (ret < 0) {
				free(filename);
				ret = -ENOMEM;
				goto error_cleanup_array;
			}
			sysfsfp = fopen(filename, "r");
			tmp = fscanf(sysfsfp, "%u", &current->index);
			fclose(sysfsfp);
			if (tmp != 1) {
				current->enabled = 0;
				current->index = -1;
			}
			free(filename);
			/* Find the scale */
			ret = iioutils_get_param_float(&current->scale,
						"scale",
						device_dir,
						current->name,
						current->generic_name);
			if (ret < 0)
				goto error_cleanup_array;
			ret = iioutils_get_param_float(&current->offset,
						"offset",
						device_dir,
						current->name,
						current->generic_name);
			if (ret < 0)
				goto error_cleanup_array;
			ret = iioutils_get_type(&current->is_signed,
						&current->bytes,
						&current->bits_used,
						&current->shift,
						&current->mask,
						&current->endianness,
						device_dir,
						current->name,
						current->generic_name);
		}
	}

	closedir(dp);
	/* reorder so that the array is in index order */
	bsort_channel_array_by_index(ci_array, *counter);

	return 0;

error_cleanup_array:
	for (i = count - 1;  i >= 0; i--)
		free((*ci_array)[i].name);
	free(*ci_array);
error_close_dir:
	closedir(dp);
error_free_name:
	free(scan_el_dir);
error_ret:
	return ret;
}

static inline void free_channel_array(struct iio_channel_info *ci_array,
				int num_channels)
{
	int i;

	for (i = 0;  i < num_channels; i++) {
		free(ci_array[i].name);
		free(ci_array[i].generic_name);
	}
	free(ci_array);
}

/**
 * find_type_by_name() - function to match top level types by name
 * @name: top level type instance name
 * @type: the type of top level instance being sort
 *
 * Typical types this is used for are device and trigger.
 **/
static inline int find_type_by_name(const char *name, const char *type)
{
	const struct dirent *ent;
	int number, numstrlen, tmp;

	FILE *nameFile;
	DIR *dp;
	char thisname[IIO_MAX_NAME_LENGTH];
	char *filename;

	dp = opendir(iio_dir);
	if (dp == NULL) {
		fprintf(stderr, "No Industrial IO devices available to %s\n", __func__);
		return -ENODEV;
	}

	while (ent = readdir(dp), ent != NULL) {
		if (strcmp(ent->d_name, ".") != 0 &&
			strcmp(ent->d_name, "..") != 0 &&
			strlen(ent->d_name) > strlen(type) &&
			strncmp(ent->d_name, type, strlen(type)) == 0) {
			numstrlen = sscanf(ent->d_name + strlen(type),
					   "%d",
					   &number);
			/* verify the next character is not a colon */
			if (strncmp(ent->d_name + strlen(type) + numstrlen,
					":",
					1) != 0) {
				filename = malloc(strlen(iio_dir)
						+ strlen(type)
						+ numstrlen
						+ 6);
				if (filename == NULL) {
					closedir(dp);
					return -ENOMEM;
				}
				sprintf(filename, "%s%s%d/name",
					iio_dir,
					type,
					number);
				nameFile = fopen(filename, "r");
				if (!nameFile)
					continue;
				free(filename);
				tmp = fscanf(nameFile, "%s", thisname);
				fclose(nameFile);
				if (tmp != 1)
					continue;
				if (strcmp(name, thisname) == 0) {
					closedir(dp);
					return number;
				}
			}
		}
	}
	closedir(dp);

	return -ENODEV;
}

/**
 *  find_names() - function to find names installed on the system
 *  @name: null char delimited names
 *  @filter: Allows to filter by device type
 *
 *  Returns the number of devices found
 **/
static inline int find_iio_names(char **names, const char *filter)
{
	DIR *dp;
	const struct dirent *ent;
	FILE *nameFile;
	char *filename, *name_str=NULL;
	char thisname[IIO_MAX_NAME_LENGTH];
	int ret=0, i=0, j, add, tmp;

	dp = opendir(iio_dir);
	if (dp == NULL)
		return -ENODEV;

	while (ent = readdir(dp), ent != NULL) {
		if (strcmp(ent->d_name, ".") == 0 ||
				strcmp(ent->d_name, "..") == 0)
			continue;

		if (filter && strncmp(ent->d_name, filter, strlen(filter)) != 0)
			continue;

		filename = malloc(strlen(iio_dir) + strlen(ent->d_name) + 6);
		if (filename == NULL)
			return -ENOMEM;

		sprintf(filename, "%s%s/name", iio_dir, ent->d_name);
		nameFile = fopen(filename, "r");
		if (!nameFile) {
			free(filename);
			continue;
		}

		add = 1;

		free(filename);
		if (!add) {
			fclose(nameFile);
			continue;
		}

		memset(thisname, 0, IIO_MAX_NAME_LENGTH);
		tmp = fscanf(nameFile, "%s", thisname);
		fclose(nameFile);

		if (tmp == 1) {
			j = i;
			i += strlen(thisname) + 1;

			name_str = realloc(name_str, i);
			sprintf(&name_str[j], "%s", thisname);
			ret++;
		}
	}
	*names = name_str;
	closedir(dp);
	return ret;
}



static inline int _write_sysfs_int(const char *filename, const char *basedir, int val, int verify, int type, int val2)
{
	int ret = 0, tmp;
	FILE *sysfsfp;
	int test;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL)
		return -ENOMEM;
	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "w");
	if (sysfsfp == NULL) {
		fprintf(stderr, "failed to open %s\n", temp);
		ret = -errno;
		goto error_free;
	}
	if (type)
		fprintf(sysfsfp, "%d %d", val, val2);
	else
		fprintf(sysfsfp, "%d", val);

	fclose(sysfsfp);
	if (verify) {
		sysfsfp = fopen(temp, "r");
		if (sysfsfp == NULL) {
			fprintf(stderr, "failed to open %s\n", temp);
			ret = -errno;
			goto error_free;
		}
		tmp = fscanf(sysfsfp, "%d", &test);
		if (tmp != 1 || test != val) {
			fprintf(stderr, "Possible failure in int write %d to %s%s\n",
				val,
				basedir,
				filename);
			ret = -1;
		}
	}
error_free:
	free(temp);
	return ret;
}

static inline int write_sysfs_int(const char *filename, const char *basedir, int val)
{
	return _write_sysfs_int(filename, basedir, val, 0, 0, 0);
}

static inline int write_sysfs_int_and_verify(const char *filename, const char *basedir, int val)
{
	return _write_sysfs_int(filename, basedir, val, 1, 0, 0);
}

static inline int write_sysfs_int2(const char *filename, const char *basedir, int val, int val2)
{
	return _write_sysfs_int(filename, basedir, val, 0, 1 , val2);
}

static inline int _write_sysfs_string(const char *filename, const char *basedir, const char *val, int verify)
{
	int ret = 0, tmp;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL) {
		fprintf(stderr, "Memory allocation failed\n");
		return -ENOMEM;
	}
	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "w");
	if (sysfsfp == NULL) {
		fprintf(stderr, "Could not open %s\n", temp);
		ret = -errno;
		goto error_free;
	}
	fprintf(sysfsfp, "%s\n", val);
	fclose(sysfsfp);
	if (verify) {
		sysfsfp = fopen(temp, "r");
		if (sysfsfp == NULL) {
			fprintf(stderr, "could not open file to verify\n");
			ret = -errno;
			goto error_free;
		}
		tmp = fscanf(sysfsfp, "%s", temp);
		if (tmp != 1 || strcmp(temp, val) != 0) {
			fprintf(stderr, "Possible failure in string write of %s "
				"Should be %s "
				"written to %s%s\n",
				temp,
				val,
				basedir,
				filename);
			ret = -1;
		}
	}
error_free:
	free(temp);

	return ret;
}

/**
 * write_sysfs_string_and_verify() - string write, readback and verify
 * @filename: name of file to write to
 * @basedir: the sysfs directory in which the file is to be found
 * @val: the string to write
 **/
static inline int write_sysfs_string_and_verify(const char *filename, const char *basedir,
	const char *val)
{
	return _write_sysfs_string(filename, basedir, val, 1);
}

static inline int write_sysfs_string(const char *filename, const char *basedir, const char *val)
{
	return _write_sysfs_string(filename, basedir, val, 0);
}

static inline int read_sysfs_posint(const char *filename, const char *basedir)
{
	int ret, tmp;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL) {
		fprintf(stderr, "Memory allocation failed");
		return -ENOMEM;
	}
	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL) {
		ret = -errno;
		goto error_free;
	}
	tmp = fscanf(sysfsfp, "%i\n", &ret);

	fclose(sysfsfp);

	if (tmp != 1)
		ret = -ENODEV;
error_free:
	free(temp);
	return ret;
}

static inline int read_sysfs_float(const char *filename, const char *basedir, float *val)
{
	float ret = 0;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL) {
		fprintf(stderr, "Memory allocation failed");
		return -ENOMEM;
	}
	sprintf(temp, "%s/%s", basedir, filename);
	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL) {
		ret = -errno;
		goto error_free;
	}
	fscanf(sysfsfp, "%f\n", val);
	fclose(sysfsfp);
error_free:
	free(temp);
	return ret;
}

/*
 * Used to quiet warnings
 */
static inline void dummy_funct_quiet(char *ptr)
{
	sprintf(ptr, "%s", iio_debug_dir);
}

#define AVAILABLE_TOKEN "_available"
#define SCALE_TOKEN "_scale"

int set_dev_paths(const char *device_name);
const char * dev_name_dir(void);
const char * debug_name_dir(void);
#ifdef IIO_THREADS
void iio_thread_clear(GThread *thread);
#endif
int read_sysfs_string(const char *filename, const char *basedir, char **str);
int set_debugfs_paths(const char *device_name);
int read_reg(unsigned int address);
int write_reg(unsigned int address, unsigned int val);
int write_devattr(const char *attr, const char *str);
int read_devattr(const char *attr, char **str);
int read_devattr_bool(const char *attr, bool *value);
int read_devattr_double(const char *attr, double *value);
int write_devattr_double(const char *attr, double value);
int write_devattr_int(const char *attr, unsigned long long val);
int read_devattr_int(char *attr, int *val);
int read_devattr_slonglong(const char *attr, long long *value);
int write_devattr_slonglong(const char *attr, long long value);
bool iio_devattr_exists(const char *device, const char *attr);
int iio_buffer_open(bool read, int flags);
int find_scan_elements(char *dev, char **elements, unsigned access);
void scan_elements_sort(char **elements);
void scan_elements_insert(char **elements, char *token, char *end);

#endif /* __IIO_UTILS_H__ */
