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

#include <iio.h>

#include "fru.h"
#include "osc.h"
#include "osc_plugin.h"
#include "ini/ini.h"

#define MATCH_SECT(s) (strcmp(section, s) == 0)

int count_char_in_string(char c, const char *s)
{
	int i;

	for (i = 0; s[i];)
		if (s[i] == c)
			i++;
		else
			s++;

	return i;
}

static char * process_value(char* value)
{
	char *last, *next, *end;
	char *tmp, *tmp2, *tmp3, tmp4[128];
	int elem_type, ret;
	gchar **elems = NULL;
	double val, val1;

	if (!value)
		return NULL;

	last = (char *)value;
	while(last) {
		next = last + 1;
		last = strchr(next, '{') ;
	}

	if (!next) {
		return NULL;
	}

	end = strchr(next, '}');

	if (!end) {
		return NULL;
	}

	tmp = malloc(end-next);
	sprintf(tmp, "%.*s", (int)(end - next), next);

	if (strstr(tmp, " + ") || strstr(tmp, " - ")) {
		sscanf(tmp, "%lf", &val);
		last = strstr(tmp, " + ");
		if (!last) {
			last = strstr(tmp, " - ");
			sscanf(last + 3, "%lf", &val1);
			val1 *= -1;
		} else
			sscanf(last + 3, "%lf", &val1);

		val += val1;

		tmp2 = malloc(1024);

		sprintf(tmp4, "%f", val);
		/* strip trailing zeros */
		while (tmp4[strlen(tmp4) - 1] == '0')
			tmp4[strlen(tmp4) -1] = 0;
		if (tmp4[strlen(tmp4) - 1] == '.')
			tmp4[strlen(tmp4) -1] = 0;

		sprintf(tmp2, "%.*s%s%.*s",
			(int) (long) (next - 1 - value), value, tmp4,
			(int) (long) (strlen(value) - 2 - strlen(tmp)),
			next + 1 + strlen(tmp));
		return tmp2;
	}

	struct iio_context *ctx = get_context_from_osc();
	struct iio_device *dev = NULL;
	struct iio_channel *ch = NULL;
	const char *attr;

	elem_type = count_char_in_string('.', tmp);
	switch(elem_type) {
		case 0 :
			sscanf(tmp, "%lf", &val);
			break;
		case 1 :
			elems = g_strsplit(tmp, ".", 0);
			dev = iio_context_find_device(ctx, elems[0]);
			ret = iio_device_identify_filename(dev, elems[1], &ch, &attr);
			if (ret != 0)
				goto read_failed;
			if (ch)
				ret = iio_channel_attr_read_double(ch, attr, &val);
			else
				ret = iio_device_attr_read_double(dev, attr, &val);

			g_strfreev(elems);
			if (ret < 0)
				goto read_failed;
			break;
		read_failed:
			printf("fail to read %s:%s\n", elems[0], elems[1]);
			return NULL;
		default:
			val = -99;
			break;
	}

	tmp2 = malloc (1024);
	sprintf(tmp4, "%f", val);
	/* strip trailing zeros */
	while (tmp4[strlen(tmp4) - 1] == '0')
		tmp4[strlen(tmp4) -1] = 0;
	if (tmp4[strlen(tmp4) - 1] == '.')
		tmp4[strlen(tmp4) -1] = 0;

	sprintf(tmp2, "%.*s%s%.*s",
			(int) (long) (next - 1 - value), value, tmp4,
			(int) (long) (strlen(value) - 2 - strlen(tmp)),
			next + 1 + strlen(tmp));
	tmp3 = tmp2;
	if (strchr(tmp2, '{'))
		tmp3 = process_value(tmp2);

	return tmp3;
}

/* Handler should return nonzero on success, zero on error. */
static int libini_restore_handler(void *user, const char* section,
				 const char* name, const char* value)
{
	struct osc_plugin *plugin = NULL;
	int elem_type;
	long long val_i, min_i, max_i;
	double val_d, min_d, max_d;
	char val_str[1024];
	char *str = NULL;
	gchar **elems = NULL, **min_max = NULL;
	GSList *node;
	int ret = 1;
	FILE *fd;
	struct iio_context *ctx = get_context_from_osc();
	struct iio_device *dev = NULL;
	struct iio_channel *ch = NULL;
	const char *attr;

	if (value[0] == '{' && value[strlen(value) - 1] == '}') {
		value = process_value(strdup(value));
	}

	/* See if the section is from the main window */
	if (MATCH_SECT(MULTI_OSC))
		return main_profile_handler(section, name, value);

	/* See if the section is from a capture window */
	if (!strncmp(section, CAPTURE_CONF, strlen(CAPTURE_CONF)))
		return capture_profile_handler(section, name, value);

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

			if ( !(dev = iio_context_find_device(ctx, elems[0])) ) {
				if (!plugin->handle_item)
					break;
				plugin->handle_item(plugin, name, value);
				break;
			} else {
				if (strstr(elems[1], "hardwaregain") && strstr(value, " dB")) {
					str = strstr(value, " dB");
					*str = 0;
				}
				if (iio_device_identify_filename(dev, elems[1], &ch, &attr)) {
					ret = 0;
					break;
				}
				if (ch)
					ret = iio_channel_attr_write(ch, attr, value);
				else
					ret = iio_device_attr_write(dev, attr, value);
			}
			break;
		case 2:
			/* log something, according to:
			 * log.device.attribute = file
			 */
			elems = g_strsplit(name, ".", 0);

			if (!strcmp("log", elems[0]) &&
					(dev = iio_context_find_device(ctx, elems[1])) ) {
				if (iio_device_identify_filename(dev, elems[2], &ch, &attr)) {
					ret = 0;
					break;
				}
				if (ch)
					ret = iio_channel_attr_read(ch, attr, val_str, sizeof(val_str));
				else
					ret = iio_device_attr_read(dev, attr, val_str, sizeof(val_str));

				if (ret >= 0) {
					fd = fopen(value, "a");
					if (!fd) {
						ret = 1;
						break;
					}
					fprintf(fd, "%s, ", val_str);
					fclose (fd);
				} else
					ret = 0;
			} else if (!strcmp("debug", elems[0])) {
				if (!(dev = iio_context_find_device(ctx, elems[1])) || iio_device_get_debug_attrs_count(dev) == 0) {
					if (!plugin->handle_item)
						break;
					ret = !plugin->handle_item(plugin, name, value);
					break;
				} else
					ret = iio_device_debug_attr_write(dev, elems[2], value);
				break;
			} else {
				if (plugin->handle_item)
					ret = !plugin->handle_item(plugin, name, value);
				else
					ret = 0;
			}
			break;
		case 3:
			/* Test something, according to:
			 * test.device.attribute.type = min max
			 */
			ret = 0;
			elems = g_strsplit(name, ".", 0);

			if (!strchr(value, ' '))
				return 0;
			min_max = g_strsplit(value, " ", 0);

			if (!strcmp(elems[0], "test")) {
				if ( (dev = iio_context_find_device(ctx, elems[1])) ) {
					if (!strcmp(elems[3], "int")) {
						if (iio_device_identify_filename(dev, elems[2], &ch, &attr)) {
							ret = 0;
							break;
						}
						if (ch)
							iio_channel_attr_read_longlong(ch, attr, &val_i);
						else
							iio_device_attr_read_longlong(dev, attr, &val_i);
						min_i = atoi(min_max[0]);
						max_i = atoi(min_max[1]);
						if (val_i >= min_i && val_i <= max_i)
							ret = 1;
						else
							printf("value = %lld\n", val_i);
					} else if (!strcmp(elems[3], "double")) {
						if (iio_device_identify_filename(dev, elems[2], &ch, &attr)) {
							ret = 0;
							break;
						}
						if (ch)
							iio_channel_attr_read_double(ch, attr, &val_d);
						else
							iio_device_attr_read_double(dev, attr, &val_d);
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
			/* Unhandled */
			ret = 0;
			break;
	}
	if (elems != NULL)
		g_strfreev(elems);

	return ret;
}

const char * get_filename_from_path(const char *path)
{
	const char * filename;

	if (!path)
		return NULL;

	if (strstr(path, "/."))
		filename = (char *)(memchr(path, '.', strlen(path)) + 1);
	else if (strchr(path, '/'))
		filename = (char *)(strrchr(path, '/') + 1);
	else
		filename = path;

	return filename;
}

static const char * unroll(const char *in_filename)
{

	FILE *in, *out;
	char *out_filename, *replace;
	char buf[1024];
	char var[128], tmp[128], tmp2[128];
	double i, first, inc, last;
	fpos_t pos;
	bool j = true, k = false;

	out_filename = malloc(strlen(in_filename) + 10);
	sprintf(out_filename, TMP_INI_FILE, get_filename_from_path(in_filename));

	in = fopen(in_filename, "r");
	out = fopen (out_filename, "w");

	if (!in) {
		fprintf(stderr, "Failed to open %s : %s\n", in_filename,
			strerror(errno));
		return "";
	}
	if (!out) {
		fprintf(stderr, "Failed to open %s : %s\n", out_filename,
			strerror(errno));
		return "";
	}

	while(fgets(buf, 1024, in) != NULL) {
		if (strlen(buf)) {
			j = true;
			if (!strncmp(buf, "<SEQ>", 5)) {
				k = true;
				/* # seq [OPTION]... FIRST INCREMENT LAST */
				sscanf(buf, "<SEQ> %s %lf %lf %lf", var, &first, &inc, &last);
				sprintf(tmp, "<%s>", var);
				fgetpos(in, &pos);
				for(i = first; i <= last; i = i + inc) {
					fsetpos(in, &pos);
					while(fgets(buf, 1024, in) != NULL) {
						j = true;
						if (!strncmp(buf, "</SEQ>", 6)) {
							k = false;
							j = false;
							break;
						}
						if ((replace = strstr(buf, tmp)) != NULL) {
							j = false;
							sprintf(tmp2, "%lf", i);
							while (tmp2[strlen(tmp2) - 1] == '0')
								tmp2[strlen(tmp2) -1] = 0;
							if (tmp2[strlen(tmp2) - 1] == '.')
								tmp2[strlen(tmp2) -1] = 0;

							fprintf(out, "%.*s%s%.*s",
								(int) (long) (replace - buf), buf, tmp2,
								(int) (long) (strlen(buf) - (int)(replace - buf) - strlen(tmp)),
								replace + strlen(tmp));

						}
						if (j)
							fprintf(out, "%s", buf);
					}
				}
			}
		}
		if (j)
			fprintf(out, "%s", buf);
	}

	fclose(in);
	fclose(out);

	if (k) {
		printf("loop isn't closed in %s\n", in_filename);
		exit(1);
	}
	return out_filename;
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

	/* unroll loops */
	filename = unroll(filename);

	main_setup_before_ini_load();
	ret = ini_parse(filename, libini_restore_handler, NULL);
	main_setup_after_ini_load();

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
	char val_str[1024];
	gchar **elems;
	GSList *node;
	FILE* cfile;
	int ret;

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
		struct iio_context *ctx = get_context_from_osc();
		struct iio_device *dev = NULL;
		struct iio_channel *ch = NULL;
		const char *attr;

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

						if (ctx == NULL || !(dev = iio_context_find_device(ctx, elems[0])) ) {
							if (elems != NULL)
								g_strfreev(elems);

							str = plugin->handle_item(plugin, *attribs, NULL);
							if (str && str[0])
								fprintf(cfile, "%s = %s\n",
									*attribs, str);
							break;
						}
						str = NULL;

						if (iio_device_identify_filename(dev, elems[1], &ch, &attr))
							break;
						if (ch)
							ret = iio_channel_attr_read(ch, attr, val_str, sizeof(val_str));
						else
							ret = iio_device_attr_read(dev, attr, val_str, sizeof(val_str));

						if (ret >= 0)
							fprintf(cfile, "%s = %s\n",
									*attribs,
									val_str);

						if (elems != NULL)
							g_strfreev(elems);
						break;
					case 2:
						if (!plugin->handle_item)
							break;

						elems = g_strsplit(*attribs, ".", 0);

						if (!strcmp(elems[0], "debug")) {
							if (ctx == NULL || !(dev = iio_context_find_device(ctx, elems[1])) ) {
								if (elems != NULL)
								g_strfreev(elems);

								str = plugin->handle_item(plugin, *attribs, NULL);
								if (str && str[0])
									fprintf(cfile, "%s = %s\n",
										*attribs, str);
								break;
							}

							str = NULL;
							if (iio_device_debug_attr_read(dev, elems[2], val_str, sizeof(val_str)) >= 0)
									fprintf(cfile, "%s = %s\n",
										*attribs,
										val_str);
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
