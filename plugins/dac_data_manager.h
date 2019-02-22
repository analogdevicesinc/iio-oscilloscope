/**
 * Copyright (C) 2012-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 */

#ifndef __DAC_DATA_MANAGER__
#define __DAC_DATA_MANAGER__

#include <gtk/gtk.h>
#include <stdarg.h>
#include <iio.h>

enum dds_tone_type {
	TONE_I,
	TONE_Q
};

enum dds_tone_index {
	TONE_1,
	TONE_2
};

enum dds_widget_type {
	WIDGET_FREQUENCY,
	WIDGET_SCALE,
	WIDGET_PHASE
};

#define DDS_DISABLED  0
#define DDS_ONE_TONE  1
#define DDS_TWO_TONE  2
#define DDS_INDEPDENT 3
#define DDS_BUFFER    4

struct dac_data_manager;

struct dac_data_manager *dac_data_manager_new(struct iio_device *dac1,
		struct iio_device *dac2, struct iio_context *ctx);
void dac_data_manager_free(struct dac_data_manager *manager);
void dac_data_manager_freq_widgets_range_update(struct dac_data_manager *manager,
		double tx_sample_rate);
void dac_data_manager_update_iio_widgets(struct dac_data_manager *manager);
int  dac_data_manager_set_dds_mode(struct dac_data_manager *manager,
		const char *dac_name, unsigned tx_index, int mode);
int  dac_data_manager_get_dds_mode(struct dac_data_manager *manager,
		const char *dac_name, unsigned tx_index);
void dac_data_manager_set_buffer_chooser_current_folder(struct dac_data_manager *manager,
		const char *path);
void dac_data_manager_set_buffer_chooser_filename(struct dac_data_manager *manager,
		const char *filename);
char *dac_data_manager_get_buffer_chooser_filename(struct dac_data_manager *manager);
void dac_data_manager_set_tx_channel_state(struct dac_data_manager *manager,
		unsigned ch_index, bool state);
bool dac_data_manager_get_tx_channel_state(struct dac_data_manager *manager,
		unsigned ch_index);
GtkWidget *dac_data_manager_get_widget(struct dac_data_manager *manager,
		enum dds_tone_type tone, enum dds_widget_type type);
GtkWidget *dac_data_manager_get_gui_container(struct dac_data_manager *manager);
void dac_data_manager_set_buffer_size_alignment(struct dac_data_manager *manager,
		unsigned align);
unsigned dac_data_manager_dds_tone(unsigned tx_index,
	enum dds_tone_index tone_index, enum dds_tone_type tone_type);

/* Helpers */
int device_scan_elements_count(struct iio_device *dev);

#endif /* __DAC_DATA_MANAGER__ */
