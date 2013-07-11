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
#include <malloc.h>

#include "../iio_widget.h"
#include "../iio_utils.h"
#include "../osc_plugin.h"
#include "../config.h"

#define SINEWAVE        0
#define SQUAREWAVE      1
#define TRIANGLE        2
#define SAWTOOTH        3

static unsigned int buffer_size;
static uint8_t *soft_buffer_ch0;
static int current_sample = 0;
static int buffer_fd;
static gint fill_buffer_function = 0;

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
static GtkDataboxGraph *databox_graph;
static gfloat *X = NULL;
static gfloat *float_soft_buff;

static gdouble wave_ampl;
static gdouble wave_offset;

static int buffer_open(unsigned int length)
{
	int ret;
	int fd;

	set_dev_paths("ad7303");
	write_devattr("trigger/current_trigger", "hrtimer-1");
	write_devattr("scan_elements/out_voltage0_en", "1");

	fd = iio_buffer_open(false);
	if (fd < 0) {
		ret = -errno;
		fprintf(stderr, "Failed to open buffer: %d\n", ret);
		return ret;
	}

	/* Setup ring buffer parameters */
	ret = write_devattr_int("buffer/length", length);
	if (ret < 0) {
		fprintf(stderr, "Failed to set buffer length: %d\n", ret);
		goto err_close;
	}

	/* Enable the buffer */
	ret = write_devattr_int("buffer/enable", 1);
	if (ret < 0) {
		fprintf(stderr, "Failed to enable buffer: %d\n", ret);
		goto err_close;
	}

	return fd;

err_close:
	close(fd);
	return ret;
}

static void buffer_close(unsigned int fd)
{
	int ret;

	set_dev_paths("ad7303");

	/* Enable the buffer */
	ret = write_devattr_int("buffer/enable", 0);
	if (ret < 0) {
		fprintf(stderr, "Failed to disable buffer: %d\n", ret);
	}

	close(fd);
}

static int FillSoftBuffer(int waveType, uint8_t *softBuffer)
{
	int sampleNr = 0;
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
	int triggerFreq = 100;
	double waveFreq;
	int i;

	set_dev_paths("hrtimer-1");
	read_devattr_int("frequency", &triggerFreq);

	/* Set the maximum frequency that user cand select to 10% of the input generator frequency. */
	if (triggerFreq >= 10)
		gtk_range_set_range(GTK_RANGE(scale_freq), 0.01, triggerFreq / 10);

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

	if(gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(btn_sine)))
		waveType = SINEWAVE;
	else if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(btn_square)))
		waveType = SQUAREWAVE;
	else if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(btn_triangle)))
		waveType = TRIANGLE;
	else if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(btn_sawtooth)))
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
	gtk_databox_graph_add(GTK_DATABOX(databox), databox_graph);
	gtk_databox_set_total_limits(GTK_DATABOX(databox), 0, (i - 1), 3.5, 0);
}

static gboolean fillBuffer(void)
{
	int samplesToSend;
	int ret;

	samplesToSend = buffer_size - current_sample;
	ret = write(buffer_fd, soft_buffer_ch0 + current_sample, samplesToSend);
	if (ret < 0)
		printf("Error occured while writing to buffer: %d\n", errno);
	else {
		current_sample += ret;
		if (current_sample >= buffer_size)
			current_sample = 0;
	}
	usleep(10000);

	return TRUE;
}

static void startWaveGeneration(void)
{
	fill_buffer_function = g_idle_add((GSourceFunc)fillBuffer, NULL);
}

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}
void rx_update_labels(void);

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

static void sine_button_toggled(GtkToggleToolButton *btn,
							gpointer user_data)
{
	if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(btn))){
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_square),
									FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_triangle),
									FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_sawtooth),
									FALSE);
		generateWavePeriod();
	}
}
static void square_button_toggled(GtkToggleToolButton *togglebutton,
							gpointer user_data)
{
	if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(togglebutton))){
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_sine),
									FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_triangle),
									FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_sawtooth),
									FALSE);
		generateWavePeriod();
	}
}
static void triangle_button_toggled(GtkToggleToolButton *togglebutton,
							gpointer user_data)
{
	if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(togglebutton))){
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_sine),
									FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_square),
									FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_sawtooth),
									FALSE);
		generateWavePeriod();
	}
}
static void sawtooth_button_toggled(GtkToggleToolButton *togglebutton,
							gpointer user_data)
{
	if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(togglebutton))){
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_sine),
									FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_triangle),
									FALSE);
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(btn_square),
									FALSE);
		generateWavePeriod();
	}
}

static void wave_param_changed(GtkRange *range, gpointer user_data)
{
	generateWavePeriod();
}

static void save_button_clicked(GtkButton *btn, gpointer data)
{
	if (buffer_fd) {
		g_source_remove(fill_buffer_function);
		buffer_close(buffer_fd);
		buffer_fd = -1;
	}

	if (gtk_toggle_button_get_active((GtkToggleButton *)radio_single_val)){
		iio_save_widgets(tx_widgets, num_tx);
		iio_save_widgets(rx_widgets, num_rx);
		rx_update_labels();
	} else if (gtk_toggle_button_get_active((GtkToggleButton *)radio_waveform)){
		generateWavePeriod();
		buffer_fd = buffer_open(buffer_size * 10);
		startWaveGeneration();
	}
}

static int AD7303_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *AD7303_panel;
	GtkWidget *table;

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "AD7303.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "AD7303.glade", NULL);

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

	/* Bind the IIO device files to the GUI widgets */
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad7303", "out_voltage0_raw",
			builder, "spinbuttonValueCh0", NULL);
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad7303", "out_voltage1_raw",
			builder, "spinbuttonValueCh1", NULL);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"AD7303", "out_voltage0_powerdown",
			builder, "checkbuttonPwrDwn0", 0);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"AD7303", "out_voltage1_powerdown",
			builder, "checkbuttonPwrDwn1", 0);

	g_signal_connect(btn_sine, "toggled", G_CALLBACK(sine_button_toggled),
									NULL);
	g_signal_connect(btn_square, "toggled", G_CALLBACK(square_button_toggled),
									NULL);
	g_signal_connect(btn_triangle, "toggled",
				G_CALLBACK(triangle_button_toggled), NULL);
	g_signal_connect(btn_sawtooth, "toggled",
				G_CALLBACK(sawtooth_button_toggled), NULL);
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
	gtk_box_pack_start(GTK_BOX(preview_graph), table, TRUE, TRUE, 0);
	gtk_widget_modify_bg(databox, GTK_STATE_NORMAL, &color_background);
	gtk_widget_set_size_request(table, 450, 300);

	gtk_widget_show_all(AD7303_panel);

	tx_update_values();
	rx_update_values();

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), AD7303_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), AD7303_panel, "AD7303");

	return 0;
}

static bool AD7303_identify(void)
{
	return !set_dev_paths("ad7303");
}

const struct osc_plugin plugin = {
	.name = "AD7303",
	.identify = AD7303_identify,
	.init = AD7303_init,
};
