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
#include <stdlib.h>
#include <string.h>

#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../libini2.c"

#define THIS_DRIVER "Motor Control"
#define AD_MC_CTRL "ad-mc-ctrl"
#define AD_MC_CTRL_2ND "ad-mc-ctrl-m2"
#define AD_MC_ADV_CTRL "ad-mc-adv-ctrl"
#define RESOLVER "ad2s1210"

#define ONE_MOTOR_GPO_MASK 0x7FF
#define TWO_MOTOR_GPO_MASK 0x00F

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

enum pid_no{
	PID_1ST_DEV,
	PID_2ND_DEV
};

extern int count_char_in_string(char c, const char *s);

static struct iio_widget tx_widgets[50];
static unsigned int num_tx;
static struct iio_device *crt_device, *pid_devs[2], *adv_dev,
				*resolver_dev;
static struct iio_context *ctx;
static unsigned gpo_mask;

/* Global Widgets */
static GtkWidget *controllers_notebook;
static GtkWidget *gpo[11];
static int gpo_id[11];

/* PID Controller Widgets */
static GtkWidget *controller_type_pid[2];
static GtkWidget *delta[2];
static GtkWidget *pwm_pid[2];
static GtkWidget *direction_pid[2];

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

/* Resolver Widgets */
static GtkWidget *resolver_angle;
static GtkWidget *resolver_angle_veloc;

#define USE_PWM_PERCENT_MODE -1
#define PWM_FULL_SCALE	2047
static int PWM_PERCENT_FLAG = -1;

static int COMMAND_NUM_FRAC_BITS = 8;
static int VELOCITY_P_NUM_FRAC_BITS = 16;
static int VELOCITY_I_NUM_FRAC_BITS = 15;
static int CURRENT_P_NUM_FRAC_BITS = 10;
static int CURRENT_I_NUM_FRAC_BITS = 2;
static int OPEN_LOOP_BIAS_NUM_FRAC_BITS = 14;
static int OPEN_LOOP_SCALAR_NUM_FRAC_BITS = 16;
static int OENCODER_NUM_FRAC_BITS = 14;

static bool can_update_widgets;

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *motor_control_panel;
static gboolean plugin_detached;

static const char *motor_control_sr_attribs[] = {
	AD_MC_CTRL".mc_ctrl_run",
	AD_MC_CTRL".mc_ctrl_delta",
	AD_MC_CTRL".mc_ctrl_direction",
	AD_MC_CTRL".mc_ctrl_matlab",
	AD_MC_CTRL_2ND".mc_ctrl_run",
	AD_MC_CTRL_2ND".mc_ctrl_delta",
	AD_MC_CTRL_2ND".mc_ctrl_direction",
	AD_MC_CTRL_2ND".mc_ctrl_matlab",
};

static const char * motor_control_driver_attribs[] = {
	"pwm",
	"pwm_2nd",
	"gpo.1",
	"gpo.2",
	"gpo.3",
	"gpo.4",
	"gpo.5",
	"gpo.6",
	"gpo.7",
	"gpo.8",
	"gpo.9",
	"gpo.10",
	"gpo.11",
};

const char *checkbutton_run[2] = {"checkbutton_run", "checkbutton_run_2nd"};
const char *checkbutton_delta[2] = {"checkbutton_delta", "checkbutton_delta_2nd"};
const char *togglebtn_direction[2] = {"togglebtn_direction", "togglebtn_direction_2nd"};
const char *togglebtn_controller_type[2] = {"togglebtn_controller_type", "togglebtn_controller_type_2nd"};
const char *spinbutton_pwm[2] = {"spinbutton_pwm", "spinbutton_pwm_2nd"};

const char *vbox_manual_pwm_lbls[2] = {"vbox_manual_pwm_lbls", "vbox_manual_pwm_lbls_2nd"};
const char *vbox_manual_pwm_widgets[2] = {"vbox_manual_pwm_widgets", "vbox_manual_pwm_widgets_2nd"};
const char *vbox_torque_ctrl_lbls[2] = {"vbox_torque_ctrl_lbls", "vbox_torque_ctrl_lbls_2nd"};
const char *vbox_torque_ctrl_widgets[2] = {"vbox_torque_ctrl_widgets", "vbox_torque_ctrl_widgets_2nd"};
const char *vbox_delta_lbls[2] = {"vbox_delta_lbls", "vbox_delta_lbls_2nd"};
const char *vbox_delta_widgets[2] = {"vbox_delta_widgets", "vbox_delta_widgets_2nd"};
const char *vbox_direction_lbls[2] = {"vbox_delta_lbls", "vbox_direction_lbls_2nd"};
const char *vbox_direction_widgets[2] = {"vbox_direction_widgets", "vbox_direction_widgets_2nd"};

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	iio_w->save(iio_w);
}

static gboolean update_display(gpointer foo)
{
	if (this_page != gtk_notebook_get_current_page(nbook) &&
			!plugin_detached)
		goto end;

	/* Update values only if "Resolver" tab is selected */
	if (gtk_notebook_get_current_page(
			GTK_NOTEBOOK(controllers_notebook)) != 2)
		goto end;

	char buf[1024];
	struct iio_channel *iio_chn;
	ssize_t ret;

	iio_chn = iio_device_find_channel(resolver_dev, "angl0", false);
	if (!iio_chn)
		goto end;
	ret = iio_channel_attr_read(iio_chn, "raw", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(resolver_angle), buf);
	else
		gtk_label_set_text(GTK_LABEL(resolver_angle), "<error>");

	iio_chn = iio_device_find_channel(resolver_dev, "anglvel0",
			false);
	if (!iio_chn)
		goto end;
	ret = iio_channel_attr_read(iio_chn, "raw", buf, sizeof(buf));
	if (ret > 0)
		gtk_label_set_text(GTK_LABEL(resolver_angle_veloc),
			buf);
	else
		gtk_label_set_text(GTK_LABEL(resolver_angle_veloc),
			"<error>");

end:
	return TRUE;
}

static gboolean change_controller_type_label(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer data)
{
	if (g_value_get_boolean(source_value))
		g_value_set_static_string(target_value, "Matlab Controller");
	else
		g_value_set_static_string(target_value, "Manual PWM");

	return TRUE;
}

static gboolean change_direction_label(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer data)
{
	if (g_value_get_boolean(source_value))
		g_value_set_static_string(target_value, "Clockwise");
	else
		g_value_set_static_string(target_value, "Counterclockwise");

	return TRUE;
}

static gboolean enable_widgets_of_manual_pwn_mode(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer data)
{
	const char *controller = g_value_get_string(source_value);

	if (!strncmp("Matlab Controller", controller, 17))
		g_value_set_boolean(target_value, FALSE);
	else
		g_value_set_boolean(target_value, TRUE);

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
	long long value;

	if (pid_devs[PID_1ST_DEV]) {
		iio_device_attr_read_longlong(pid_devs[PID_1ST_DEV],
				"mc_ctrl_gpo", &value);
		if (gtk_toggle_button_get_active(btn))
			value |= (1ul << id);
		else
			value &= ~(1ul << id);
		iio_device_attr_write_longlong(pid_devs[PID_1ST_DEV],
				"mc_ctrl_gpo", value & gpo_mask);
	}
	if (adv_dev) {
		iio_device_attr_read_longlong(adv_dev,
				"mc_adv_ctrl_gpo", &value);
		if (gtk_toggle_button_get_active(btn))
			value |= (1ul << id);
		else
			value &= ~(1ul << id);
		iio_device_attr_write_longlong(adv_dev,
				"mc_adv_ctrl_gpo", value & gpo_mask);
	}
}

static void resolver_resolution_update_val(GtkBuilder *builder)
{
	GtkComboBox *box;
	ssize_t ret;
	char buf[1024];
	int resolution;

	box = GTK_COMBO_BOX(gtk_builder_get_object(builder,
				"comboboxtext_resolver_resolution"));
	ret = iio_device_attr_read(resolver_dev, "bits", buf,
			sizeof(buf));
	if (ret > 0) {
		resolution = atoi(buf);
		if (resolution >= 10 && resolution <= 16)
			gtk_combo_box_set_active(box,
				(resolution / 2) - 5);
	} else {
		printf("read to <bits> attribute failed:%zd\n", ret);
	}
}

static void resolver_resolution_changed_cb(GtkComboBoxText *box,
		gpointer user_data)
{
	gchar *buf;
	ssize_t ret;

	if (!resolver_dev)
		return;

	buf = gtk_combo_box_text_get_active_text(box);
	if (buf) {
		ret = iio_device_attr_write(resolver_dev, "bits", buf);
		if (ret < 0)
			printf("write to <bits> attribute failed:%zd\n", ret);
		g_free(buf);
	}
}

static void create_iio_bindings_for_pid_ctrl(GtkBuilder *builder,
		enum pid_no i)
{
	struct iio_device *pid_dev = pid_devs[i];

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		pid_dev, NULL, "mc_ctrl_run",
		builder, checkbutton_run[i], 0);

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		pid_dev, NULL, "mc_ctrl_delta",
		builder, checkbutton_delta[i], 0);
	delta[i] = tx_widgets[num_tx - 1].widget;

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		pid_dev, NULL, "mc_ctrl_direction",
		builder, togglebtn_direction[i], 0);
	direction_pid[i] = tx_widgets[num_tx - 1].widget;

	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		pid_dev, NULL, "mc_ctrl_matlab",
		builder, togglebtn_controller_type[i], 0);
	controller_type_pid[i] = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		pid_dev, NULL, "mc_ctrl_pwm",
		builder, spinbutton_pwm[i], NULL);
	pwm_pid[i] = tx_widgets[num_tx - 1].widget;
}

static void create_iio_bindings_for_advanced_ctrl(GtkBuilder *builder)
{
	iio_toggle_button_init_from_builder(&tx_widgets[num_tx++],
		adv_dev, NULL, "mc_adv_ctrl_run",
		builder, "checkbutton_run_adv", 0);

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		adv_dev, NULL, "mc_adv_ctrl_command",
		builder, "spinbutton_command", NULL);
	command = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		adv_dev, NULL, "mc_adv_ctrl_velocity_p_gain",
		builder, "spinbutton_velocity_p", NULL);
	velocity_p = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		adv_dev, NULL, "mc_adv_ctrl_velocity_i_gain",
		builder, "spinbutton_velocity_i", NULL);
	velocity_i = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		adv_dev, NULL, "mc_adv_ctrl_current_p_gain",
		builder, "spinbutton_current_p", NULL);
	current_p = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		adv_dev, NULL, "mc_adv_ctrl_current_i_gain",
		builder, "spinbutton_current_i", NULL);
	current_i = tx_widgets[num_tx - 1].widget;

	iio_combo_box_init_from_builder(&tx_widgets[num_tx++],
		adv_dev, NULL, "mc_adv_ctrl_controller_mode",
		"mc_adv_ctrl_controller_mode_available", builder,
		"combobox_controller_mode", NULL);
	controller_mode = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		adv_dev, NULL, "mc_adv_ctrl_open_loop_bias",
		builder, "spinbutton_open_loop_bias", NULL);
	openloop_bias = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		adv_dev, NULL, "mc_adv_ctrl_open_loop_scalar",
		builder, "spinbutton_open_loop_scalar", NULL);
	openloop_scalar = tx_widgets[num_tx - 1].widget;

	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		adv_dev, NULL, "mc_adv_ctrl_encoder_zero_offset",
		builder, "spinbutton_zero_offset", NULL);
	zero_offset = tx_widgets[num_tx - 1].widget;
}

static void controllers_notebook_page_switched_cb (GtkNotebook *notebook,
	GtkWidget *page, guint page_num, gpointer user_data)
{
	const gchar *page_name;

	page_name = gtk_notebook_get_tab_label_text(notebook, page);
	if (!strcmp(page_name, "Controller"))
		crt_device = pid_devs[PID_1ST_DEV];
	else if (!strcmp(page_name, "Advanced"))
		crt_device = adv_dev;
	else if (!strcmp(page_name, "Resolver"))
		;
	else
		printf("Notebook page is unknown to the Motor Control Plugin\n");
}

static void pid_controller_init(GtkBuilder *builder, enum pid_no pid)
{
	GtkWidget *box_manpwm_pid_widgets;
	GtkWidget *box_manpwm_pid_lbls;
	GtkWidget *box_controller_pid_widgets;
	GtkWidget *box_controller_pid_lbls;

	box_manpwm_pid_widgets = GTK_WIDGET(
			gtk_builder_get_object(builder, vbox_manual_pwm_widgets[pid]));
	box_manpwm_pid_lbls = GTK_WIDGET(
			gtk_builder_get_object(builder, vbox_manual_pwm_lbls[pid]));
	box_controller_pid_widgets = GTK_WIDGET(
			gtk_builder_get_object(builder, vbox_torque_ctrl_widgets[pid]));
	box_controller_pid_lbls = GTK_WIDGET(
			gtk_builder_get_object(builder, vbox_torque_ctrl_lbls[pid]));

	/* Bind the IIO device files to the GUI widgets */
	create_iio_bindings_for_pid_ctrl(builder, pid);

	/* Connect signals. */
	g_signal_connect(G_OBJECT(pwm_pid[pid]), "input",
		G_CALLBACK(spin_input_cb), &PWM_PERCENT_FLAG);
	g_signal_connect(G_OBJECT(pwm_pid[pid]), "output",
		G_CALLBACK(spin_output_cb), &PWM_PERCENT_FLAG);

	/* Bind properties. */

	/* Show widgets listed below when in "PID Controller" state */
	g_object_bind_property(controller_type_pid[pid], "active",
		box_controller_pid_widgets, "visible", 0);
	g_object_bind_property(controller_type_pid[pid], "active",
		box_controller_pid_lbls, "visible", 0);
	/* Show widgets listed below when in "Manual PWM" state */
	g_object_bind_property(controller_type_pid[pid], "active",
		box_manpwm_pid_widgets, "visible", G_BINDING_INVERT_BOOLEAN);
	g_object_bind_property(controller_type_pid[pid], "active",
		box_manpwm_pid_lbls, "visible", G_BINDING_INVERT_BOOLEAN);
	/* Change between "PID Controller" and "Manual PWM" labels on a toggle button */
	g_object_bind_property_full(controller_type_pid[pid], "active",
		controller_type_pid[pid], "label", 0,
		change_controller_type_label, NULL, NULL, NULL);
	/* Change direction label between "CW" and "CCW" */
	g_object_bind_property_full(direction_pid[pid], "active",
		direction_pid[pid], "label", 0, change_direction_label,
		NULL, NULL, NULL);

	/* Hide widgets when Matlab Controller type is active */
	g_object_bind_property_full(controller_type_pid[pid], "label",
		gtk_builder_get_object(builder, vbox_delta_lbls[pid]), "visible",
		0, enable_widgets_of_manual_pwn_mode, NULL, NULL, NULL);
	g_object_bind_property_full(controller_type_pid[pid], "label",
		gtk_builder_get_object(builder, vbox_delta_widgets[pid]), "visible",
		0, enable_widgets_of_manual_pwn_mode, NULL, NULL, NULL);
	g_object_bind_property_full(controller_type_pid[pid], "label",
		gtk_builder_get_object(builder, vbox_direction_lbls[pid]), "visible",
		0, enable_widgets_of_manual_pwn_mode, NULL, NULL, NULL);
	g_object_bind_property_full(controller_type_pid[pid], "label",
		gtk_builder_get_object(builder, vbox_direction_widgets[pid]), "visible",
		0, enable_widgets_of_manual_pwn_mode, NULL, NULL, NULL);
	g_object_bind_property_full(controller_type_pid[pid], "label",
		gtk_builder_get_object(builder, vbox_manual_pwm_lbls[pid]), "visible",
		0, enable_widgets_of_manual_pwn_mode, NULL, NULL, NULL);
	g_object_bind_property_full(controller_type_pid[pid], "label",
		gtk_builder_get_object(builder, vbox_manual_pwm_widgets[pid]), "visible",
		0, enable_widgets_of_manual_pwn_mode, NULL, NULL, NULL);
}

static void advanced_controller_init(GtkBuilder *builder)
{
	/* Bind the IIO device files to the GUI widgets */
	create_iio_bindings_for_advanced_ctrl(builder);

	/* Connect signals. */
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

static void resolver_init(GtkBuilder *builder)
{

	resolver_angle = GTK_WIDGET(gtk_builder_get_object(builder,
					"resolver_angle"));
	resolver_angle_veloc = GTK_WIDGET(gtk_builder_get_object(builder,
					"resolver_angle_veloc"));

	/* Bind the IIO device files to the GUI widgets */
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		resolver_dev, NULL, "fexcit",
		builder, "spin_resolver_fexcit", NULL);

	/* Connect signals. */
	g_builder_connect_signal(builder,
		"comboboxtext_resolver_resolution", "changed",
		G_CALLBACK(resolver_resolution_changed_cb), NULL);

	/* Set up a periodic read-only widget update function */
	g_timeout_add(1000, (GSourceFunc) update_display, ctx);
}

static int motor_control_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	if (MATCH_ATTRIB("pwm")) {
		if (value[0]) {
			gtk_entry_set_text(GTK_ENTRY(pwm_pid[PID_1ST_DEV]), value);
			gtk_spin_button_update(GTK_SPIN_BUTTON(pwm_pid[PID_1ST_DEV]));
		}
	} if (MATCH_ATTRIB("pwm_2nd")) {
		if (value[0]) {
			gtk_entry_set_text(GTK_ENTRY(pwm_pid[PID_2ND_DEV]), value);
			gtk_spin_button_update(GTK_SPIN_BUTTON(pwm_pid[PID_2ND_DEV]));
		}
	} else if (!strncmp(attrib, "gpo.", sizeof("gpo.") - 1)) {
		int id = atoi(attrib + sizeof("gpo.") - 1);
		gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(gpo[id - 1]), !!atoi(value));
	} else if (MATCH_ATTRIB("SYNC_RELOAD")) {
		if (can_update_widgets)
			tx_update_values();
	} else {
		return -EINVAL;
	}

	return 0;
}

static int motor_control_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			motor_control_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(motor_control_driver_attribs); i++) {
		char *value = read_token_from_ini(ini_fn, THIS_DRIVER,
				motor_control_driver_attribs[i]);
		if (value) {
			motor_control_handle_driver(NULL,
					motor_control_driver_attribs[i], value);
			free(value);
		}
	}

	if (pid_devs[PID_1ST_DEV])
		update_from_ini(ini_fn, THIS_DRIVER, pid_devs[PID_1ST_DEV],
				motor_control_sr_attribs,
				ARRAY_SIZE(motor_control_sr_attribs));
	if (pid_devs[PID_2ND_DEV])
		update_from_ini(ini_fn, THIS_DRIVER, pid_devs[PID_2ND_DEV],
				motor_control_sr_attribs,
				ARRAY_SIZE(motor_control_sr_attribs));

	if (can_update_widgets)
		tx_update_values();
}

static GtkWidget * motor_control_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *pid_page;
	GtkWidget *advanced_page;
	GtkWidget *resolver_page;
	size_t i;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	pid_devs[PID_1ST_DEV] = iio_context_find_device(ctx, AD_MC_CTRL);
	pid_devs[PID_2ND_DEV] = iio_context_find_device(ctx, AD_MC_CTRL_2ND);
	adv_dev = iio_context_find_device(ctx, AD_MC_ADV_CTRL);
	resolver_dev = iio_context_find_device(ctx, RESOLVER);

	nbook = GTK_NOTEBOOK(notebook);
	builder = gtk_builder_new();

	if (osc_load_glade_file(builder, "motor_control") < 0)
		return NULL;

	motor_control_panel = GTK_WIDGET(gtk_builder_get_object(builder, "tablePanelMotor_Control"));
	controllers_notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook_controllers"));

	pid_page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(controllers_notebook), 0);
	advanced_page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(controllers_notebook), 1);
	resolver_page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(controllers_notebook), 2);

	if (pid_devs[PID_1ST_DEV])
		pid_controller_init(builder, PID_1ST_DEV);
	else
		gtk_widget_hide(pid_page);
	if (pid_devs[PID_2ND_DEV])
		pid_controller_init(builder, PID_2ND_DEV);
	if (adv_dev)
		advanced_controller_init(builder);
	else
		gtk_widget_hide(advanced_page);

	if (resolver_dev)
		resolver_init(builder);
	else
		gtk_widget_hide(resolver_page);

	if (ini_fn)
		load_profile(NULL, ini_fn);

	/* Update all widgets with current values */
	tx_update_values();
	if (resolver_dev)
		resolver_resolution_update_val(builder);

	/* Connect signals. */

	/* Signal connections for GPOs */
	char widget_name[25];
	for (i = 0; i < sizeof(gpo)/sizeof(gpo[0]); i++) {
		sprintf(widget_name, "checkbutton_gpo%zu", i+1);
		gpo_id[i] = i;
		gpo[i] = GTK_WIDGET(gtk_builder_get_object(builder, widget_name));
		g_signal_connect(G_OBJECT(gpo[i]), "toggled", G_CALLBACK(gpo_toggled_cb), &gpo_id[i]);
	}

	/* Signal connections for the rest of the widgets */
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

	g_signal_connect(G_OBJECT(controllers_notebook), "switch-page",
		G_CALLBACK(controllers_notebook_page_switched_cb), NULL);

	tx_update_values();

	if (pid_devs[PID_1ST_DEV]) {
		/* Make sure  delta parameter is set to 1 */
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(delta[PID_1ST_DEV]), true);
	}

	if (adv_dev) {
		gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(controller_mode), 1);
		gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(controller_mode), 0);
	}

	gpo_mask = ONE_MOTOR_GPO_MASK;
	if (pid_devs[PID_2ND_DEV]) {
		gpo_mask = TWO_MOTOR_GPO_MASK;
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(builder,
			"frame_motor2")));
		gtk_widget_show(GTK_WIDGET(gtk_builder_get_object(builder,
			"label_frame_motor1")));

		for (i = 4; i < 11; i++)
			gtk_widget_hide(gpo[i]);
		gtk_widget_set_sensitive(
			controller_type_pid[PID_1ST_DEV], false);
		gtk_widget_set_sensitive(
			controller_type_pid[PID_2ND_DEV], false);
	}

	gint p;
	p = gtk_notebook_get_current_page(GTK_NOTEBOOK(controllers_notebook));
	controllers_notebook_page_switched_cb(GTK_NOTEBOOK(controllers_notebook),
		gtk_notebook_get_nth_page(GTK_NOTEBOOK(controllers_notebook), p), p, NULL);

	can_update_widgets = true;

	return motor_control_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void save_widgets_to_ini(FILE *f)
{
	fprintf(f, "pwm = %s\n"
			"gpo.1 = %i\n"
			"gpo.2 = %i\n"
			"gpo.3 = %i\n"
			"gpo.4 = %i\n"
			"gpo.5 = %i\n"
			"gpo.6 = %i\n"
			"gpo.7 = %i\n"
			"gpo.8 = %i\n"
			"gpo.9 = %i\n"
			"gpo.10 = %i\n"
			"gpo.11 = %i\n",
			(char *)gtk_entry_get_text(GTK_ENTRY(pwm_pid[PID_1ST_DEV])),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gpo[0])),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gpo[1])),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gpo[2])),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gpo[3])),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gpo[4])),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gpo[5])),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gpo[6])),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gpo[7])),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gpo[8])),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gpo[9])),
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gpo[10])));
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		if (pid_devs[PID_1ST_DEV]) {
			save_to_ini(f, THIS_DRIVER, pid_devs[PID_1ST_DEV],
				motor_control_sr_attribs,
				ARRAY_SIZE(motor_control_sr_attribs));
		}
		if (pid_devs[PID_2ND_DEV]) {
			save_to_ini(f, THIS_DRIVER, pid_devs[PID_2ND_DEV],
				motor_control_sr_attribs,
				ARRAY_SIZE(motor_control_sr_attribs));
		}
		save_widgets_to_ini(f);
		fclose(f);
	}
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	g_source_remove_by_user_data(ctx);

	if (ini_fn)
		save_profile(NULL, ini_fn);

	osc_destroy_context(ctx);
}

static bool motor_control_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();
	return !!iio_context_find_device(osc_ctx, AD_MC_CTRL) ||
		!!iio_context_find_device(osc_ctx, AD_MC_ADV_CTRL);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = motor_control_identify,
	.init = motor_control_init,
	.handle_item = motor_control_handle,
	.update_active_page = update_active_page,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
