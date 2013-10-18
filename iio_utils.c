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

	if (!device_name) {
		ret = -EFAULT;
		goto error_ret;
	}

	ret = stat(iio_dir, &s);
	if (ret) {
		ret = -EFAULT;
		goto error_ret;
	}

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

	return 0;

error_ret:
	dev_dir_name[0] = '\0';
	buffer_access[0] = '\0';
	last_device_name[0] = '\0';
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
	return 0;

error_ret:
	debug_dir_name[0] ='\0';
	return ret;
}

int read_reg(unsigned int address)
{
	if (strlen(debug_dir_name) == 0)
		return 0;

	write_sysfs_int("direct_reg_access", debug_dir_name, address);
	return read_sysfs_posint("direct_reg_access", debug_dir_name);
}

int write_reg(unsigned int address, unsigned int val)
{
	char temp[40];

	if (strlen(debug_dir_name) == 0)
		return 0;

	sprintf(temp, "0x%x 0x%x\n", address, val);
	return write_sysfs_string("direct_reg_access", debug_dir_name, temp);
}

/* returns true if needle is inside haystack */
static inline bool element_substr(const char *haystack, const char * end, const char *needle)
{
	int i;
	char ssub[256], esub[256], need[256];

	strcpy(need, needle);
	if (end)
		strcat(need, end);

	if (!strcmp(haystack, need))
		return true;

	/* split the string, and look for it */
	for (i = 0; i < strlen(need); i++) {
		sprintf(ssub, "%.*s", i, need);
		sprintf(esub, "%.*s", (int)(strlen(need) - i), need + i);
		if ((strstr(haystack, ssub) == haystack) &&
		    ((strstr(haystack, esub) + strlen(esub)) == (haystack + strlen(haystack))))
			return true;
	}
	return false;
}

/*
* make sure the "_available" is right after the control
* IIO core doesn't make this happen in a normal sort
* since we can have indexes sometimes missing:
* out_altvoltage_1B_scale_available links to
* out_altvoltage1_1B_scale  and
* one _available, linking to multiple elements:
* in_voltage_test_mode_available links to both:
* in_voltage0_test_mode and in_voltage1_test_mode
*/
void scan_elements_insert(char **elements, char *token, char *end)
{
	char key[256], entire_key[256], *loop, *added = NULL;
	char *start, *next;
	int len, i, j, k, num = 0, num2;

        if (!*elements)
                return;

        start = *elements;
        len = strlen(start);

	/* strip everything apart, to make it easier to work on */
	next = strtok(start, " ");
	while (next && (start + len) > next) {
		num++;
		next = strtok(NULL, " ");
	}
	/* now walk through things, looking for the token */
        for (i = 0; i < num; i++) {
                next = strstr(start, token);
                if (next) {
			if(added) {
				/* did we all ready process this one? */
				if (strstr(added, start)) {
					start += strlen(start) + 1;
					continue;
				}
				added = realloc(added, strlen(added) + strlen (start) + 1);
				strcat(added, start);
			} else
				added = strdup(start);

			strcpy(entire_key, start);
			/*
			 * find where this belongs (if anywhere), and put it there
			 */
			sprintf(key, "%.*s", (int)(next - start), start);

			/*  so we need to:
			 *  - find out where it goes (can go multiple places)
			 *  - add it to all the places where it needs to go
			 *  - update the pointers, since we may have realloc'ed things
			 */
			next = *elements;
			loop = NULL;
			k = 0;
			/* scan through entire list */
			num2 = num;
			for (j = 0; j < num2; j++) {
				if (element_substr(next, end, key)) {
					if (!loop) {
					//	if (strcmp(next, entire_key)
						/* The first time we do this, we just move it, so
						 * we don't need to make the string bigger
						 */
						loop = next + strlen(next) + 1;
						memmove(loop + strlen(entire_key) + 1, loop, start - loop - 1);
						strcpy(loop, entire_key);
						/* we moved the token off the end, so check for one less */
						num2--;
					} else {
						k = next - *elements;
						*elements = realloc(*elements, len + strlen(entire_key) + 1);
						next = *elements + k;
						loop = next + strlen(next) + 1;
						memmove(loop + strlen(entire_key) + 1, loop, *elements + len - loop);
						strcpy(loop, entire_key);
						num++; num2++;
						len += strlen(entire_key) + 1;
					}
					start -= 1;
					next += strlen(next) + 1;
				}
				next += strlen(next) + 1;
			}
		}
                start += strlen(start) + 1;
        }

	start = *elements;

	/* put everything back together */
	for (i = 0; i < len; i++) {
		if (start[i] == 0)
			start[i] = ' ';
	}

	start[len] = 0;

        if (len != strlen(start))
                fprintf(stderr, "error in %s(%s)\n", __FILE__, __func__);
}

void scan_elements_sort(char **elements)
{
	int len, i, j, k, num = 0, swap;
	char *start, *next, *loop, temp[256];

	if (!*elements)
		return;

	next = start = *elements;

	len = strlen(start);

	/* strip everything apart, to make it easier to work on */
	next = strtok(start, " ");
	while (next) {
		num++;
		next = strtok(NULL, " ");
	}

	/*
	 * sort things using bubble sort
	 * there are plenty ways more efficent to do this - knock yourself out
	 */
	for (j = 0; j < num - 1; j++) {
		start = *elements;
		/* make sure dev, name, uevent are first (if they exist) */
		while (!strcmp(start, "name") || !strcmp(start, "dev") || !strcmp(start, "uevent")) {
			start += strlen(start) + 1;
		}

		loop = start;
		next = start + strlen(start) + 1;
		for (i = j; (i < num - 1) && (strlen(start)) && (strlen(next)); i++) {
			if (!strcmp(next, "name") || !strcmp(next, "dev") || !strcmp(next, "uevent")) {
				strcpy(temp, next);
				memmove(loop + strlen(temp) + 1, loop, next - loop - 1);
				strcpy(loop, temp);
				loop += strlen(temp) + 1;
			} else {
				swap = 0;
				/* Can't use strcmp, since it doesn't sort numerically */
				for (k = 0; k < strlen(start) && k < strlen(next); k++) {
					if (start[k] == next[k])
						continue;

					/* sort LABEL0_ LABEL10_ as zero and ten */
					if ((isdigit(start[k]) && isdigit(next[k])) &&
					    (isdigit(start[k+1]) || isdigit(next[k+1]))){
					    	if (atoi(&start[k]) >= atoi(&next[k])) {
							swap = 1;
						}
					} else if (start[k] >= next[k]) {
						swap = 1;
					}

					break;
				}
				if (k == strlen(next))
					swap = 1;

				if (swap) {
					strcpy(temp, start);
					strcpy(start, next);
					next = start + strlen(start) + 1;
					strcpy(next, temp);
				}
			}
			start += strlen(start) + 1;
			next = start + strlen(start) + 1;
		}
	}

	start = *elements;

	/* put everything back together */
	for (i = 0; i < len; i++) {
		if (start[i] == 0)
			start[i] = ' ';
	}
	start[len] = 0;

	if (len != strlen(start))
		fprintf(stderr, "error in %s(%s)\n", __FILE__, __func__);

}

int find_scan_elements(char *dev, char **relement, unsigned access)
{
	FILE *fp;
	char elements[128], buf[128];
	char *elem = NULL;
	int num = 0;

	/* flushes all open output streams */
	 fflush(NULL);

	sprintf(buf, "echo \"%s %s . \" | iio_cmdsrv",
		access ? "dbfsshow" : "show", dev);
	fp = popen(buf, "r");
	if(fp == NULL) {
		fprintf(stderr, "Can't execute iio_cmdsrv\n");
		return -ENODEV;
	}


	elem = malloc(128);
	memset (elem, 0, 128);

	while(fgets(elements, sizeof(elements), fp) != NULL){
		/* strip trailing new lines */
		if (elements[strlen(elements) - 1] == '\n')
			elements[strlen(elements) - 1] = '\0';

		/* first thing returned is the return code */
		if (num == 0) {
			num ++;
			continue;
		}

		elem = realloc(elem, strlen(elements) + strlen(elem) + 1);
		strncat(elem, elements, 128);
	}

	if (relement)
		*relement = elem;

	return 1;
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
	int ret;

	if (strlen(dev_dir_name) == 0)
		return -ENODEV;

	ret = write_sysfs_string(attr, dev_dir_name, str);

	if (ret < 0) {
		syslog(LOG_ERR, "write_devattr failed (%d)\n", __LINE__);
	}

	return ret;
}

int read_devattr(const char *attr, char **str)
{
	int ret;

	if (strlen(dev_dir_name) == 0)
		return -ENODEV;

	ret = read_sysfs_string(attr, dev_dir_name, str);
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
	int ret;

	if (strlen(dev_dir_name) == 0)
		return -ENODEV;

	ret = read_sysfs_posint(attr, dev_dir_name);
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

	if (strlen(dev_dir_name) == 0)
		return -ENODEV;

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

int iio_buffer_open(bool read, int flags)
{
	if (read)
		flags |= O_RDONLY;
	else
		flags |= O_WRONLY;

	return open(buffer_access, flags);
}
