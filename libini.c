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

/*Handler should return nonzero on success, zero on error. */
static int libini_restore_handler(void *user, const char* section,
				 const char* name, const char* value)
{
	struct osc_plugin *plugin = NULL;
	int elem_type;
	int val_i, min_i, max_i;
	double val_d, min_d, max_d;
	char *str = NULL;
	gchar **elems = NULL, **min_max = NULL;
	GSList *node;
	int ret = 1;

	/* See if the section is from the main capture window */
	if (MATCH_SECT(CAPTURE_CONF)) {
		return capture_profile_handler(name, value);
	}

	/* needs to be a plugin */
	for (node = plugin_list; node; node = g_slist_next(node)) {
		plugin = node->data;
		if (plugin && plugin->save_restore_attribs &&
				MATCH_SECT(plugin->name)) {
			break;
		}
	}

	if (!plugin || !MATCH_SECT(plugin->name))
		return 0;

	elem_type = count_char_in_string('.', name);
	switch(elem_type) {
		case 0:
			if (!plugin->handle_item)
				break;
			ret = !plugin->handle_item(plugin, name, value);
			break;
		case 1:
			/* Set something, according to:
			 * device.attribute = value
			 */
			elems = g_strsplit(name, ".", 0);

			if (set_dev_paths(elems[0])) {
				if (!plugin->handle_item)
					break;
				ret = !plugin->handle_item(plugin, name, value);
				break;
			} else
				ret = !write_devattr(elems[1], value);
			break;
		case 3:
			/* Test something, according it:
			 * test.device.attribute.type = min max
			 */
			ret = 0;
			elems = g_strsplit(name, ".", 0);

			if (!strchr(value, ' '))
				return 0;
			min_max = g_strsplit(value, " ", 0);

			if (!strcmp(elems[0], "test")) {
				if (!set_dev_paths(elems[1])) {
					if (!strcmp(elems[3], "int")) {
						read_devattr_int(elems[2], &val_i);
						min_i = atoi(min_max[0]);
						max_i = atoi(min_max[1]);
						if (val_i >= min_i && val_i <= max_i)
							ret = 1;
						else
							printf("value = %i\n", val_i);
					} else if (!strcmp(elems[3], "double")) {
						read_devattr_double(elems[2], &val_d);
						min_d = atof(min_max[0]);
						max_d = atof(min_max[1]);
						if (val_d >= min_d && val_d <= max_d)
							ret = 1;
						else
							printf("value = %f\n", val_d);
					} else {
						ret = -1;
					}
				} else
					ret = -1;
			} else
				ret = -1;

			g_strfreev(min_max);
			if (ret == -1 && plugin->handle_item) {
				str = plugin->handle_item(plugin, name, value);
				if (str == NULL)
					ret = 1;
				else
					ret = 0;
			}
			break;
		default:
			break;
	}
	if (elems != NULL)
		g_strfreev(elems);

	return ret;
}

int restore_all_plugins(const char *filename, gpointer user_data)
{
	GtkWidget *msg;
	int ret = 0;

	msg = create_nonblocking_popup(GTK_MESSAGE_INFO,
			"Please wait",
			"Loading ini file:\n%s", filename);

	/* if the main loop isn't running, we need to poke things, so the message
	 * can be displayed
	 */
	if (msg && gtk_main_level() == 0) {
		while (gtk_events_pending() && ret <= 200) {
			gtk_main_iteration();
			ret++;
		}
	}

	ret = ini_parse(filename, libini_restore_handler, NULL);

	if (msg)
		gtk_widget_destroy(msg);

	return ret;
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
