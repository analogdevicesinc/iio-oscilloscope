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
	bool (*identify)(void);
	int (*init)(GtkWidget *notebook);
	char *(*handle_item) (struct osc_plugin *plugin, const char *attrib,
			      const char *value);
	int (*handle_external_request) (const char *request);
	const char **save_restore_attribs;
	void (*update_active_page)(gint active_page, gboolean is_detached);
	void (*get_preferred_size)(int *width, int *size);
	void (*destroy)(void);
};

void osc_plugin_register(const struct osc_plugin *plugin);
void * plugin_dlsym(const char *name, const char *symbol);
bool plugin_installed(const char *name);
extern GSList *plugin_list;

#define MATCH_ATTRIB(s) (strcmp(attrib, s) == 0)

#endif
