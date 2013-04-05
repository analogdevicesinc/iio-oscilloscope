/**
 * Copyright (C) 2012 Analog Devices, Inc.
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

static int AD5628_1_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *AD5628_1_panel;

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "AD5628_1.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "AD5628_1.glade", NULL);

	AD5628_1_panel = GTK_WIDGET(gtk_builder_get_object(builder, "tablePanelAD5628_1"));

	/* Bind the IIO device files to the GUI widgets */
	
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage0_raw",
			builder, "spinbuttonRawValue0", NULL);	
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage0_powerdown_mode",
			builder, "comboboxPwrDwnModes0", NULL);
    iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage0_powerdown",
			builder, "checkbuttonPowerdown0");

	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage1_raw",
			builder, "spinbuttonRawValue1", NULL);	
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage1_powerdown_mode",
			builder, "comboboxPwrDwnModes1", NULL);
    iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage1_powerdown",
			builder, "checkbuttonPowerdown1");
            
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage2_raw",
			builder, "spinbuttonRawValue2", NULL);	
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage2_powerdown_mode",
			builder, "comboboxPwrDwnModes2", NULL);
    iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage2_powerdown",
			builder, "checkbuttonPowerdown2");    

	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage3_raw",
			builder, "spinbuttonRawValue3", NULL);	
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage3_powerdown_mode",
			builder, "comboboxPwrDwnModes3", NULL);
    iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage3_powerdown",
			builder, "checkbuttonPowerdown3");      
            
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage4_raw",
			builder, "spinbuttonRawValue4", NULL);	
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage4_powerdown_mode",
			builder, "comboboxPwrDwnModes4", NULL);
    iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage4_powerdown",
			builder, "checkbuttonPowerdown4");  
 
 	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage5_raw",
			builder, "spinbuttonRawValue5", NULL);	
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage5_powerdown_mode",
			builder, "comboboxPwrDwnModes5", NULL);
    iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage5_powerdown",
			builder, "checkbuttonPowerdown5");      
            
	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage6_raw",
			builder, "spinbuttonRawValue6", NULL);	
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage6_powerdown_mode",
			builder, "comboboxPwrDwnModes6", NULL);
    iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage6_powerdown",
			builder, "checkbuttonPowerdown6");                                        
 
 	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage7_raw",
			builder, "spinbuttonRawValue7", NULL);	
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage7_powerdown_mode",
			builder, "comboboxPwrDwnModes7", NULL);
    iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
			"ad5628-1", "out_voltage7_powerdown",
			builder, "checkbuttonPowerdown7");                                        

	g_builder_connect_signal(builder, "buttonSave", "clicked",
		G_CALLBACK(save_button_clicked), NULL);

	tx_update_values();
	rx_update_values();

	gtk_widget_unparent(AD5628_1_panel);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), AD5628_1_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), AD5628_1_panel, "AD5628-1");

	return 0;
}

static bool AD5628_1_identify(void)
{
	
    //return !set_dev_paths("ad5628-1");
    return TRUE;
}

const struct osc_plugin plugin = {
	.name = "AD5628-1",
	.identify = AD5628_1_identify,
	.init = AD5628_1_init,
};
