/**
 * Copyright (C) 2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <glib.h>
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
#include <values.h>
#include <sys/stat.h>
#include <string.h>

#include <iio.h>

#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "../ini/ini.h"
#include "dac_data_manager.h"

static const gdouble mhz_scale = 1000000.0;
static const gdouble khz_scale = 1000.0;

static struct dac_data_manager *dac_tx_manager;

static struct iio_context *ctx;
static struct iio_device *dac, *adc;

static GtkWidget *rf_out;

static GtkWidget *dds1_freq, *dds2_freq, *dds3_freq, *dds4_freq;
static GtkWidget *dds1_scale, *dds2_scale, *dds3_scale, *dds4_scale;
static GtkWidget *dds1_phase, *dds2_phase, *dds3_phase, *dds4_phase;

static GtkWidget *dac_interpolation;
static GtkWidget *dac_shift;

static GtkWidget *avg_I, *avg_Q;
static GtkWidget *span_I, *span_Q;
static GtkWidget *radius_IQ, *angle_IQ;

static struct iio_widget tx_widgets[100];
static struct iio_widget rx_widgets[100];
static struct iio_widget cal_widgets[100];
static unsigned int num_tx, num_rx, num_cal;

typedef struct _Dialogs Dialogs;
struct _Dialogs
{
	GtkWidget *calibrate;
	GtkWidget *filechooser;
};
static Dialogs dialogs;
static GtkWidget *cal_save, *cal_open, *cal_tx, *cal_rx;
static GtkWidget *I_dac_pha_adj, *I_dac_offs, *I_dac_fs_adj;
static GtkWidget *Q_dac_pha_adj, *Q_dac_offs, *Q_dac_fs_adj;
static GtkWidget *I_adc_offset_adj, *I_adc_gain_adj, *I_adc_phase_adj;
static GtkWidget *Q_adc_offset_adj, *Q_adc_gain_adj, *Q_adc_phase_adj;

static GtkWidget *ad9122_temp;

static void combo_box_set_active_text(GtkWidget *combobox, const char* text)
{
	GtkTreeModel *tree = gtk_combo_box_get_model(GTK_COMBO_BOX(combobox));
	gboolean valid;
	GtkTreeIter iter;
	gint i = 0;

	valid = gtk_tree_model_get_iter_first (tree, &iter);
	while (valid) {
		gchar *str_data;

		gtk_tree_model_get(tree, &iter, 0, &str_data, -1);
		if (!strcmp(str_data, text)) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(combobox), i);
			break;
		}

		i++;
		g_free (str_data);
		valid = gtk_tree_model_iter_next (tree, &iter);
	}
}

static void dds_scale_set_string_value(GtkWidget *scale, const char *value)
{
	if (GTK_IS_COMBO_BOX_TEXT(scale)) {
		combo_box_set_active_text(scale, value);
	} else if (GTK_IS_SPIN_BUTTON(scale)){
		gtk_entry_set_text(GTK_ENTRY(scale), value);
	}
}

static const char *dds_scale_get_string_value(GtkWidget *scale)
{
	if (GTK_IS_COMBO_BOX_TEXT(scale)) {
		return gtk_combo_box_get_active_text(GTK_COMBO_BOX(scale));
	} else if (GTK_IS_SPIN_BUTTON(scale)) {
		return gtk_entry_get_text(GTK_ENTRY(scale));
	}

	return NULL;
}

static int oneover(const gchar *num)
{
	float close;

	close = powf(2.0, roundf(log2f(1.0 / atof(num))));
	return (int)close;

}

static void rf_out_update(void)
{
	char buf[1024], dds1_m[16], dds2_m[16];
	static GtkTextBuffer *tbuf = NULL;
	GtkTextIter iter;

	if (tbuf == NULL) {
		tbuf = gtk_text_buffer_new(NULL);
		gtk_text_view_set_buffer(GTK_TEXT_VIEW(rf_out), tbuf);
	}

	memset(buf, 0, 1024);

	sprintf(buf, "\n");
	gtk_text_buffer_set_text(tbuf, buf, -1);
	gtk_text_buffer_get_iter_at_line(tbuf, &iter, 1);
	sprintf(dds1_m, "1/%i", oneover(dds_scale_get_string_value(dds1_scale)));
	sprintf(dds2_m, "1/%i", oneover(dds_scale_get_string_value(dds2_scale)));
}

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
	rf_out_update();
}

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

static void cal_update_values(void)
{
	iio_update_widgets(cal_widgets, num_cal);
}

static int compare_gain(const char *a, const char *b) __attribute__((unused));
static int compare_gain(const char *a, const char *b)
{
	double val_a, val_b;
	sscanf(a, "%lf", &val_a);
	sscanf(b, "%lf", &val_b);

	if (val_a < val_b)
		return -1;
	else if(val_a > val_b)
		return 1;
	else
		return 0;
}

#if 0
static void display_temp(void *ptr)
{
	double temp;
	int tmp;
	char buf[25];

	while (!kill_thread) {
		if (set_dev_paths("axi-ad9144-hpc") < 0) {
			kill_thread = 1;
			break;
		}

		if (read_devattr_double("in_temp0_input", &temp) < 0) {
			/* Just assume it's 25C, units are in milli-degrees C */
			temp = 25 * 1000;
			write_devattr_double("in_temp0_input", temp);
			read_devattr_int("in_temp0_calibbias", &tmp);
			/* This will eventually be stored in the EEPROM */
			temp_calibbias = tmp;
			printf("AD9122 temp cal value : %i\n", tmp);
		} else {

			sprintf(buf, "%2.1f", temp/1000);
			gdk_threads_enter();
			gtk_label_set_text(GTK_LABEL(ad9122_temp), buf);
			gdk_threads_leave();
		}
		usleep(500000);
	}
}
#endif

#define MATCH_SECT(s) (strcmp(section, s) == 0)
#define MATCH_NAME(n) (strcmp(name, n) == 0)
#define RX_F   "Rx_Frequency"
#define TX_F   "Tx_Frequency"
#define DDS1_F "DDS1_Frequency"
#define DDS1_S "DDS1_Scale"
#define DDS1_P "DDS1_Phase"
#define DDS2_F "DDS2_Frequency"
#define DDS2_S "DDS2_Scale"
#define DDS2_P "DDS2_Phase"
#define DDS3_F "DDS3_Frequency"
#define DDS3_S "DDS3_Scale"
#define DDS3_P "DDS3_Phase"
#define DDS4_F "DDS4_Frequency"
#define DDS4_S "DDS4_Scale"
#define DDS4_P "DDS4_Phase"

#define DAC_I_P "I_pha_adj"
#define DAC_I_O "I_dac_offs"
#define DAC_I_G "I_fs_adj"
#define DAC_Q_P "Q_pha_adj"
#define DAC_Q_O "Q_dac_offs"
#define DAC_Q_G "Q_fs_adj"

#define ADC_I_O "I_adc_offset_adj"
#define ADC_I_G "I_adc_gain_adj"
#define ADC_I_P "I_adc_phase_adj"
#define ADC_Q_O "Q_adc_offset_adj"
#define ADC_Q_G "Q_adc_gain_adj"
#define ADC_Q_P "Q_adc_phase_adj"

#if 0
static void dac_cal_spin_helper(GtkRange *range,
		struct iio_channel *chn, const char *attr)
{
	gdouble inc, val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(range));
	gtk_spin_button_get_increments(GTK_SPIN_BUTTON(range), &inc, NULL);

	if (inc == 1.0)
		iio_channel_attr_write_longlong(chn, attr, (long long) val);
	else
		iio_channel_attr_write_double(chn, attr, val);
}

static void dac_cal_spin0(GtkRange *range, gpointer user_data)
{
	dac_cal_spin_helper(range,
			iio_device_find_channel(dac, "voltage0", true),
			(const char *) user_data);
}

static void dac_cal_spin1(GtkRange *range, gpointer user_data)
{
	dac_cal_spin_helper(range,
			iio_device_find_channel(dac, "voltage1", true),
			(const char *) user_data);
}

static void adc_cal_spin_helper(GtkRange *range,
		struct iio_channel *chn, const char *attr)
{
	gdouble val, inc;

	val = gtk_spin_button_get_value(GTK_SPIN_BUTTON(range));
	gtk_spin_button_get_increments(GTK_SPIN_BUTTON(range), &inc, NULL);

	if (inc == 1.0)
		iio_channel_attr_write_longlong(chn, attr, (long long) val);
	else
		iio_channel_attr_write_double(chn, attr, val);
}

static void adc_cal_spin0(GtkRange *range, gpointer user_data)
{
	adc_cal_spin_helper(range,
			iio_device_find_channel(adc, "voltage0", false),
			(const char *) user_data);
}

static void adc_cal_spin1(GtkRange *range, gpointer user_data)
{
	adc_cal_spin_helper(range,
			iio_device_find_channel(adc, "voltage1", false),
			(const char *) user_data);
}
#endif

static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
{
	iio_w->save(iio_w);
}

static void make_widget_update_signal_based(struct iio_widget *widgets,
	unsigned int num_widgets)
{
	char signal_name[25];
	unsigned int i;

	for (i = 0; i < num_widgets; i++) {
		if (GTK_IS_CHECK_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_TOGGLE_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_SPIN_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "value-changed");
		else if (GTK_IS_COMBO_BOX_TEXT(widgets[i].widget))
			sprintf(signal_name, "%s", "changed");
		else
			printf("unhandled widget type, attribute: %s\n", widgets[i].attr_name);

		if (GTK_IS_SPIN_BUTTON(widgets[i].widget) &&
			widgets[i].priv_progress != NULL) {
				iio_spin_button_progress_activate(&widgets[i]);
		} else {
			g_signal_connect(G_OBJECT(widgets[i].widget), signal_name, G_CALLBACK(save_widget_value), &widgets[i]);
		}
	}
}

static int daq2_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *daq2_panel;
	GtkWidget *dds_container;
	GtkTextBuffer *adc_buff, *dac_buff;
	struct iio_channel *ch0;

	dac_tx_manager = dac_data_manager_new(dac, NULL, ctx);
	if (!dac_tx_manager)
		return -1;

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "daq2.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "daq2.glade", NULL);


	daq2_panel = GTK_WIDGET(gtk_builder_get_object(builder, "daq2_panel"));


	avg_I = GTK_WIDGET(gtk_builder_get_object(builder, "avg_I"));
	avg_Q = GTK_WIDGET(gtk_builder_get_object(builder, "avg_Q"));
	span_I = GTK_WIDGET(gtk_builder_get_object(builder, "span_I"));
	span_Q = GTK_WIDGET(gtk_builder_get_object(builder, "span_Q"));
	radius_IQ = GTK_WIDGET(gtk_builder_get_object(builder, "radius_IQ"));
	angle_IQ = GTK_WIDGET(gtk_builder_get_object(builder, "angle_IQ"));

	dds_container = GTK_WIDGET(gtk_builder_get_object(builder, "dds_transmit_block"));
	gtk_container_add(GTK_CONTAINER(dds_container), dac_data_manager_get_gui_container(dac_tx_manager));
	gtk_widget_show_all(dds_container);

	dialogs.calibrate =  GTK_WIDGET(gtk_builder_get_object(builder, "cal_dialog"));
	dialogs.filechooser = GTK_WIDGET(gtk_builder_get_object(builder, "filechooser"));

	cal_save = GTK_WIDGET(gtk_builder_get_object(builder, "Save"));
	cal_open = GTK_WIDGET(gtk_builder_get_object(builder, "Open"));
	cal_rx = GTK_WIDGET(gtk_builder_get_object(builder, "Cal_Rx"));
	cal_tx = GTK_WIDGET(gtk_builder_get_object(builder, "Cal_Tx"));

	I_dac_pha_adj = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibphase0"));
	I_dac_offs = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibbias0"));
	I_dac_fs_adj = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibscale0"));

	Q_dac_pha_adj = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibphase1"));
	Q_dac_offs = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibbias1"));
	Q_dac_fs_adj = GTK_WIDGET(gtk_builder_get_object(builder, "dac_calibscale1"));

	I_adc_offset_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibbias0"));
	I_adc_gain_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibscale0"));
	I_adc_phase_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibphase0"));

	Q_adc_offset_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibbias1"));
	Q_adc_gain_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibscale1"));
	Q_adc_phase_adj = GTK_WIDGET(gtk_builder_get_object(builder, "adc_calibphase1"));

	ad9122_temp = GTK_WIDGET(gtk_builder_get_object(builder, "dac_temp"));

	rf_out =  GTK_WIDGET(gtk_builder_get_object(builder, "RF_out"));
	dac_interpolation = GTK_WIDGET(gtk_builder_get_object(builder, "dac_interpolation_clock"));
	dac_shift = GTK_WIDGET(gtk_builder_get_object(builder, "dac_fcenter_shift"));

	dds1_freq = dac_data_manager_get_widget(dac_tx_manager, TX1_T1_I, WIDGET_FREQUENCY);
	dds2_freq = dac_data_manager_get_widget(dac_tx_manager, TX1_T2_I, WIDGET_FREQUENCY);
	dds3_freq = dac_data_manager_get_widget(dac_tx_manager, TX1_T1_Q, WIDGET_FREQUENCY);
	dds4_freq = dac_data_manager_get_widget(dac_tx_manager, TX1_T2_Q, WIDGET_FREQUENCY);

	dds1_scale = dac_data_manager_get_widget(dac_tx_manager, TX1_T1_I, WIDGET_SCALE);
	dds2_scale = dac_data_manager_get_widget(dac_tx_manager, TX1_T2_I, WIDGET_SCALE);
	dds3_scale = dac_data_manager_get_widget(dac_tx_manager, TX1_T1_Q, WIDGET_SCALE);
	dds4_scale = dac_data_manager_get_widget(dac_tx_manager, TX1_T2_Q, WIDGET_SCALE);

	dds1_phase = dac_data_manager_get_widget(dac_tx_manager, TX1_T1_I, WIDGET_PHASE);
	dds2_phase = dac_data_manager_get_widget(dac_tx_manager, TX1_T2_I, WIDGET_PHASE);
	dds3_phase = dac_data_manager_get_widget(dac_tx_manager, TX1_T1_Q, WIDGET_PHASE);
	dds4_phase = dac_data_manager_get_widget(dac_tx_manager, TX1_T2_Q, WIDGET_PHASE);

	/* Bind the IIO device files to the GUI widgets */

	char attr_val[256];
	long long val;

	/* Rx Widgets */

	ch0 = iio_device_find_channel(adc, "voltage0", false);

	if (iio_channel_attr_read_longlong(ch0, "sampling_frequency", &val) == 0)
		snprintf(attr_val, sizeof(attr_val), "%.2f", (double)(val / 1000000ul));
	else
		snprintf(attr_val, sizeof(attr_val), "%s", "error");

	adc_buff = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(adc_buff, attr_val, -1);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(builder, "text_view_adc_freq")), adc_buff);

	/* Tx Widgets */
	ch0 = iio_device_find_channel(dac, "altvoltage0", true);

	if (iio_channel_attr_read_longlong(ch0, "sampling_frequency", &val) == 0)
		snprintf(attr_val, sizeof(attr_val), "%.2f", (double)(val / 1000000ul));
	else
		snprintf(attr_val, sizeof(attr_val), "%s", "error");

	dac_buff = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(dac_buff, attr_val, -1);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(gtk_builder_get_object(builder, "text_view_dac_freq")), dac_buff);

	g_signal_connect_after(dds1_scale, "change-value", G_CALLBACK(rf_out_update), NULL);
	g_signal_connect_after(dds2_scale, "change-value", G_CALLBACK(rf_out_update), NULL);
	g_signal_connect_after(dds3_scale, "change-value", G_CALLBACK(rf_out_update), NULL);
	g_signal_connect_after(dds4_scale, "change-value", G_CALLBACK(rf_out_update), NULL);

	make_widget_update_signal_based(rx_widgets, num_rx);
	make_widget_update_signal_based(tx_widgets, num_tx);

	double rate;

	rate = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gtk_builder_get_object(builder, "dac_data_clock")));
	dac_data_manager_freq_widgets_range_update(dac_tx_manager, rate);

	tx_update_values();
	rx_update_values();
	cal_update_values();
	dac_data_manager_update_iio_widgets(dac_tx_manager);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), daq2_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), daq2_panel, "DAQ2");
	dac_data_manager_set_buffer_chooser_current_folder(dac_tx_manager, OSC_WAVEFORM_FILE_PATH);

	return 0;
}

#define SYNC_RELOAD "SYNC_RELOAD"

static char *handle_item(struct osc_plugin *plugin, const char *attrib,
			 const char *value)
{
	char *buf;
	bool state;

	if (MATCH_ATTRIB(SYNC_RELOAD)) {
		if (value) {
			tx_update_values();
			rx_update_values();
		} else {
			return "1";
		}
	} else if (MATCH_ATTRIB("dds_mode")) {
		if (value) {
			dac_data_manager_set_dds_mode(dac_tx_manager, "axi-ad9144-hpc", 1, atoi(value));
		} else {
			buf = malloc (10);
			sprintf(buf, "%i", dac_data_manager_get_dds_mode(dac_tx_manager, "axi-ad9144-hpc", 1));
			return buf;
		}
	} else if (MATCH_ATTRIB("dac_buf_filename") &&
				dac_data_manager_get_dds_mode(dac_tx_manager, "axi-ad9144-hpc", 1) == DDS_BUFFER) {
		if (value) {
			dac_data_manager_set_buffer_chooser_filename(dac_tx_manager, value);
		} else {
			return dac_data_manager_get_buffer_chooser_filename(dac_tx_manager);
		}
	} else if (MATCH_ATTRIB("tx_channel_0")) {
		if (value) {
			state = (atoi(value)) ? true : false;
			dac_data_manager_set_tx_channel_state(dac_tx_manager, 0, state);
		} else {
			buf = malloc (10);
			sprintf(buf, "%i", dac_data_manager_get_tx_channel_state(dac_tx_manager, 0));
			return buf;
		}
	} else if (MATCH_ATTRIB("tx_channel_1")) {
		if (value) {
			state = (atoi(value)) ? true : false;
			dac_data_manager_set_tx_channel_state(dac_tx_manager, 1, state);
		} else {
			buf = malloc (10);
			sprintf(buf, "%i", dac_data_manager_get_tx_channel_state(dac_tx_manager, 1));
			return buf;
		}
	} else {
		if (value) {
			printf("Unhandled tokens in ini file,\n"
					"\tSection %s\n\tAtttribute : %s\n\tValue: %s\n",
					"DAQ2", attrib, value);
			return "FAIL";
		}
	}

	return NULL;
}

static void context_destroy(void)
{
	if (dac_tx_manager) {
		dac_data_manager_free(dac_tx_manager);
		dac_tx_manager = NULL;
	}
	iio_context_destroy(ctx);
}

static const char *daq2_sr_attribs[] = {
	"axi-ad9680-hpc.in_voltage_sampling_frequency",
	"axi-ad9144-hpc.out_altvoltage_sampling_frequency",
	"dds_mode",
	"dac_buf_filename",
	"tx_channel_0",
	"tx_channel_1",
	"axi-ad9144-hpc.out_altvoltage0_1A_frequency",
	"axi-ad9144-hpc.out_altvoltage2_2A_frequency",
	"axi-ad9144-hpc.out_altvoltage1_1B_frequency",
	"axi-ad9144-hpc.out_altvoltage3_2B_frequency",
	"axi-ad9144-hpc.out_altvoltage0_1A_scale",
	"axi-ad9144-hpc.out_altvoltage2_2A_scale",
	"axi-ad9144-hpc.out_altvoltage1_1B_scale",
	"axi-ad9144-hpc.out_altvoltage3_2B_scale",
	"axi-ad9144-hpc.out_altvoltage0_1A_phase",
	"axi-ad9144-hpc.out_altvoltage1_1B_phase",
	"axi-ad9144-hpc.out_altvoltage2_2A_phase",
	"axi-ad9144-hpc.out_altvoltage3_2B_phase",
	SYNC_RELOAD,
	NULL,
};

static bool daq2_identify(void)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();
	if (!iio_context_find_device(osc_ctx, "axi-ad9144-hpc")
		|| !iio_context_find_device(osc_ctx, "axi-ad9680-hpc"))
		return false;

	ctx = osc_create_context();
	dac = iio_context_find_device(ctx, "axi-ad9144-hpc");
	adc = iio_context_find_device(ctx, "axi-ad9680-hpc");
	if (!dac || !adc)
		iio_context_destroy(ctx);
	return !!dac && !!adc;
}

struct osc_plugin plugin = {
	.name = "DAQ2",
	.identify = daq2_identify,
	.init = daq2_init,
	.save_restore_attribs = daq2_sr_attribs,
	.handle_item = handle_item,
	.destroy = context_destroy,
};
