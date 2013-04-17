/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
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

#define BUFFER_SIZE    360

#define SINEWAVE       0
#define SQUAREWAVE     1
#define TRIANGLE       2
#define SAWTOOTH       3

static short softBufferCh0[BUFFER_SIZE];
static int currentSample = 0;


static gint fill_buffer_function = 0;

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static unsigned int num_tx, num_rx;

static GtkWidget* radioSine;
static GtkWidget* radioSquare;
static GtkWidget* radioTriangle;
static GtkWidget* radioSawtooth;
static GtkWidget* radioSine1;
static GtkWidget* radioSquare1;
static GtkWidget* radioTriangle1;
static GtkWidget* radioSawtooth1;
static GtkWidget* spinAmpl;
static GtkWidget* spinOffset;
static GtkWidget* radioSingleVal;
static GtkWidget* radioWaveform;

static gdouble waveAmpl;
static gdouble waveOffset;

static int buffer_open(unsigned int length)
{
	int ret;
	int fd;

	set_dev_paths("ad7303");

	fd = iio_buffer_open();
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

static int FillSoftBuffer(int waveType, short* softBuffer)
{
    int sampleNr = 0;
    int rawVal;
    int intAmpl;
    int intOffset;
    
    intAmpl = waveAmpl  * (256 / 3.3);
    intOffset = waveOffset * (256 / 3.3);
      
    switch(waveType){
    case SINEWAVE:
        for(;sampleNr < BUFFER_SIZE; sampleNr++)
        {
            
            rawVal = (intAmpl/ 2) * sin(sampleNr * G_PI / 180) + intOffset;
            if(rawVal < 0)
                rawVal = 0;
            else if(rawVal > 255)
                rawVal = 255;
            softBuffer[sampleNr] = rawVal;
        }
        break;
    case SQUAREWAVE:
        for(;sampleNr < BUFFER_SIZE / 2; sampleNr++)
        {
            rawVal = intOffset - (intAmpl/ 2);
            if(rawVal < 0)
                rawVal = 0;
            else if(rawVal > 255)
                rawVal = 255;
            softBuffer[sampleNr] = rawVal;
            
        }
        for(;sampleNr < BUFFER_SIZE; sampleNr++)
        {
            rawVal = intOffset + (intAmpl/ 2);
            if(rawVal < 0)
                rawVal = 0;
            else if(rawVal > 255)
                rawVal = 255;
            softBuffer[sampleNr] = rawVal;
        }
        break;
    case TRIANGLE:
        for(;sampleNr < BUFFER_SIZE / 2; sampleNr++)
        {
            rawVal = sampleNr * intAmpl / (BUFFER_SIZE / 2) + (intOffset - intAmpl / 2 );
            if(rawVal < 0)
                rawVal = 0;
            else if(rawVal > 255)
                rawVal = 255;
            softBuffer[sampleNr] = rawVal;
        }
        for(sampleNr = 0 ;sampleNr < BUFFER_SIZE / 2; sampleNr++)
        {
            rawVal = intAmpl - sampleNr * intAmpl / (BUFFER_SIZE / 2) + (intOffset - intAmpl / 2 );
            if(rawVal < 0)
                rawVal = 0;
            else if(rawVal > 255)
                rawVal = 255;
            softBuffer[sampleNr] = rawVal;
        }
        break;
    case SAWTOOTH:
        for(;sampleNr < BUFFER_SIZE; sampleNr++)
        {
            rawVal = sampleNr * intAmpl / BUFFER_SIZE + (intOffset - intAmpl / 2 );
            if(rawVal < 0)
                rawVal = 0;
            else if(rawVal > 255)
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
    
    waveAmpl = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spinAmpl));
    waveOffset = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spinOffset));
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radioSine)))
    {
        waveType = SINEWAVE;
    }
    else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radioSquare)))
    {
        waveType = SQUAREWAVE;
    }
    else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radioTriangle)))
    {
        waveType = TRIANGLE;
    }
    else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radioSawtooth)))
    {
        waveType = SAWTOOTH;
    }
    FillSoftBuffer(waveType, softBufferCh0);
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radioSine1)))
    {
        waveType = SINEWAVE;
    }
    else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radioSquare1)))
    {
        waveType = SQUAREWAVE;
    }
    else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radioTriangle1)))
    {
        waveType = TRIANGLE;
    }
    else if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(radioSawtooth1)))
    {
        waveType = SAWTOOTH;
    }
    FillSoftBuffer(waveType, softBufferCh0);
}

static gboolean fillBuffer(int *fd)
{
    int samplesToSend;
    int ret;
    
    samplesToSend = BUFFER_SIZE - currentSample;
    ret = write(*fd, softBufferCh0 + currentSample, samplesToSend);
    if(ret < 0)
    {
        printf("Error occured while writing to buffer\n");
    }
    else
    {
        currentSample += ret;
        if(currentSample == BUFFER_SIZE)
        {
            currentSample = 0;
        }
    }
    usleep(10000);
    
    return TRUE;
}

void startWaveGeneration(int * fd)
{
    fill_buffer_function = g_idle_add((GSourceFunc)fillBuffer, fd);
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

static void save_button_clicked(GtkButton *btn, gpointer data)
{
	int fd;
    if(gtk_toggle_button_get_active((GtkToggleButton *)radioSingleVal))
    {
        iio_save_widgets(tx_widgets, num_tx);
        iio_save_widgets(rx_widgets, num_rx);
        rx_update_labels();
    }
    else if(gtk_toggle_button_get_active((GtkToggleButton *)radioWaveform))
    {
        fd = buffer_open(BUFFER_SIZE * 10);
        generateWavePeriod();
        startWaveGeneration(&fd);
    }
}

static int AD7303_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *AD7303_panel;

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "AD7303.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "AD7303.glade", NULL);

	AD7303_panel = GTK_WIDGET(gtk_builder_get_object(builder, "tablePanelAD7303"));
    radioSine = GTK_WIDGET(gtk_builder_get_object(builder, "radioSine"));
    radioSquare = GTK_WIDGET(gtk_builder_get_object(builder, "radioSquare"));
    radioTriangle = GTK_WIDGET(gtk_builder_get_object(builder, "radioTriangle"));
    radioSawtooth = GTK_WIDGET(gtk_builder_get_object(builder, "radioSawtooth"));
    radioSine1 = GTK_WIDGET(gtk_builder_get_object(builder, "radioSine1"));
    radioSquare1 = GTK_WIDGET(gtk_builder_get_object(builder, "radioSquare1"));
    radioTriangle1 = GTK_WIDGET(gtk_builder_get_object(builder, "radioTriangle1"));
    radioSawtooth1 = GTK_WIDGET(gtk_builder_get_object(builder, "radioSawtooth1"));
    spinAmpl = GTK_WIDGET(gtk_builder_get_object(builder, "spinAmpl"));
    spinOffset = GTK_WIDGET(gtk_builder_get_object(builder, "spinOffset"));
    radioSingleVal = GTK_WIDGET(gtk_builder_get_object(builder, "radioSingleVal"));
    radioWaveform = GTK_WIDGET(gtk_builder_get_object(builder, "radioWaveform"));
    
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

	g_builder_connect_signal(builder, "buttonSave", "clicked",
		G_CALLBACK(save_button_clicked), NULL);

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
