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
#include "../iio_widget.h"

enum dds_tone_type {
	TX1_T1_I,
	TX1_T2_I,
	TX1_T1_Q,
	TX1_T2_Q,
	TX2_T1_I,
	TX2_T2_I,
	TX2_T1_Q,
	TX2_T2_Q,
	TX3_T1_I,
	TX3_T2_I,
	TX3_T1_Q,
	TX3_T2_Q,
	TX4_T1_I,
	TX4_T2_I,
	TX4_T1_Q,
	TX4_T2_Q
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

struct dds_tone {
	struct dds_channel *parent;

	unsigned number;
	struct iio_device *iio_dac;
	struct iio_channel *iio_ch;

	struct iio_widget iio_freq;
	struct iio_widget iio_scale;
	struct iio_widget iio_phase;

	double scale_state;

	gint dds_freq_hid;
	gint dds_scale_hid;
	gint dds_phase_hid;

	GtkWidget *freq;
	GtkWidget *scale;
	GtkWidget *phase;
	GtkWidget *frame;
};

struct dds_channel {
	struct dds_tx *parent;

	char type;
	struct dds_tone t1;
	struct dds_tone t2;

	GtkWidget *frame;
};

struct dds_tx {
	struct dds_dac *parent;

	unsigned index;
	struct dds_channel ch_i;
	struct dds_channel ch_q;
	struct dds_tone *dds_tones[4];

	GtkWidget *frame;
	GtkWidget *dds_mode_widget;
};

struct dds_dac {
	struct dac_data_manager *parent;

	unsigned index;
	const char *name;
	struct iio_device *iio_dac;
	unsigned tx_count;
	struct dds_tx tx1;
	struct dds_tx tx2;
	int dds_mode;
	unsigned tones_count;

	GtkWidget *frame;
};

struct dac_buffer {
	struct dac_data_manager *parent;

	char *dac_buf_filename;
	int scan_elements_count;
	struct iio_device *dac_with_scanelems;

	GtkWidget *frame;
	GtkWidget *buffer_fchooser_btn;
	GtkWidget *tx_channels_view;
	GtkTextBuffer *load_status_buf;
};

struct dac_data_manager {
	struct dds_dac dac1;
	struct dds_dac dac2;
	struct dac_buffer dac_buffer_module;

	struct iio_context *ctx;
	unsigned dacs_count;
	unsigned tones_count;
	unsigned alignment;
	GSList *dds_tones;
	bool scale_available_mode;
	double lowest_scale_point;
	bool dds_activated;
	bool dds_disabled;
	struct iio_buffer *dds_buffer;
	bool is_local;

	GtkWidget *container;
};

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
struct iio_widget *dac_data_manager_get_iio_widget(struct dac_data_manager *manager,
		enum dds_tone_type tone, enum dds_widget_type type);
GtkWidget *dac_data_manager_get_gui_container(struct dac_data_manager *manager);

#endif /* __DAC_DATA_MANAGER__ */
