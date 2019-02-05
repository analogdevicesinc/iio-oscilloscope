/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"

#define SINEWAVE        0
#define SQUAREWAVE      1
#define TRIANGLE        2
#define SAWTOOTH        3

static unsigned int buffer_size;
static uint8_t *soft_buffer_ch0;
static unsigned int current_sample = 0;
static gint fill_buffer_function = 0;
static struct iio_device *dev;
static bool dev_opened;
static struct iio_context *ctx, *thread_ctx;
static struct iio_buffer *dac_buff;

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static unsigned int num_tx, num_rx;

static GtkWidget *btn_sine;
static GtkWidget *btn_square;
static GtkWidget *btn_triangle;
static GtkWidget *btn_sawtooth;
static GtkWidget *scale_ampl;
static GtkWidget *scale_offset;
static GtkWidget *scale_freq;
static GtkWidget *radio_single_val;
static GtkWidget *radio_waveform;
static GtkWidget *databox;
static GtkWidget *preview_graph;

static GdkColor color_background = {
	.red = 0,
	.green = 0,
	.blue = 0,
};
static GdkColor color_prev_graph = {
	.red = 0,
	.green = 65535,
	.blue = 0,
};

static GdkColor color_prev_graph_dots = {
	.red = 65535,
	.green = 0,
	.blue = 0,
};

static GtkDataboxGraph *databox_graph;
static GtkDataboxGraph *databox_graph_dots;
static gfloat *X = NULL;
static gfloat *float_soft_buff;

static gdouble wave_ampl;
static gdouble wave_offset;

#define IIO_BUFFER_SIZE 400

static int buffer_open(unsigned int length)
{
	struct iio_device *trigger = iio_context_find_device(ctx, "hrtimer-1");
	struct iio_channel *ch0 = iio_device_find_channel(dev, "voltage0", true);

	iio_device_set_trigger(dev, trigger);
	iio_channel_enable(ch0);

	dac_buff = iio_device_create_buffer(dev, IIO_BUFFER_SIZE, false);

	return (dac_buff) ? 0 : 1;
}

static int buffer_close()
{
	iio_buffer_destroy(dac_buff);
	dac_buff = NULL;

	return 0;
}

static int FillSoftBuffer(int waveType, uint8_t *softBuffer)
{
	unsigned int sampleNr = 0;
	int rawVal;
	int intAmpl;
	int intOffset;

	intAmpl = wave_ampl  * (256 / 3.3);
	intOffset = wave_offset * (256 / 3.3);

	switch (waveType){
	case SINEWAVE:
		for (; sampleNr < buffer_size; sampleNr++){
			rawVal = (intAmpl/ 2) * sin(2 * sampleNr * G_PI / buffer_size) + intOffset;
			if (rawVal < 0)
				rawVal = 0;
			else if (rawVal > 255)
				rawVal = 255;
			softBuffer[sampleNr] = rawVal;
		}
		break;
	case SQUAREWAVE:
		for (; sampleNr < buffer_size / 2; sampleNr++){
			rawVal = intOffset - (intAmpl/ 2);
			if (rawVal < 0)
				rawVal = 0;
			else if (rawVal > 255)
				rawVal = 255;
			softBuffer[sampleNr] = rawVal;

		}
		for (; sampleNr < buffer_size; sampleNr++){
			rawVal = intOffset + (intAmpl/ 2);
			if (rawVal < 0)
				rawVal = 0;
			else if (rawVal > 255)
				rawVal = 255;
			softBuffer[sampleNr] = rawVal;
		}
		break;
	case TRIANGLE:
		for (; sampleNr < buffer_size / 2; sampleNr++){
			rawVal = sampleNr * intAmpl / (buffer_size / 2) + (intOffset - intAmpl / 2 );
			if (rawVal < 0)
				rawVal = 0;
			else if (rawVal > 255)
				rawVal = 255;
			softBuffer[sampleNr] = rawVal;
		}
		for (sampleNr = 0; sampleNr < (buffer_size +1) / 2; sampleNr++){
			rawVal = intAmpl - sampleNr * intAmpl / (buffer_size / 2) + (intOffset - intAmpl / 2 );
			if (rawVal < 0)
				rawVal = 0;
			else if (rawVal > 255)
				rawVal = 255;
			softBuffer[sampleNr + buffer_size / 2] = rawVal;
		}
		break;
	case SAWTOOTH:
		for (; sampleNr < buffer_size; sampleNr++){
			rawVal = sampleNr * intAmpl / buffer_size + (intOffset - intAmpl / 2 );
			if (rawVal < 0)
				rawVal = 0;
			else if (rawVal > 255)
				rawVal = 255;
			softBuffer[sampleNr] = rawVal;
		}
		break;
	default:
		break;
	}

	return 0;
}

static void generateWavePeriod(void)
{
	int waveType = 0;
	double waveFreq;
	unsigned int i;
	struct iio_device *trigger = iio_context_find_device(ctx, "hrtimer-1");
	unsigned long triggerFreq;
	long long triggerFreqLL = 0;

	iio_device_attr_read_longlong(trigger, "frequency", &triggerFreqLL);
	triggerFreq = triggerFreqLL;

	/* Set the maximum frequency that user can select to 10% of the input generator frequency. */
	if (triggerFreq >= 10)
		gtk_range_set_range(GTK_RANGE(scale_freq), 0.1, triggerFreq / 10);

	wave_ampl = gtk_range_get_value(GTK_RANGE(scale_ampl));
	wave_offset = gtk_range_get_value(GTK_RANGE(scale_offset));
	waveFreq = gtk_range_get_value(GTK_RANGE(scale_freq));
	buffer_size = (unsigned int)round(triggerFreq / waveFreq);
	if (buffer_size < 2)
		buffer_size = 2;
	else if (buffer_size > 10000)
		buffer_size = 10000;
	current_sample = 0;

	soft_buffer_ch0 = g_renew(uint8_t, soft_buffer_ch0, buffer_size);

	gtk_range_set_value(GTK_RANGE(scale_freq), (double)triggerFreq / buffer_size);
	gtk_widget_queue_draw(scale_freq);

	if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_sine)))
		waveType = SINEWAVE;
	else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_square)))
		waveType = SQUAREWAVE;
	else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_triangle)))
		waveType = TRIANGLE;
	else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_sawtooth)))
		waveType = SAWTOOTH;
	FillSoftBuffer(waveType, soft_buffer_ch0);

	/* Also generate a preview of the output signal. */
	float_soft_buff = g_renew(gfloat, float_soft_buff,  2 * buffer_size);
	X = g_renew(gfloat, X, 2 * buffer_size);
	for (i = 0; i < 2 * buffer_size; i++)
		X[i] = i;
	for (i = 0; i < 2 * buffer_size; i++)
		float_soft_buff[i] = soft_buffer_ch0[i % buffer_size] * 3.3 / 256;
	gtk_databox_graph_remove_all(GTK_DATABOX(databox));
	databox_graph = gtk_databox_lines_new((2 * buffer_size), X, float_soft_buff,
							&color_prev_graph, 2);
	databox_graph_dots = gtk_databox_points_new((2 * buffer_size), X, float_soft_buff,
							&color_prev_graph_dots, 5);
	gtk_databox_graph_add(GTK_DATABOX(databox), databox_graph_dots);
	gtk_databox_graph_add(GTK_DATABOX(databox), databox_graph);
	gtk_databox_set_total_limits(GTK_DATABOX(databox), -0.2, (i - 1), 3.5, -0.2);
}

static gboolean fillBuffer(void)
{
	unsigned int i;
	uint8_t *buf;
	int ret;

	while (true) {
		buf = iio_buffer_start(dac_buff);
		for (i = 0; i < IIO_BUFFER_SIZE; i++) {
			buf[i] = soft_buffer_ch0[current_sample];
			current_sample++;
			if (current_sample >= buffer_size)
				current_sample = 0;
		}

		ret = iio_buffer_push(dac_buff);
		if (ret < 0)
			printf("Error occured while writing to buffer: %d\n", ret);
	}

	return TRUE;
}

static void startWaveGeneration(void)
{
	g_thread_new("fill_buffer_thread", (void *) &fillBuffer, NULL);
}

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_device_sampling_freq("ad7303",
		USE_INTERN_SAMPLING_FREQ);
}

static void wave_param_changed(GtkRange *range, gpointer user_data)
{
	generateWavePeriod();
}

static void save_button_clicked(GtkButton *btn, gpointer data)
{
	if (dev_opened) {
		g_source_remove(fill_buffer_function);
		buffer_close();
		dev_opened = false;
	}

	if (gtk_toggle_button_get_active((GtkToggleButton *)radio_single_val)){
		iio_save_widgets(tx_widgets, num_tx);
		iio_save_widgets(rx_widgets, num_rx);
		rx_update_device_sampling_freq("ad7303",
			USE_INTERN_SAMPLING_FREQ);
	} else if (gtk_toggle_button_get_active((GtkToggleButton *)radio_waveform)){
		generateWavePeriod();
		dev_opened = !buffer_open(buffer_size);
		startWaveGeneration();
	}
}

static GtkWidget * AD7303_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	struct iio_channel *ch0, *ch1;
	GtkBuilder *builder;
	GtkWidget *AD7303_panel;
	GtkWidget *table;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	thread_ctx = osc_create_context();
	dev = iio_context_find_device(thread_ctx, "ad7303");

	builder = gtk_builder_new();

	if (osc_load_glade_file(builder, "AD7303") < 0)
		return NULL;

	AD7303_panel = GTK_WIDGET(gtk_builder_get_object(builder, "tablePanelAD7303"));
	btn_sine = GTK_WIDGET(gtk_builder_get_object(builder, "togBtnSine"));
	btn_square = GTK_WIDGET(gtk_builder_get_object(builder, "togBtnSquare"));
	btn_triangle = GTK_WIDGET(gtk_builder_get_object(builder, "togBtnTriangle"));
	btn_sawtooth = GTK_WIDGET(gtk_builder_get_object(builder, "togBtnSawth"));
	scale_ampl = GTK_WIDGET(gtk_builder_get_object(builder, "vscaleAmpl"));
	scale_offset = GTK_WIDGET(gtk_builder_get_object(builder, "vscaleOff"));
	scale_freq = GTK_WIDGET(gtk_builder_get_object(builder, "vscaleFreq"));
	radio_single_val = GTK_WIDGET(gtk_builder_get_object(builder, "radioSingleVal"));
	radio_waveform = GTK_WIDGET(gtk_builder_get_object(builder, "radioWaveform"));
	preview_graph = GTK_WIDGET(gtk_builder_get_object(builder, "vboxDatabox"));

	ch0 = iio_device_find_channel(dev, "voltage0", true);
	ch1 = iio_device_find_channel(dev, "voltage1", true);

	/* Bind the IIO device files to the GUI widgets */
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, ch0, "raw",
			builder, "spinbuttonValueCh0", NULL);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			dev, ch1, "raw",
			builder, "spinbuttonValueCh1", NULL);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, ch0, "powerdown",
			builder, "checkbuttonPwrDwn0", 0);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			dev, ch1, "powerdown",
			builder, "checkbuttonPwrDwn1", 0);

	g_signal_connect(btn_sine, "toggled", G_CALLBACK(wave_param_changed),
		NULL);
	g_signal_connect(btn_square, "toggled",  G_CALLBACK(wave_param_changed),
		NULL);
	g_signal_connect(btn_triangle, "toggled", G_CALLBACK(wave_param_changed),
		NULL);
	g_signal_connect(btn_sawtooth, "toggled", G_CALLBACK(wave_param_changed),
		NULL);

	g_signal_connect(scale_ampl, "value-changed",
				G_CALLBACK(wave_param_changed), NULL);
	g_signal_connect(scale_offset, "value-changed",
				G_CALLBACK(wave_param_changed), NULL);
	g_signal_connect(scale_freq, "value-changed",
				G_CALLBACK(wave_param_changed), NULL);
	g_builder_connect_signal(builder, "buttonSave", "clicked",
					G_CALLBACK(save_button_clicked), NULL);
	/* Create a GtkDatabox widget */
	gtk_databox_create_box_with_scrollbars_and_rulers(&databox, &table,
						TRUE, TRUE, TRUE, TRUE);
	gtk_container_add(GTK_CONTAINER(preview_graph), table);
	gtk_widget_modify_bg(databox, GTK_STATE_NORMAL, &color_background);
	gtk_widget_set_size_request(table, 450, 300);

	gtk_widget_show_all(AD7303_panel);

	tx_update_values();
	rx_update_values();

	return AD7303_panel;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	if (dac_buff) {
		iio_buffer_destroy(dac_buff);
		dac_buff = NULL;
	}

	osc_destroy_context(ctx);
	osc_destroy_context(thread_ctx);
}

static bool AD7303_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();
	return !!iio_context_find_device(osc_ctx, "ad7303");
}

struct osc_plugin plugin = {
	.name = "AD7303",
	.identify = AD7303_identify,
	.init = AD7303_init,
	.destroy = context_destroy,
};
