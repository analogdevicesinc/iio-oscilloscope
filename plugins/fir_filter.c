/**
 * Copyright (C) 2012-2015 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#include <errno.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <iio.h>
#include <string.h>

#include "../config.h"
#include "../osc.h"

int load_fir_filter(const char *file_name,
		struct iio_device *dev1, struct iio_device *dev2,
		GtkWidget *panel, GtkFileChooser *chooser,
		GtkWidget *fir_filter_en_tx, GtkWidget *enable_fir_filter_rx,
		GtkWidget *enable_fir_filter_rx_tx,
		GtkWidget *disable_all_fir_filters, char *last_fir_filter)
{
	bool rx = false, tx = false;
	int ret = -ENOMEM;
	gchar *ptr, *path = NULL;
	FILE *f;

	if (!strncmp(file_name, "@FILTERS@/", sizeof("@FILTERS@/") - 1))
		path = g_build_filename(OSC_FILTER_FILE_PATH,
				file_name + sizeof("@FILTERS@/") - 1, NULL);
	else
		path = g_strdup(file_name);
	if (!path)
		goto err_set_filename;

	for (ptr = path; *ptr; ptr++)
		if (*ptr == '/')
			*ptr = G_DIR_SEPARATOR_S[0];

	f = fopen(path, "r");
	if (f) {
		char *buf;
		ssize_t len;
		char line[80];
		int ret2;

		while (fgets(line, 80, f) != NULL && line[0] == '#');
		if (!strncmp(line, "RX", strlen("RX")))
			rx = true;
		else if (!strncmp(line, "TX", strlen("TX")))
			tx = true;
		if (fgets(line, 80, f) != NULL) {
			if (!strncmp(line, "TX", strlen("TX")))
				tx = true;
			else if (!strncmp(line, "RX", strlen("RX")))
				rx = true;
		}

		fseek(f, 0, SEEK_END);
		len = ftell(f);
		buf = malloc(len);
		fseek(f, 0, SEEK_SET);
		len = fread(buf, 1, len, f);
		fclose(f);

		ret = iio_device_attr_write_raw(dev1,
				"filter_fir_config", buf, len);
		if (dev2) {
			ret2 = iio_device_attr_write_raw(dev2,
					"filter_fir_config", buf, len);
			ret = (ret > ret2) ? ret2 : ret;
		}
		free(buf);
	}

	if (ret < 0) {
		fprintf(stderr, "FIR filter config failed: %s\n", path);
		GtkWidget *toplevel = gtk_widget_get_toplevel(panel);
		if (!gtk_widget_is_toplevel(toplevel))
			toplevel = NULL;

		GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(toplevel),
						GTK_DIALOG_MODAL,
						GTK_MESSAGE_ERROR,
						GTK_BUTTONS_CLOSE,
						"\nFailed to configure the FIR filter using the selected file.");
		gtk_window_set_title(GTK_WINDOW(dialog), "FIR Filter Configuration Failed");
		if (gtk_dialog_run(GTK_DIALOG(dialog)))
			gtk_widget_destroy(dialog);

	} else {
		if (last_fir_filter)
			strncpy(last_fir_filter, path, PATH_MAX);

		gtk_widget_hide(fir_filter_en_tx);
		gtk_widget_hide(enable_fir_filter_rx);
		gtk_widget_hide(enable_fir_filter_rx_tx);
		gtk_widget_show(disable_all_fir_filters);

		if (rx && tx)
			gtk_widget_show(enable_fir_filter_rx_tx);
		else if (rx)
			gtk_widget_show(enable_fir_filter_rx);
		else if (tx)
			gtk_widget_show(fir_filter_en_tx);
		else
			gtk_widget_hide(disable_all_fir_filters);
		gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON (disable_all_fir_filters), true);
	}

	printf("Filter loaded: %s (ret = %i)\n", path, ret);
	if (ret >= 0)
		gtk_file_chooser_set_filename(chooser, path);
	g_free(path);

err_set_filename:
	if (ret < 0) {
		if (last_fir_filter && last_fir_filter[0])
			gtk_file_chooser_set_filename(chooser, last_fir_filter);
		else
			gtk_file_chooser_set_filename(chooser, "(None)");
	}
	return ret;
}
