/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __OSC_H__
#define __OSC_H__

extern GtkWidget *capture_graph;
extern gint capture_function_id;
extern pthread_mutex_t buffer_full;

void rx_update_labels(void);
void dialogs_init(GtkBuilder *builder);
void trigger_dialog_init(GtkBuilder *builder);
void trigger_update_current_device(void);
void application_quit (void);

int plugin_data_capture_size(void);
int plugin_data_capture(void *buf);
int plugin_data_capture_num_active_channels(void);
int plugin_data_capture_bytes_per_sample(void);
void plugin_data_capture_demux(void *buf, gfloat **cooked, unsigned int num_samples,
	unsigned int num_channels);


#endif
