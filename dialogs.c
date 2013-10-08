/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <matio.h>

#include "fru.h"
#include "osc.h"
#include "iio_utils.h"

extern gfloat **channel_data;
extern unsigned int num_samples;
extern unsigned int num_active_channels;
extern const char *current_device;
extern double adc_freq;
extern char adc_scale[10];

typedef struct _Dialogs Dialogs;
struct _Dialogs
{
	GtkWidget *about;
	GtkWidget *saveas;
	GtkWidget *connect;
	GtkWidget *connect_fru;
	GtkWidget *connect_iio;
};

static Dialogs dialogs;

void connect_fillin(Dialogs *data)
{
	char eprom_names[128];
	unsigned char *raw_input_data = NULL;
	struct FRU_DATA *fru = NULL;
	FILE *efp, *fp;
	GtkTextBuffer *buf;
	GtkTextIter iter;
	size_t bytes;
	char text[256];
	int num;
	char *devices=NULL, *device;

	/* flushes all open output streams */
	fflush(NULL);

#if DEBUG
	fp = popen("find ./ -name \"fru*.bin\"", "r");
#else
	fp = popen("find /sys -name eeprom 2>/dev/null", "r");
#endif

	if(fp == NULL) {
		fprintf(stderr, "can't execute find\n");
		return;
	}

	buf = gtk_text_buffer_new(NULL);
	gtk_text_buffer_get_iter_at_offset(buf, &iter, 0);

	num = 0;
	while(fgets(eprom_names, sizeof(eprom_names), fp) != NULL){
		num++;
		/* strip trailing new lines */
		if (eprom_names[strlen(eprom_names) - 1] == '\n')
			eprom_names[strlen(eprom_names) - 1] = '\0';

		sprintf(text, "Found %s:\n", eprom_names);
		gtk_text_buffer_insert(buf, &iter, text, -1);

		efp = fopen(eprom_names, "rb");
		if(efp == NULL) {
			int errsv = errno;
			printf_err("Cannot open file %s\n%s\n", eprom_names, strerror(errsv));
			return;
		}

		raw_input_data = x_calloc(1, 1024);
		bytes = fread(raw_input_data, 1024, 1, efp);
		fclose(efp);

		/* Since EOF should be reached, bytes should be zero */
		if (!bytes)
			fru = parse_FRU(raw_input_data);

		if (fru) {
			if (fru->Board_Area->manufacturer && fru->Board_Area->manufacturer[0] & 0x3F) {
				sprintf(text, "FMC Manufacture : %s\n", &fru->Board_Area->manufacturer[1]);
				gtk_text_buffer_insert(buf, &iter, text, -1);
			}
			if (fru->Board_Area->product_name && fru->Board_Area->product_name[0] & 0x3F) {
				sprintf(text, "Product Name: %s\n", &fru->Board_Area->product_name[1]);
				gtk_text_buffer_insert(buf, &iter, text, -1);
			}
			if (fru->Board_Area->part_number && fru->Board_Area->part_number[0] & 0x3F) {
				sprintf(text, "Part Number : %s\n", &fru->Board_Area->part_number[1]);
				gtk_text_buffer_insert(buf, &iter, text, -1);
			}
			if (fru->Board_Area->serial_number && fru->Board_Area->serial_number[0] & 0x3F) {
				sprintf(text, "Serial Number : %s\n", &fru->Board_Area->serial_number[1]);
				gtk_text_buffer_insert(buf, &iter, text, -1);
			}
			if (fru->Board_Area->mfg_date) {
				time_t tmp = min2date(fru->Board_Area->mfg_date);
				sprintf(text, "Date of Man : %s", ctime(&tmp));
				gtk_text_buffer_insert(buf, &iter, text, -1);
			}

			sprintf(text, "\n");
			gtk_text_buffer_insert(buf, &iter, text, -1);

			free(fru);
			fru = NULL;
		} else {
			sprintf(text, "Not a FRU file\n");
			gtk_text_buffer_insert(buf, &iter, text, -1);
		}

		free (raw_input_data);
	}
	pclose(fp);

	if (!num) {
		sprintf(text, "No eeprom files found in /sys/\n");
		gtk_text_buffer_insert(buf, &iter, text, -1);
	}
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(data->connect_fru), buf);
	g_object_unref(buf);

	buf = gtk_text_buffer_new(NULL);
	gtk_text_buffer_get_iter_at_offset(buf, &iter, 0);

	num = find_iio_names(&devices, NULL);
	device=devices;
	if (num > 0) {
		for (; num > 0; num--) {
			sprintf(text, "%s\n", devices);
			gtk_text_buffer_insert(buf, &iter, text, -1);
			devices += strlen(devices) + 1;
		}
	} else {
		sprintf(text, "No iio devices found\n");
		gtk_text_buffer_insert(buf, &iter, text, -1);
	}
	free(device);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(data->connect_iio), buf);
	g_object_unref(buf);

	return;
}

G_MODULE_EXPORT void cb_connect(GtkButton *button, Dialogs *data)
{
	/* Connect Dialog */
	gint ret;

	do {
		ret = gtk_dialog_run(GTK_DIALOG(data->connect));
		if (ret == GTK_RESPONSE_APPLY)
			connect_fillin(data);
	} while (ret == GTK_RESPONSE_APPLY);

	switch(ret) {
		case GTK_RESPONSE_CANCEL:
			printf("Cancel\n");
			break;
		case GTK_RESPONSE_OK:
			printf("OK\n");
			break;
		default:
			printf("unknown: %i\n", ret);
			break;
	}
	gtk_widget_hide(data->connect);

}
G_MODULE_EXPORT void cb_show_about(GtkButton *button, Dialogs *data)
{
	/* About dialog */
	gtk_dialog_run(GTK_DIALOG(data->about));
	gtk_widget_hide(data->about);
}

G_MODULE_EXPORT void cb_saveas(GtkButton *button, Dialogs *data)
{
	/* Save as Dialog */
	gint ret;
	static char *filename = NULL, *name;

	if (!channel_data || !num_active_channels)
		return;

	gtk_file_chooser_set_action(GTK_FILE_CHOOSER (data->saveas), GTK_FILE_CHOOSER_ACTION_SAVE);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(data->saveas), TRUE);

	if(!filename) {
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (data->saveas), getenv("HOME"));
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER (data->saveas), current_device);
	} else {
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER (data->saveas), filename);
		g_free(filename);
	}

	ret = gtk_dialog_run(GTK_DIALOG(data->saveas));

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (data->saveas));
	if (filename) {
		name = malloc(strlen(filename) + 4);
		switch(ret) {
			/* Response Codes encoded in glade file */
			case GTK_RESPONSE_DELETE_EVENT:
			case GTK_RESPONSE_CANCEL:
				break;
			case 5:
				/* Save as Agilent VSA formatted file */
				sprintf(name, "%s.txt", filename);
				{
					FILE *fp;
					unsigned int i, j;
					double freq;

					fp = fopen(name, "w");
					if (!fp)
						break;
					fprintf(fp, "InputZoom\tTRUE\n");
					fprintf(fp, "InputCenter\t0\n");
					fprintf(fp, "InputRange\t1\n");
					fprintf(fp, "InputRefImped\t50\n");
					fprintf(fp, "XStart\t0\n");
					if (!strcmp(adc_scale, "MSPS"))
						freq = adc_freq * 1000000;
					else if (!strcmp(adc_scale, "kSPS"))
						freq = adc_freq * 1000;
					else {
						printf("error in writing\n");
						break;
					}

					fprintf(fp, "XDelta\t%-.17f\n", 1.0/freq);
					fprintf(fp, "XDomain\t2\n");
					fprintf(fp, "XUnit\tSec\n");
					fprintf(fp, "YUnit\tV\n");
					fprintf(fp, "FreqValidMax\t%e\n", freq / 2);
					fprintf(fp, "FreqValidMin\t-%e\n", freq / 2);
					fprintf(fp, "Y\n");

					for (j = 0; j < num_samples; j++) {
						for (i = 0; i < num_active_channels ; i++) {
							fprintf(fp, "%g", channel_data[i][j]);
							if (i < (num_active_channels - 1))
								fprintf(fp, "\t");
						}
						fprintf(fp, "\n");
					}
					fprintf(fp, "\n");
					fclose(fp);
				}
				break;

			case 4:
				sprintf(name, "%s.mat", filename);
				/* Matlab file
				 * http://na-wiki.csc.kth.se/mediawiki/index.php/MatIO
				 */
				{
					mat_t *mat;
					matvar_t *matvar;
					int dims[2];

					dims[0] = num_active_channels;
					dims[1] = num_samples;

					mat = Mat_Open(name, MAT_ACC_RDWR);
					if(mat) {
						matvar = Mat_VarCreate("IIO_vec",MAT_C_DOUBLE,MAT_T_DOUBLE,2,dims,channel_data,0);
						Mat_VarWrite( mat, matvar, 0);
						Mat_VarFree(matvar);
						Mat_Close(mat);
					}
				}
				break;
			case 2:
				sprintf(name, "%s.csv", filename);
				/* save comma seperated valus (csv) */
				{
					FILE *fp;
					unsigned int i, j;

					fp = fopen(name, "w");
					if (!fp)
						break;

					for (j = 0; j < num_samples; j++) {
						for (i = 0; i < num_active_channels ; i++) {
							fprintf(fp, "%g", channel_data[i][j]);
							if (i < (num_active_channels - 1))
								fprintf(fp, ", ");
						}
						fprintf(fp, "\n");
					}
					fprintf(fp, "\n");
					fclose(fp);
				}
				break;
			case 3:
				/* save_png */
				sprintf(name, "%s.png", filename);
				{
					GdkPixbuf *pixbuf;
					GError *err=NULL;
					GdkColormap *cmap;
					gint width, height;
					gboolean ret = true;

					cmap = gdk_window_get_colormap(
							GDK_DRAWABLE(gtk_widget_get_window(capture_graph)));
					gdk_drawable_get_size(GDK_DRAWABLE(gtk_widget_get_window(capture_graph)),
							&width, &height);
					pixbuf = gdk_pixbuf_get_from_drawable(NULL,
							GDK_DRAWABLE(gtk_widget_get_window(capture_graph)),
							cmap, 0, 0, 0, 0, width, height);

					if (pixbuf)
						ret = gdk_pixbuf_save(pixbuf, name, "png", &err, NULL);
					if (!pixbuf || !ret)
						printf("error creating %s\n", name);
				}
				break;
			default:
				printf("ret : %i\n", ret);
		}
	}
	gtk_widget_hide(data->saveas);
}

G_MODULE_EXPORT void cb_quit(GtkButton *button, Dialogs *data)
{
	application_quit();
}

void dialogs_init(GtkBuilder *builder)
{
	GtkWidget *tmp, *tmp2;

	dialogs.about = GTK_WIDGET(gtk_builder_get_object(builder, "About_dialog"));
	dialogs.saveas = GTK_WIDGET(gtk_builder_get_object(builder, "saveas_dialog"));
	dialogs.connect = GTK_WIDGET(gtk_builder_get_object(builder, "connect_dialog"));
	dialogs.connect_fru = GTK_WIDGET(gtk_builder_get_object(builder, "fru_info"));
	dialogs.connect_iio = GTK_WIDGET(gtk_builder_get_object(builder, "connect_iio_devices"));

	connect_fillin(&dialogs);
	gtk_builder_connect_signals(builder, &dialogs);

	/* Bind some dialogs radio buttons to text/labels */
	tmp2 = GTK_WIDGET(gtk_builder_get_object(builder, "connect_net"));
	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "connect_net_label"));
	g_object_bind_property(tmp2, "active", tmp, "sensitive", 0);
	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "connect_net_IP"));
	g_object_bind_property(tmp2, "active", tmp, "sensitive", 0);
}
