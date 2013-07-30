/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __OSC_H__
#define __OSC_H__

extern GtkWidget *capture_graph;
extern const char *current_device;

void rx_update_labels(void);
void dialogs_init(GtkBuilder *builder);
void trigger_dialog_init(GtkBuilder *builder);
void trigger_update_current_device(void);
void application_quit (void);

#endif
