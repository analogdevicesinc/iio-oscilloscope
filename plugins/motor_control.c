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
static gboolean is_torque_controller;
static char crt_device_name[100];

/* Torque Controller Widgets */
static GtkWidget *controller_type;
static GtkWidget *pwm;
static GtkWidget *ref_speed;
static GtkWidget *kp;
static GtkWidget *ki;
static GtkWidget *kp1;
static GtkWidget *ki1;
static GtkWidget *kd1;

/* Advanced Controller Widgets */
static GtkWidget *command;
static GtkWidget *velocity_p;
static GtkWidget *velocity_i;
static GtkWidget *current_p;
static GtkWidget *current_i;
static GtkWidget *controller_mode;
static GtkWidget *openloop_bias;
static GtkWidget *openloop_scalar;
static GtkWidget *zero_offset;

/* GPO Wigdets */
static GtkWidget *gpo[11];
static int gpo_id[11];

#define USE_PWM_PERCENT_MODE -1
#define PWM_FULL_SCALE	2047
static int TOTAL_NUM_BITS;
static int Kxy_NUM_FRAC_BITS = 14;
static int PWM_PERCENT_FLAG = -1;

static int COMMAND_NUM_FRAC_BITS = 8;
static int VELOCITY_P_NUM_FRAC_BITS = 16;
static int VELOCITY_I_NUM_FRAC_BITS = 15;
static int CURRENT_P_NUM_FRAC_BITS = 10;
static int CURRENT_I_NUM_FRAC_BITS = 2;
static int OPEN_LOOP_BIAS_NUM_FRAC_BITS = 14;
static int OPEN_LOOP_SCALAR_NUM_FRAC_BITS = 16;
static int OENCODER_NUM_FRAC_BITS = 14;

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	set_dev_paths(crt_device_name);
	iio_w->save(iio_w);
}

static gboolean change_controller_type_label(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer data)
{
	if (g_value_get_boolean(source_value))
		g_value_set_static_string(target_value, "Torque Controller");
	else
		g_value_set_static_string(target_value, "Manual PWM");
		
	return TRUE;
}

static gint spin_input_cb(GtkSpinButton *btn, gpointer new_value, gpointer data)
{
	gdouble value;
	gdouble fractpart;
	gdouble intpart;
	int32_t intvalue;
	const char *entry_buf;
	int fract_bits = *((int *)data);

	entry_buf = gtk_entry_get_text(GTK_ENTRY(btn));
	if (*((int *)data) == USE_PWM_PERCENT_MODE) {
		value = g_strtod(entry_buf, NULL);
		intvalue = (int32_t)(((value * PWM_FULL_SCALE) / 100.0) + 0.5);
	} else {
		value = g_strtod(entry_buf, NULL);
		fractpart = modf(value, &intpart);
		fractpart = ((1 << fract_bits) * fractpart) + 0.5;
		intvalue = ((int32_t)intpart << fract_bits) | (int32_t)fractpart;
	}
	*((gdouble *)new_value) = (gdouble)intvalue;
	
	return TRUE;
}

static gboolean spin_output_cb(GtkSpinButton *spin, gpointer data)
{
	GtkAdjustment *adj;
	gchar *text;
	int value;
	gdouble fvalue;
	int fract_bits = *((int *)data);

	adj = gtk_spin_button_get_adjustment(spin);
	value = (int)gtk_adjustment_get_value(adj);
	if (*((int *)data) == USE_PWM_PERCENT_MODE) {
		fvalue = ((float)value / (float)PWM_FULL_SCALE) * 100;
		text = g_strdup_printf("%.2f%%", fvalue);
	} else {
		fvalue = (value >> fract_bits) + (gdouble)(value & ((1 << fract_bits) - 1)) / (gdouble)(1 << fract_bits);
		text = g_strdup_printf("%.5f", fvalue);
	}
	gtk_entry_set_text(GTK_ENTRY(spin), text);
	g_free(text);
	
	return TRUE;
}

static void gpo_toggled_cb(GtkToggleButton *btn, gpointer data)
{
	int id = *((int *)data);
	int attribute_value;
	
	set_dev_paths(crt_device_name);
	read_devattr_int("mc_torque_ctrl_gpo", &attribute_value);
	if (gtk_toggle_button_get_active(btn))
		attribute_value |= (1ul << id);
	else
		attribute_value &= ~(1ul << id);
	write_devattr_int("mc_torque_ctrl_gpo", attribute_value);
}

void create_iio_bindings_for_torque_ctrl(GtkBuilder *builder)
{
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-torque-ctrl", "mc_torque_ctrl_run",
		builder, "checkbutton_run", 0);
		
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-torque-ctrl", "mc_torque_ctrl_delta",
		builder, "checkbutton_delta", 0);
		
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++], 
		"ad-mc-torque-ctrl", "mc_torque_ctrl_matlab",
		builder, "togglebtn_controller_type", 0);
	controller_type = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-torque-ctrl", "mc_torque_ctrl_ref_speed",
		builder, "spinbutton_ref_speed", NULL);
	ref_speed = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-torque-ctrl", "mc_torque_ctrl_kp",
		builder, "spinbutton_kp", NULL);
	kp = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-torque-ctrl", "mc_torque_ctrl_ki",
		builder, "spinbutton_ki", NULL);
	ki = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-torque-ctrl", "mc_torque_ctrl_kp1",
		builder, "spinbutton_kp1", NULL);
	kp1 = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-torque-ctrl", "mc_torque_ctrl_ki1",
		builder, "spinbutton_ki1", NULL);
	ki1 = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-torque-ctrl", "mc_torque_ctrl_kd1",
		builder, "spinbutton_kd1", NULL);
	kd1 = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-torque-ctrl", "mc_torque_ctrl_pwm",
		builder, "spinbutton_pwm", NULL);
	pwm = tx_widgets[num_tx - 1].widget;
	
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-torque-ctrl", "mc_torque_ctrl_sensors",
		"mc_torque_ctrl_sensors_available", builder,
		"comboboxtext_sensors", NULL);
}

void create_iio_bindings_for_advanced_ctrl(GtkBuilder *builder)
{
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_run",
		builder, "checkbutton_run_adv", 0);
	
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_delta",
		builder, "checkbutton_delta_adv", 0);
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_command",
		builder, "spinbutton_command", NULL);
	command = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_velocity_p_gain",
		builder, "spinbutton_velocity_p", NULL);
	velocity_p = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_velocity_i_gain",
		builder, "spinbutton_velocity_i", NULL);
	velocity_i = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_current_p_gain",
		builder, "spinbutton_current_p", NULL);
	current_p = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_current_i_gain",
		builder, "spinbutton_current_i", NULL);
	current_i = tx_widgets[num_tx - 1].widget;
	
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_matlab",
		builder, "togglebtn_controller_type_adv", 0);
	controller_type = tx_widgets[num_tx - 1].widget;
	
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_controller_mode",
		"mc_adv_ctrl_controller_mode_available", builder,
		"combobox_controller_mode", NULL);
	controller_mode = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_pwm",
		builder, "spinbutton_pwm_adv", NULL);
	pwm = tx_widgets[num_tx - 1].widget;
	
	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_sensors",
		"mc_adv_ctrl_sensors_available",builder,
		"comboboxtext_sensors_adv", NULL);
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_open_loop_bias",
		builder, "spinbutton_open_loop_bias", NULL);
	openloop_bias = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_open_loop_scalar",
		builder, "spinbutton_open_loop_scalar", NULL);
	openloop_scalar = tx_widgets[num_tx - 1].widget;
	
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad-mc-adv-ctrl", "mc_adv_ctrl_encoder_zero_offset",
		builder, "spinbutton_zero_offset", NULL);
	zero_offset = tx_widgets[num_tx - 1].widget;
}

static int motor_control_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *motor_control_panel;
	GtkWidget *box_manpwm_widgets;
	GtkWidget *box_manpwm_lbls;
	GtkWidget *box_controller_widgets;
	GtkWidget *box_controller_lbls;
	GtkWidget *torque_widgets;
	GtkWidget *advanced_widgets;
	int i;
	
	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "motor_control.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "motor_control.glade", NULL);
	
	if (set_dev_paths("ad-mc-adv-ctrl") == 0) {
		sprintf(crt_device_name, "%s", "ad-mc-adv-ctrl");
		is_torque_controller = FALSE;
		TOTAL_NUM_BITS = 18;
	} else {
		sprintf(crt_device_name, "%s", "ad-mc-torque-ctrl");
		is_torque_controller = TRUE;
		TOTAL_NUM_BITS = 32;
	}
	
	motor_control_panel = GTK_WIDGET(gtk_builder_get_object(builder, "tablePanelMotor_Control"));
	torque_widgets = GTK_WIDGET(gtk_builder_get_object(builder, "table_torque_ctrl_widgets"));
	advanced_widgets = GTK_WIDGET(gtk_builder_get_object(builder, "table_advanced_ctrl_widgets"));
	
	char widget_name[25];
	for (i = 0; i < sizeof(gpo)/sizeof(gpo[0]); i++) {
		sprintf(widget_name, "checkbutton_gpo%d", i+1);
		gpo_id[i] = i;
		gpo[i] = GTK_WIDGET(gtk_builder_get_object(builder, widget_name));
		g_signal_connect(G_OBJECT(gpo[i]), "toggled", G_CALLBACK(gpo_toggled_cb), &gpo_id[i]);
	}
	
	if (is_torque_controller) {
		box_manpwm_widgets = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_manual_pwm_widgets"));
		box_manpwm_lbls = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_manual_pwm_lbls"));
		box_controller_widgets = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_torque_ctrl_widgets"));
		box_controller_lbls = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_torque_ctrl_lbls"));
		gtk_widget_show(torque_widgets);
		gtk_widget_hide(advanced_widgets);
	} else {
		box_manpwm_widgets = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_manual_pwm_widgets_adv"));
		box_manpwm_lbls = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_manual_pwm_lbls_adv"));
		box_controller_widgets = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_matlab_ctrl_widgets_adv"));
		box_controller_lbls = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_matlab_ctrl_lbls_adv"));
		gtk_widget_show(advanced_widgets);
		gtk_widget_hide(torque_widgets);
	}

	/* Bind the IIO device files to the GUI widgets */
	if (is_torque_controller)
		create_iio_bindings_for_torque_ctrl(builder);
	else
		create_iio_bindings_for_advanced_ctrl(builder);
	
	/* Connect signals. */
	
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
	if (is_torque_controller) {
		g_signal_connect(G_OBJECT(pwm), "input", G_CALLBACK(spin_input_cb), &PWM_PERCENT_FLAG);
		g_signal_connect(G_OBJECT(pwm), "output", G_CALLBACK(spin_output_cb), &PWM_PERCENT_FLAG);
		g_signal_connect(G_OBJECT(kp), "input", G_CALLBACK(spin_input_cb), &Kxy_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(kp), "output", G_CALLBACK(spin_output_cb), &Kxy_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(ki), "input", G_CALLBACK(spin_input_cb), &Kxy_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(ki), "output", G_CALLBACK(spin_output_cb), &Kxy_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(kp1), "input", G_CALLBACK(spin_input_cb), &Kxy_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(kp1), "output", G_CALLBACK(spin_output_cb), &Kxy_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(ki1), "input", G_CALLBACK(spin_input_cb), &Kxy_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(ki1), "output", G_CALLBACK(spin_output_cb), &Kxy_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(kd1), "input", G_CALLBACK(spin_input_cb), &Kxy_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(kd1), "output", G_CALLBACK(spin_output_cb), &Kxy_NUM_FRAC_BITS);
	} else {
		g_signal_connect(G_OBJECT(pwm), "input", G_CALLBACK(spin_input_cb), &PWM_PERCENT_FLAG);
		g_signal_connect(G_OBJECT(pwm), "output", G_CALLBACK(spin_output_cb), &PWM_PERCENT_FLAG);
		g_signal_connect(G_OBJECT(command), "input", G_CALLBACK(spin_input_cb), &COMMAND_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(command), "output", G_CALLBACK(spin_output_cb), &COMMAND_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(velocity_p), "input", G_CALLBACK(spin_input_cb), &VELOCITY_P_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(velocity_p), "output", G_CALLBACK(spin_output_cb), &VELOCITY_P_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(velocity_i), "input", G_CALLBACK(spin_input_cb), &VELOCITY_I_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(velocity_i), "output", G_CALLBACK(spin_output_cb), &VELOCITY_I_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(current_p), "input", G_CALLBACK(spin_input_cb), &CURRENT_P_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(current_p), "output", G_CALLBACK(spin_output_cb), &CURRENT_P_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(current_i), "input", G_CALLBACK(spin_input_cb), &CURRENT_I_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(current_i), "output", G_CALLBACK(spin_output_cb), &CURRENT_I_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(openloop_bias), "input", G_CALLBACK(spin_input_cb), &OPEN_LOOP_BIAS_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(openloop_bias), "output", G_CALLBACK(spin_output_cb), &OPEN_LOOP_BIAS_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(openloop_scalar), "input", G_CALLBACK(spin_input_cb), &OPEN_LOOP_SCALAR_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(openloop_scalar), "output", G_CALLBACK(spin_output_cb), &OPEN_LOOP_SCALAR_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(zero_offset), "input", G_CALLBACK(spin_input_cb), &OENCODER_NUM_FRAC_BITS);
		g_signal_connect(G_OBJECT(zero_offset), "output", G_CALLBACK(spin_output_cb), &OENCODER_NUM_FRAC_BITS);
	}
	
	/* Bind properties. */
	
	/* Show widgets listed below when in "Torque Controller" state */
	g_object_bind_property(controller_type, "active", box_controller_widgets, "visible", 0);
	g_object_bind_property(controller_type, "active", box_controller_lbls, "visible", 0);	
	/* Show widgets listed below when in "Manual PWM" state */
	g_object_bind_property(controller_type, "active", box_manpwm_widgets, "visible", G_BINDING_INVERT_BOOLEAN);
	g_object_bind_property(controller_type, "active", box_manpwm_lbls, "visible", G_BINDING_INVERT_BOOLEAN);
	/* Change between "Torque Controller" and "Manual PWM" labels on a toggle button */
	g_object_bind_property_full(controller_type, "active", controller_type, "label", 0, change_controller_type_label, NULL, NULL, NULL);
	
	tx_update_values();
	
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), motor_control_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), motor_control_panel, "Motor Control");

	return 0;
}

static bool motor_control_identify(void)
{
	if (!set_dev_paths("ad-mc-torque-ctrl"))
		return TRUE;
	if(!set_dev_paths("ad-mc-adv-ctrl"))
		return TRUE;
	return FALSE;
}

const struct osc_plugin plugin = {
	.name = "Motor Control",
	.identify = motor_control_identify,
	.init = motor_control_init,
};
