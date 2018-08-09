/*
 * XCOMM on-board calibration EEPROM methods.
 *
 * Copyright 2015 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <ftw.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include "eeprom.h"

static char *eeprom_path = NULL;

/* Test if a given file is a valid XCOMM EEPROM file. */
static int is_eeprom(const char *fpath, const struct stat *sb,
		int typeflag, struct FTW *ftwbuf)
{
	int ret = 0;
	char *fpath_s = strdup(fpath);
	if (typeflag == FTW_F && !strcmp(basename(fpath_s), "eeprom") \
			&& sb->st_size == FAB_SIZE_FRU_EEPROM) {
		free(eeprom_path);
		eeprom_path = strdup(fpath);
		ret = 1;
	}
	free(fpath_s);
	return ret;
}

/* Recursively search a given path (defaulting to /sys) for an XCOMM compatible
 * EEPROM file.
 *
 * If a matching EEPROM file is found the path is returned, otherwise returns
 * NULL. Note that the string for the returned path is obtained with malloc and
 * should be freed.
 */
const char *find_eeprom(const char *path)
{
	if (path == NULL)
		path = "/sys";

	if (nftw(path, is_eeprom, 64, FTW_DEPTH | FTW_PHYS) == 1)
		return eeprom_path;
	return NULL;
}
