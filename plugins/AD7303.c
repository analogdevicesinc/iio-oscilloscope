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

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static unsigned int num_tx, num_rx;

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
	iio_save_widgets(tx_widgets, num_tx);
	iio_save_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

static int AD7303_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *AD7303_panel;

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "AD7303.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "AD7303.glade", NULL);

	AD7303_panel = GTK_WIDGET(gtk_builder_get_object(builder, "tablePanelAD7303"));

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
