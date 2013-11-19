/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __OSC_H__
#define __OSC_H__
#define IIO_THREADS

extern GtkWidget *capture_graph;
extern gint capture_function;
extern const char *current_device;
extern bool str_endswith(const char *str, const char *needle);
extern bool is_input_device(const char *device);

G_LOCK_EXTERN (buffer_full);

void rx_update_labels(void);
void dialogs_init(GtkBuilder *builder);
void trigger_dialog_init(GtkBuilder *builder);
void trigger_update_current_device(void);
void application_quit (void);

void add_ch_setup_check_fct(char * device_name, void *fp);

void * plugin_get_device_by_reference(const char *device_name);
int plugin_data_capture_size(void *device);
int plugin_data_capture(void *device, void *buf);
int plugin_data_capture_num_active_channels(void *device);
int plugin_data_capture_bytes_per_sample(void *device);
void plugin_data_capture_demux(void *device, void *buf, gfloat **cooked, unsigned int num_samples,
	unsigned int num_channels);

void save_all_plugins(const char *filename, gpointer user_data);
void restore_all_plugins(const char *filename, gpointer user_data);

#endif

