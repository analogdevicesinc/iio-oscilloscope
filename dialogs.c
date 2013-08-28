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

#include "fru.h"
#include "osc.h"
#include "iio_utils.h"

typedef struct _Dialogs Dialogs;
struct _Dialogs
{
	GtkWidget *about;
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

G_MODULE_EXPORT void cb_quit(GtkButton *button, Dialogs *data)
{
	application_quit();
}

void dialogs_init(GtkBuilder *builder)
{
	GtkWidget *tmp, *tmp2;

	dialogs.about = GTK_WIDGET(gtk_builder_get_object(builder, "About_dialog"));
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
