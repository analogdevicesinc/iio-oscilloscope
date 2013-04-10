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
	const char *name;
	bool (*identify)(void);
	int (*init)(GtkWidget *notebook);
};

void osc_plugin_register(const struct osc_plugin *plugin);

#endif
