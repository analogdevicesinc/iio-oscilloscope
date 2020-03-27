/**
 * Copyright (C) 2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 */

#ifndef __OSC_PLUGIN_H__
#define __OSC_PLUGIN_H__

#include "osc_preferences.h"

#include <gtk/gtk.h>

/* Information needed to create a new plugin */
struct osc_plugin_context {
	char *plugin_name;
	GList *required_devices;
};

/* The interface of an iio-oscilloscope plugin */
struct osc_plugin {
	void *handle;
	const char *name;
	GThread *thd;
	bool dynamically_created;

	bool (*identify)(const struct osc_plugin *plugin);
	GtkWidget * (*init)(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn);
	int (*handle_item) (struct osc_plugin *plugin, int line, const char *attrib, const char *value);
	int (*handle_external_request) (struct osc_plugin *plugin, const char *request);
	void (*update_active_page)(struct osc_plugin *plugin, gint active_page, gboolean is_detached);
	void (*get_preferred_size)(const struct osc_plugin *plugin, int *width, int *size);
	OscPreferences* (*get_preferences_for_osc)(const struct osc_plugin *plugin);
	void (*destroy)(struct osc_plugin *plugin, const char *ini_fn);

	void (*save_profile)(const struct osc_plugin *plugin, const char *ini_fn);
	void (*load_profile)(struct osc_plugin *plugin, const char *ini_fn);
	GSList* (*get_dac_dev_names)(const struct osc_plugin *plugin);
	struct plugin_private *priv;
};

void osc_plugin_context_free_resources(struct osc_plugin_context *ctx);
void osc_plugin_register(const struct osc_plugin *plugin);
void * plugin_dlsym(const char *name, const char *symbol);
bool plugin_installed(const char *name);
extern GSList *plugin_list;

#define MATCH_ATTRIB(s) (strcmp(attrib, s) == 0)

#endif
