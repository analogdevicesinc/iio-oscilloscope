/**
 * Copyright (C) 2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 */

#ifndef __OSC_PLUGIN_H__
#define __OSC_PLUGIN_H__

#include <gtk/gtk.h>

struct osc_plugin {
	void *handle;
	const char *name;
	GThread *thd;

	bool (*identify)(void);
	GtkWidget * (*init)(GtkWidget *notebook, const char *ini_fn);
	int (*handle_item) (int line, const char *attrib, const char *value);
	int (*handle_external_request) (const char *request);
	void (*update_active_page)(gint active_page, gboolean is_detached);
	void (*get_preferred_size)(int *width, int *size);
	void (*destroy)(const char *ini_fn);

	void (*save_profile)(const char *ini_fn);
	void (*load_profile)(const char *ini_fn);
};

void osc_plugin_register(const struct osc_plugin *plugin);
void * plugin_dlsym(const char *name, const char *symbol);
bool plugin_installed(const char *name);
extern GSList *plugin_list;

#define MATCH_ATTRIB(s) (strcmp(attrib, s) == 0)

#endif
