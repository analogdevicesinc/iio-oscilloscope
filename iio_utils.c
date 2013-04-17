/**
 * Copyright 2012-2013(c) Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "iio_utils.h"

#define MAX_STR_LEN		512
char dev_dir_name[MAX_STR_LEN];
static char buf_dir_name[MAX_STR_LEN];
static char buffer_access[MAX_STR_LEN];
static char last_device_name[MAX_STR_LEN];
static char last_debug_name[MAX_STR_LEN];

static char debug_dir_name[MAX_STR_LEN];

int set_dev_paths(const char *device_name)
{
	int dev_num, ret;
	struct stat s;

	if (!device_name)
		return -EFAULT;

	ret = stat(iio_dir, &s);
	if (ret)
		return -EFAULT;

	if (strncmp(device_name, last_device_name, MAX_STR_LEN) != 0) {
	/* Find the device requested */
		dev_num = find_type_by_name(device_name, "iio:device");
		if (dev_num >= 0) {
			ret = snprintf(buf_dir_name, MAX_STR_LEN,"%siio:device%d/buffer",
					   iio_dir, dev_num);
			if (ret >= MAX_STR_LEN) {
				syslog(LOG_ERR, "set_dev_paths failed (%d)\n", __LINE__);
				ret = -EFAULT;
				goto error_ret;
			}
			snprintf(dev_dir_name, MAX_STR_LEN, "%siio:device%d",
				 iio_dir, dev_num);
			snprintf(buffer_access, MAX_STR_LEN, "/dev/iio:device%d",
			 dev_num);
			strcpy(last_device_name, device_name);
		} else {
			dev_num = find_type_by_name(device_name, "trigger");
			if (dev_num >= 0) {
				snprintf(dev_dir_name, MAX_STR_LEN, "%strigger%d",
					 iio_dir, dev_num);
				strcpy(last_device_name, device_name);
			} else {
				syslog(LOG_ERR, "set_dev_paths failed to find the %s\n",
			       device_name);
				ret = -ENODEV;
				goto error_ret;
			}
		}
	}

	ret = 0;
error_ret:
	return ret;
}

int set_debugfs_paths(const char *device_name)
{
	int dev_num, ret;
	FILE *debugfsfp;

	if (strncmp(device_name, last_debug_name, MAX_STR_LEN) != 0) {
		/* Find the device requested */
		dev_num = find_type_by_name(device_name, "iio:device");
		if (dev_num < 0) {
			syslog(LOG_ERR, "%s failed to find the %s\n",
				__func__, device_name);
			ret = -ENODEV;
			goto error_ret;
		}
		ret = snprintf(debug_dir_name, MAX_STR_LEN,"%siio:device%d/",
		iio_debug_dir, dev_num);
		if (ret >= MAX_STR_LEN) {
			syslog(LOG_ERR, "%s failed (%d)\n", __func__, __LINE__);
			ret = -EFAULT;
			goto error_ret;
		}
		debugfsfp = fopen(debug_dir_name, "r");
		if (!debugfsfp) {
			syslog(LOG_ERR, "%s can't open %s\n", __func__, debug_dir_name);
			ret = -ENODEV;
			goto error_ret;
		}
	}
	ret = 0;
error_ret:
	return ret;
}

int read_reg(unsigned int address)
{
	write_sysfs_int("direct_reg_access", debug_dir_name, address);
	return read_sysfs_posint("direct_reg_access", debug_dir_name);
}

int write_reg(unsigned int address, unsigned int val)
{
	char temp[40];

	sprintf(temp, "0x%x 0x%x\n", address, val);
	return write_sysfs_string("direct_reg_access", debug_dir_name, temp);
}

int read_sysfs_string(const char *filename, const char *basedir, char **str)
{
	int ret = 0;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);

	if (temp == NULL) {
		syslog(LOG_ERR, "Memory allocation failed\n");
		return -ENOMEM;
	}
	sprintf(temp, "%s/%s", basedir, filename);

	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL) {
		syslog(LOG_ERR, "could not open file to verify\n");
		ret = -errno;
		goto error_free;
	}
	*str = calloc(1024, 1);
	ret = fread(*str, 1024, 1, sysfsfp);

	if (ret < 0) {
		if (NULL != *str)
			free(*str);

	}
	ret = strlen(*str);

	if ((*str)[ret - 1] == '\n')
		(*str)[ret - 1] = '\0';

	fclose(sysfsfp);

error_free:
	free(temp);
	return ret;
}

int write_devattr(const char *attr, const char *str)
{
	int ret = write_sysfs_string(attr, dev_dir_name, str);

	if (ret < 0) {
		syslog(LOG_ERR, "write_devattr failed (%d)\n", __LINE__);
	}

	return ret;
}

int read_devattr(const char *attr, char **str)
{
	int ret = read_sysfs_string(attr, dev_dir_name, str);
	if (ret < 0) {
		syslog(LOG_ERR, "read_devattr failed (%d)\n", __LINE__);
	}

	return ret;
}

int read_devattr_bool(const char *attr, bool *value)
{
	char *buf;
	int ret;

	ret = read_devattr(attr, &buf);
	if (ret < 0)
		return ret;

	if (buf[0] == '1' && buf[1] == '\0')
		*value = true;
	else
		*value = false;
	free(buf);

	return 0;
}

int read_devattr_double(const char *attr, double *value)
{
	char *buf;
	int ret;

	ret = read_devattr(attr, &buf);
	if (ret < 0)
		return ret;

	sscanf(buf, "%lf", value);
	free(buf);

	return 0;
}

int write_devattr_double(const char *attr, double value)
{
	char buf[100];

	snprintf(buf, 100, "%f", value);
	return write_devattr(attr, buf);
}

int read_devattr_slonglong(const char *attr, long long *value)
{
	char *buf;
	int ret;

	ret = read_devattr(attr, &buf);
	if (ret < 0)
		return ret;

	sscanf(buf, "%lli", value);
	free(buf);

	return 0;
}

int write_devattr_slonglong(const char *attr, long long value)
{
	char buf[100];

	snprintf(buf, 100, "%lld", value);
	return write_devattr(attr, buf);
}

int write_devattr_int(const char *attr, unsigned long long value)
{
	char buf[100];

	snprintf(buf, 100, "%llu", value);
	return write_devattr(attr, buf);
}

int read_devattr_int(char *attr, int *val)
{
	int ret = read_sysfs_posint(attr, dev_dir_name);
	if (ret < 0) {
		syslog(LOG_ERR, "read_devattr failed (%d)\n", __LINE__);
	}

	*val = ret;

	return ret;
}

bool iio_devattr_exists(const char *device, const char *attr)
{
	char *temp;
	struct stat s;

	set_dev_paths(device);

	temp = malloc(strlen(dev_dir_name) + strlen(attr) + 2);
	if (temp == NULL) {
		fprintf(stderr, "Memory allocation failed\n");
		return -ENOMEM;
	}
	sprintf(temp, "%s/%s", dev_dir_name, attr);

	stat(temp, &s);

	free(temp);

	return S_ISREG(s.st_mode);
}

int iio_buffer_open(void)
{
	return open(buffer_access, O_RDONLY | O_NONBLOCK);
}
