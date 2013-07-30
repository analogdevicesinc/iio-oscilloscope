/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <gtkdatabox_markers.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <malloc.h>
#include <dlfcn.h>

#include <fftw3.h>

#include "oscplot.h"
#include "datatypes.h"

extern char dev_dir_name[512];

struct buffer {
	void *data;
	unsigned int available;
	unsigned int size;
};

struct _device_list *device_list;
unsigned num_devices = 0;

/* Debug only */
struct iio_channel_info ch_devA[3];
struct iio_channel_info ch_devB[2];

gfloat data_buffer_devA_ch0[10] = {0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};
gfloat data_buffer_devA_ch1[10] = {1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9};
gfloat data_buffer_devA_ch2[10] = {2.0, 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9};
gfloat data_buffer_devB_ch0[10] = {5.0, 5.1, 5.2, 5.3, 5.4, 5.5, 5.6, 5.7, 5.8, 5.9};
gfloat data_buffer_devB_ch1[10] = {6.0, 6.1, 6.2, 6.3, 6.4, 6.5, 6.6, 6.7, 6.8, 6.9};

/* End Debug only */

static struct buffer data_buffer;
static unsigned int num_samples = 10;

static int capture_start_flag = 0;
static GList *plot_list = NULL;


void time_transform_test_function(Transform *tr, gboolean init_transform)
{
	int i;
	unsigned axis_length = *tr->in_data_size;
	
	if (init_transform) {
		tr->x_axis = g_renew(gfloat, tr->x_axis, axis_length);
		for (i = 0; i < axis_length; i++)
			tr->x_axis[i] = i;
		Transform_resize_out_buffer(tr, axis_length);
		
		return;
	}
	
	for (i = 0; i < axis_length; i++)
		tr->out_data[i] = tr->in_data[i];
}

void fft_transform_test_function(Transform *tr, gboolean init_transform)
{
	int i;
	unsigned axis_length = *tr->in_data_size;
	
	if (init_transform) {
		tr->x_axis = g_renew(gfloat, tr->x_axis, axis_length);
		for (i = 0; i < axis_length; i++)
			tr->x_axis[i] = i;
		Transform_resize_out_buffer(tr, axis_length);
		
		return;
	}
	
	for (i = 0; i < axis_length; i++) {
		if (i == 5)
			tr->out_data[i] = tr->in_data[i] * 1.41;
		else
			tr->out_data[i] = tr->in_data[i];
	}
}

void constellation_transform_test_function(Transform *tr, gboolean init_transform)
{
	//struct _constellation_settings settings = tr->settings;
	int i;
	unsigned axis_length = *tr->in_data_size;
	
	if (init_transform) {
		Transform_resize_out_buffer(tr, axis_length);
		
		return;
	}
	
	for (i = 0; i < axis_length; i++)
		tr->out_data[i] = tr->in_data[9 - i];
}

/* End Debug only */

void print_channel_status(void)
{
	int i, j;
	
	for (i = 0; i < num_devices; i++) {
		for (j = 0; j < device_list[i].num_channels; j++) {
			printf("Device[%d].channel[%d] has enabled status: %d\n", i, j, device_list[i].channel_list[j].enabled);
		}
	}
	printf("\n");
}

void start(OscPlot *plot, gboolean start_event, gpointer databox)
{
	if (start_event)
		capture_start_flag++;
	else
		capture_start_flag--;

/* Debug only */
//	if (start_event)
//		print_channel_status();
}

static void btn_capture_cb(GtkButton *button, gpointer user_data)
{
	GtkWidget *plot;
	
	plot = osc_plot_new();
	plot_list = g_list_append(plot_list, plot);
	g_signal_connect(plot, "capture-event", G_CALLBACK(start), NULL);
	gtk_widget_show(plot);
}

void application_quit (void)
{
	g_list_free(plot_list);
	gtk_main_quit();
}

void sigterm (int signum)
{
	application_quit();
}

void set_sample_count_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	struct _device_list *dev_list = user_data;
	GtkBuilder *builder = dev_list->settings_dialog_builder;
	GtkAdjustment *sample_count;
	
	if (response_id == 1) { /* OK button was pressed */
		sample_count = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_sample_count"));
		dev_list->sample_count = gtk_adjustment_get_value(sample_count);
	}
}

void init_device_list(void)
{
	/* Debug only */
	
	num_devices = 2;
	device_list = (struct _device_list *)malloc(sizeof(*device_list) * num_devices);
	
	device_list[0].channel_list = ch_devA;
	device_list[0].num_channels = 3;
	device_list[0].device_name = "DeviceA";
	device_list[0].sample_count = 10;
	device_list[0].channel_list[0].extra_field = data_buffer_devA_ch0;
	device_list[0].channel_list[1].extra_field = data_buffer_devA_ch1;
	device_list[0].channel_list[2].extra_field = data_buffer_devA_ch2;
	
	device_list[1].channel_list = ch_devB;
	device_list[1].num_channels = 2;
	device_list[1].device_name = "DeviceB";
	device_list[1].sample_count = 10;
	device_list[1].channel_list[0].extra_field = data_buffer_devB_ch0;
	device_list[1].channel_list[1].extra_field = data_buffer_devB_ch1;
	
	ch_devA[0].name = "Channel 0";
	ch_devA[1].name = "Channel 1";
	ch_devA[2].name = "Channel 2";
	
	ch_devB[0].name = "Channel 0";
	ch_devB[1].name = "Channel 1";
	
	/* End Debug only */
	
	GtkBuilder *builder = NULL;
	GError *error = NULL;    
	GtkWidget *dialog;
	int i;
	
	for (i = 0; i < num_devices; i++) {
		builder = gtk_builder_new();
		if (!gtk_builder_add_from_file(builder, "osc.glade", &error)) {
			g_warning("%s", error->message);
			g_free(error);
		}
		device_list[i].settings_dialog_builder = builder;
		dialog = GTK_WIDGET(gtk_builder_get_object(builder, "Sample_count_dialog"));
		g_signal_connect(dialog, "response", G_CALLBACK(set_sample_count_cb), &device_list[i]);
		gtk_window_set_title(GTK_WINDOW(dialog), (gchar *)device_list[i].device_name);
	}
}

void gfunc_update_plot(gpointer data, gpointer user_data)
{
	GtkWidget *plot = data;
	
	osc_plot_update(OSC_PLOT(plot));
}

static void init_application (void)
{
	GtkBuilder *builder = NULL;
	GError     *error   = NULL;    
	GtkWidget  *window;
	GtkWidget *btn_capture;
	
	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file(builder, "osc.glade", &error)) {
		g_warning("%s", error->message);
		g_free(error);
	}
	window = GTK_WIDGET(gtk_builder_get_object(builder, "main_menu"));
	btn_capture = GTK_WIDGET(gtk_builder_get_object(builder, "button_capture"));

	/* Connect signals. */
	g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(application_quit), NULL);
	g_signal_connect(G_OBJECT(btn_capture), "clicked", G_CALLBACK(btn_capture_cb), NULL);
   
	g_object_unref(G_OBJECT(builder));
	gtk_widget_show(window);	
	
	init_device_list();
}

gint main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	signal(SIGTERM, sigterm);
	signal(SIGINT, sigterm);
	signal(SIGHUP, sigterm);
	init_application();
	gtk_main();
	
	return 0;
}
