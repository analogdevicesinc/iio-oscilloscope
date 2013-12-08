/**
 * Copyright (C) 2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "fru.h"
#include "osc.h"
#include "iio_utils.h"
#include "osc_plugin.h"
#include "ini/ini.h"

#define MATCH_SECT(s) (strcmp(section, s) == 0)

static int count_char_in_string(char c, const char *s)
{
	int i;

	for (i = 0; s[i];)
		if (s[i] == c)
			i++;
		else
			s++;

	return i;
}

static int libini_restore_handler(void *user, const char* section,
				 const char* name, const char* value)
{
	struct osc_plugin *plugin;
	int elem_type;
	gchar **elems = NULL;
	GSList *node;

	/* See if the section is from the main capture window */
	if (MATCH_SECT(CAPTURE_CONF)) {
		capture_profile_handler(name, value);
		return 0;
	}

	/* needs to be a plugin */
	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin && plugin->save_restore_attribs &&
				MATCH_SECT(plugin->name)) {
			break;
		}
	}

	if (!MATCH_SECT(plugin->name))
		return 0;

	elem_type = count_char_in_string('.', name);
	switch(elem_type) {
		case 0:
			if (!plugin->handle_item)
				break;
			plugin->handle_item(plugin, name, value);
			break;
		case 1:
			elems = g_strsplit(name, ".", 0);

			if (set_dev_paths(elems[0])) {
				if (!plugin->handle_item)
					break;
				plugin->handle_item(plugin, name, value);
				break;
			}

			write_devattr(elems[1], value);
			break;
		default:
			break;
	}
	if (elems != NULL)
		g_strfreev(elems);

	return 0;
}

void restore_all_plugins(const char *filename, gpointer user_data)
{
	ini_parse(filename, libini_restore_handler, NULL);
}

void save_all_plugins(const char *filename, gpointer user_data)
{
	struct osc_plugin *plugin;
	const char **attribs;
	int elem_type;
	char *str;
	gchar **elems;
	GSList *node;
	FILE* cfile;

	capture_profile_save(filename);

	cfile = fopen(filename, "a");
	if (cfile == NULL) {
		fprintf(stderr, "Failed to open %s : %s\n",filename,
			strerror(errno));
		return;
	}

	for (node = plugin_list; node; node = g_slist_next(node))
	{
		plugin = node->data;

		if (plugin && plugin->save_restore_attribs) {
			attribs = plugin->save_restore_attribs;

			fprintf(cfile, "\n[%s]\n", plugin->name);

			do {
				elem_type = count_char_in_string('.', *attribs);

				switch(elem_type) {
					case 0:
						if (!plugin->handle_item)
							break;

						str = plugin->handle_item(plugin, *attribs, NULL);
						if (str && str[0])
							fprintf(cfile, "%s = %s\n",
								*attribs,
								str);
						break;
					case 1:
						elems = g_strsplit(*attribs, ".", 0);

						if (set_dev_paths(elems[0])) {
							if (elems != NULL)
								g_strfreev(elems);

							str = plugin->handle_item(plugin, *attribs, NULL);
							if (str && str[0])
								fprintf(cfile, "%s = %s\n",
									*attribs, str);
							break;
						}
						str = NULL;
						if (read_devattr(elems[1], &str) >= 0) {
							if (str) {
								fprintf(cfile, "%s = %s\n",
									*attribs,
									str);
								free(str);
							}
						}

						if (elems != NULL)
							g_strfreev(elems);
						break;
					default:
						break;
				}
			} while (*(++attribs));
		}
	}

	fclose(cfile);
}
