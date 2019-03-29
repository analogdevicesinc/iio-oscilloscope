/*
 * XCOMM on-board calibration EEPROM methods.
 *
 * Copyright 2015 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libgen.h>

#include "eeprom.h"


/* Recursively search a given path (defaulting to /sys) for an XCOMM compatible
 * EEPROM file.
 *
 * If a matching EEPROM file is found the path is returned, otherwise returns
 * NULL. Note that the string for the returned path is obtained with malloc and
 * should be freed. It's up to the caller to free the allocated moemroy.
 */
const char *find_eeprom(const char *path)
{
	char *eeprom_path = NULL;
	char eeprom_names[512];
	FILE *fp = NULL;
	char cmd[512];

	if (path == NULL) {
		path = "/sys";
	}

	snprintf(cmd, sizeof(cmd), "find %s -name eeprom 2>/dev/null", path);

	fp = popen(cmd, "r");
	if (fp == NULL) {
		perror("popen");
		return NULL;
	}

	while (fgets(eeprom_names, sizeof(eeprom_names), fp) != NULL) {
		struct stat eeprom_file;
		char *__basename = NULL;

		if (eeprom_names[strlen(eeprom_names) - 1] == '\n')
			eeprom_names[strlen(eeprom_names) - 1] = '\0';

		__basename = strdup(eeprom_names);
		stat(eeprom_names, &eeprom_file);

		if (S_ISREG(eeprom_file.st_mode) &&
		    !strcmp(basename(__basename), "eeprom") &&
		    eeprom_file.st_size == FAB_SIZE_FRU_EEPROM) {

			eeprom_path = strdup(eeprom_names);
			free(__basename);
			break;
		}

		free(__basename);
	}

	pclose(fp);
	return eeprom_path;
}
