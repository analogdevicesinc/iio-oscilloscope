/**
 * Copyright (C) 2013 Analog Devices, Inc.
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

#include "../osc.h"
#include "../iio_widget.h"
#include "../iio_utils.h"
#include "../osc_plugin.h"
#include "../config.h"

static struct iio_widget tx_widgets[20];
static unsigned int num_tx;
static GtkWidget *controller_type;

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	set_dev_paths("ad-mc-controller");
	iio_w->save(iio_w);
}

static gboolean change_controller_type_label(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer data)
{
	if (g_value_get_boolean(source_value))
		g_value_set_static_string(target_value, "Matlab Controller");
	else
		g_value_set_static_string(target_value, "Open Loop Controller");
		
	return TRUE;
}

static int motor_control_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *motor_control_panel;
	
	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "motor_control.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "motor_control.glade", NULL);

	motor_control_panel = GTK_WIDGET(gtk_builder_get_object(builder, "tablePanelMotor_Control"));

	/* Bind the IIO device files to the GUI widgets */
	
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-controller", "motor_controller_run",
		builder, "checkbutton_run", 0);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-controller", "motor_controller_delta",
		builder, "checkbutton_delta", 0);
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++], 
		"ad-mc-controller", "motor_controller_matlab",
		builder, "togglebtn_controller_type", 0);
	controller_type = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-controller", "motor_controller_ref_speed",
		builder, "spinbutton_ref_speed", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-controller", "motor_controller_kp",
		builder, "spinbutton_kp", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-controller", "motor_controller_ki",
		builder, "spinbutton_ki", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-controller", "motor_controller_kd",
		builder, "spinbutton_kd", NULL);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-controller", "motor_controller_pwm",
		builder, "spinbutton_pwm", NULL);
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-controller", "motor_controller_sensors",
		"motor_controller_sensors_available",builder,
		"comboboxtext_sensors", NULL);
	
	/* Connect signals. */
	
	int i;
	char signal_name[25];
		
	for (i = 0; i < num_tx; i++) {
		if (GTK_IS_CHECK_BUTTON(tx_widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_TOGGLE_BUTTON(tx_widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_SPIN_BUTTON(tx_widgets[i].widget))
			sprintf(signal_name, "%s", "value-changed");
		else if (GTK_IS_COMBO_BOX_TEXT(tx_widgets[i].widget))
			sprintf(signal_name, "%s", "changed");
			
		g_signal_connect(G_OBJECT(tx_widgets[i].widget), signal_name, G_CALLBACK(save_widget_value), &tx_widgets[i]);
	}
	
	/* Bind properties. */
	
	g_object_bind_property_full(controller_type, "active", controller_type, "label", 0, change_controller_type_label, NULL, NULL, NULL);
	
	tx_update_values();
	
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), motor_control_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), motor_control_panel, "Motor Control");

	return 0;
}

static bool motor_control_identify(void)
{
	return !set_dev_paths("ad-mc-controller");
}

const struct osc_plugin plugin = {
	.name = "Motor Control",
	.identify = motor_control_identify,
	.init = motor_control_init,
};
