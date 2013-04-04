/**
 * Copyright (C) 2013 Analog Devices, Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
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
