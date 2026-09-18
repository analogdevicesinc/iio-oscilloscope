/*
 * XCOMM on-board calibration EEPROM methods.
 *
 * Copyright 2015 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libgen.h>

#include "eeprom.h"

extern char **environ;


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

/* Write the tuning value to the FRU EEPROM by running fru-dump. The tool is
 * executed directly (no shell involved), so the EEPROM path can't be
 * interpreted as part of a command line.
 *
 * Returns 0 on success, -1 otherwise.
 */
int fru_dump_write_tuning(const char *eeprom_path, const char *tuning)
{
	char *const argv[] = {
		"fru-dump", "-i", (char *)eeprom_path, "-o", (char *)eeprom_path,
		"-t", (char *)tuning, NULL
	};
	posix_spawn_file_actions_t actions;
	int status, ret;
	pid_t pid;

	ret = posix_spawn_file_actions_init(&actions);
	if (ret)
		return -1;

	/* We only care about the exit status, so discard the output */
	ret = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO,
					       "/dev/null", O_WRONLY, 0);
	if (!ret)
		ret = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO,
						       STDERR_FILENO);
	if (!ret)
		ret = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);

	posix_spawn_file_actions_destroy(&actions);
	if (ret) {
		fprintf(stderr, "Failed to run fru-dump: %s\n", strerror(ret));
		return -1;
	}

	do {
		ret = waitpid(pid, &status, 0);
	} while (ret < 0 && errno == EINTR);

	if (ret < 0)
		return -1;

	return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}
