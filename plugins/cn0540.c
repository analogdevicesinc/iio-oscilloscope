/**
 * Copyright (C) 2019 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <math.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../osc_plugin.h"
#include "../iio_widget.h"
#include "../config.h"
#include "./block_diagram.h"

#define THIS_DRIVER			"CN0540"
#define ADC_DEVICE			"ad7768-1"
#define DAC_DEVICE			"ltc2606"
#define GPIO_CTRL			"one-bit-adc-dac"
#define VOLTAGE_MONITOR_1		"xadc"
#define VOLTAGE_MONITOR_2		"ltc2308"
#define ADC_DEVICE_CH			"voltage0"
#define DAC_DEVICE_CH			"voltage0"

#define G				0.3
#define FDA_VOCM_MV			2500
#define FDA_GAIN			2.667
#define DAC_BUF_GAIN			1.22
#define CALIB_MAX_ITER		  	20
#define XADC_VREF			3.3
#define XADC_RES			12
#define MAX_NUM_GPIOS			64
#define NUM_ANALOG_PINS		 	6

struct iio_gpio {
	struct iio_channel *gpio;
	char label[30];
};

struct iio_device *iio_adc;
struct iio_device *iio_dac;
struct iio_device *iio_gpio;
struct iio_device *iio_voltage_mon;
static struct iio_context *ctx;
static struct iio_channel *adc_ch;
static struct iio_channel *dac_ch;
static struct iio_channel *analog_in[NUM_ANALOG_PINS];
static struct iio_gpio gpio_ch[MAX_NUM_GPIOS];
static struct iio_widget iio_widgets[25];
static unsigned int num_widgets;

static GtkWidget *cn0540_panel;
static GtkCheckButton *tgbtn_shutdown;
static GtkCheckButton *tgbtn_fda;
static GtkCheckButton *tgbtn_fda_mode;
static GtkCheckButton *tgbtn_cc;
static GtkButton *btn_get_sw_ff;
static GtkButton *calib_btn;
static GtkButton *write_btn;
static GtkButton *read_btn;
static GtkButton *readvsensor_btn;
static GtkTextView *sw_ff_status;
static GtkTextView *shutdown_status;
static GtkTextView *fda_status;
static GtkTextView *fda_mode_status;
static GtkTextView *voltage_status[6];
static GtkTextView *vshift_log;
static GtkTextView *vsensor_log;
static GtkTextView *calib_status;
static GtkTextView *cc_status;
static GtkTextBuffer *sw_ff_buffer;
static GtkTextBuffer *shutdown_buffer;
static GtkTextBuffer *fda_buffer;
static GtkTextBuffer *fda_mode_buffer;
static GtkTextBuffer *voltage_buffer[NUM_ANALOG_PINS];
static GtkTextBuffer *calib_buffer;
static GtkTextBuffer *vsensor_buf;
static GtkTextBuffer *vshift_buf;
static GtkTextBuffer *cc_buf;

static gboolean plugin_detached;
static gint this_page;

struct osc_plugin plugin;

void delay_ms(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000 * 1000 };
	nanosleep(&ts, NULL);

}

static gboolean cn0540_get_gpio_state(const char* gpio_name)
{
	long long readback = -1;
	int idx;

	for(idx = 0; idx < MAX_NUM_GPIOS; idx++) {
		if(strstr(gpio_ch[idx].label, gpio_name)) {
			iio_channel_attr_read_longlong(gpio_ch[idx].gpio, "raw",
				&readback);
			break;
		}
	}

	if(readback == -1)
		printf("%s: wrong gpio name: %s\n",__func__, gpio_name);

	return (gboolean)readback;
}

static void cn0540_set_gpio_state(const char* gpio_name, gboolean state)
{
	int idx;

	for(idx = 0; idx < MAX_NUM_GPIOS; idx++) {
		if (strstr(gpio_ch[idx].label, gpio_name)) {
			iio_channel_attr_write_longlong(gpio_ch[idx].gpio,
				"raw", (long long)state);
			break;
		}
	}
}

static void monitor_shutdown(GtkCheckButton *btn)
{
	struct extra_dev_info *info;

	/* If the buffer is enabled */
	if (iio_channel_is_enabled(adc_ch)) {
		info = iio_device_get_data(iio_adc);
		if (info->buffer) {
			iio_buffer_destroy(info->buffer);
			info->buffer = NULL;
		}

		iio_channel_disable(adc_ch);
	}
	/* Shutdown pin is tied to active-low inputs */
	cn0540_set_gpio_state("cn0540_shutdown_gpio",
		!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tgbtn_shutdown)));
	gtk_text_buffer_set_text(shutdown_buffer, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))?
		"ENABLED" : "DISABLED", -1);
	/* Enable back the channel */
	iio_channel_enable(adc_ch);
}

static void monitor_sw_ff(GtkButton *btn)
{
	gboolean state;

	state = cn0540_get_gpio_state("cn0540_sw_ff_gpio");
	gtk_text_buffer_set_text(sw_ff_buffer, state ? "HIGH" : "LOW", -1);
}

static void monitor_fda(GtkCheckButton *btn)
{
	struct extra_dev_info *info;

	/* If the buffer is enabled */
	if (iio_channel_is_enabled(adc_ch)) {
		info = iio_device_get_data(iio_adc);
		if (info->buffer) {
			iio_buffer_destroy(info->buffer);
			info->buffer = NULL;
		}

		iio_channel_disable(adc_ch);
	}
	/* FDA_DIS pin is tied to active-low inputs */
	cn0540_set_gpio_state("cn0540_FDA_DIS",!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn)));
	gtk_text_buffer_set_text(fda_buffer, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn)) ?
		"ENABLED" : "DISABLED", -1);
	/* Enable back the channel */
	iio_channel_enable(adc_ch);
}

static void monitor_cc(GtkCheckButton *btn)
{
	cn0540_set_gpio_state("cn0540_blue_led",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn)));
	gtk_text_buffer_set_text(cc_buf, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn)) ?
		"ENABLED" : "DISABLED", -1);
}

static void monitor_fda_mode(GtkCheckButton *btn)
{
	struct extra_dev_info *info;

	/* If the buffer is enabled */
	if (iio_channel_is_enabled(adc_ch)) {
		info = iio_device_get_data(iio_adc);
		if (info->buffer) {
			iio_buffer_destroy(info->buffer);
			info->buffer = NULL;
		}

		iio_channel_disable(adc_ch);
	}
	cn0540_set_gpio_state("cn0540_FDA_MODE",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn)));
	gtk_text_buffer_set_text(fda_mode_buffer, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn)) ?
		"FULL POWER" : "LOW POWER", -1);
	/* Enable back the channel */
	iio_channel_enable(adc_ch);
}

static gboolean update_voltages(struct iio_device *voltage_mon)
{
	double scale, result;
	char voltage[10];
	long long raw;
	int idx;

	for(idx = 0; idx < NUM_ANALOG_PINS; idx++) {
		iio_channel_attr_read_longlong(analog_in[idx], "raw", &raw);
		iio_channel_attr_read_double(analog_in[idx], "scale", &scale);
		result = raw * scale;
		if(!strcmp(iio_device_get_name(voltage_mon),VOLTAGE_MONITOR_1))
			result *= XADC_VREF;
		snprintf(voltage, sizeof(voltage), "%.2f", result);
		gtk_text_buffer_set_text(voltage_buffer[idx], voltage, -1);
	}

	return TRUE;
}

static double get_voltage(struct iio_channel *ch)
{
	double scale;
	long long raw;

	iio_channel_attr_read_longlong(ch,"raw",&raw);
	iio_channel_attr_read_double(ch,"scale",&scale);
	return raw * scale;
}

static void set_voltage(struct iio_channel *ch, double voltage_mv)
{
	double scale;

	iio_channel_attr_read_double(ch,"scale",&scale);
	iio_channel_attr_write_longlong(ch,"raw",
					(long long)(voltage_mv / scale));
}

static double get_vshift_mv()
{
	return get_voltage(dac_ch) * DAC_BUF_GAIN;
}

static void read_vshift(GtkButton *btn)
{
	char buff_string[9];
	double vshift_mv;

	vshift_mv = get_vshift_mv();

	snprintf(buff_string, sizeof(buff_string), "%f", vshift_mv);
	gtk_text_buffer_set_text(vshift_buf, buff_string, -1);
}

static void write_vshift(GtkButton *btn)
{
	static GtkTextIter start, end;
	gchar *vshift_string;
	double vshift_mv;

	gtk_text_buffer_get_start_iter(vshift_buf,&start);
	gtk_text_buffer_get_end_iter(vshift_buf,&end);
	vshift_string = gtk_text_buffer_get_text(vshift_buf,&start,&end, -1);
	vshift_mv = atof(vshift_string);
	set_voltage(dac_ch, (double)(vshift_mv / DAC_BUF_GAIN));

	g_free(vshift_string);
	fflush(stdout);
}

static void read_vsensor(GtkButton *btn)
{

	char buff_string[9];
	double vsensor_mv, vshift_mv,vadc_mv;

	vadc_mv = get_voltage(adc_ch);
	vshift_mv = get_vshift_mv();
	double v1_st = FDA_VOCM_MV - vadc_mv/FDA_GAIN;
	vsensor_mv = (((G + 1) *vshift_mv ) - v1_st )/G;
	vsensor_mv -= get_voltage(adc_ch);

	snprintf(buff_string, sizeof(buff_string), "%f", vsensor_mv);
	gtk_text_buffer_set_text(vsensor_buf, buff_string, -1);
}

static void calib(GtkButton *btn)
{
	double adc_voltage_mv, dac_voltage_mv;
	char buff_string[9];
	int i;

	gtk_text_buffer_set_text(calib_buffer, "Calibrating...", -1);

	for(i = 0; i < CALIB_MAX_ITER; i ++)
	{
		adc_voltage_mv = get_voltage(adc_ch);
		dac_voltage_mv = get_voltage(dac_ch) - adc_voltage_mv;
		set_voltage(dac_ch, dac_voltage_mv);
		delay_ms(10);
	}
	snprintf(buff_string, sizeof(buff_string), "%f", adc_voltage_mv);
	gtk_text_buffer_set_text(calib_buffer, buff_string, -1);
	gtk_button_clicked(read_btn);
	gtk_button_clicked(readvsensor_btn);
}

 static void save_widget_value(GtkWidget *widget, struct iio_widget *iio_w)
 {
	iio_w->save(iio_w);
	/* refresh widgets so that, we know if our value was updated */
	iio_update_widgets(iio_widgets, num_widgets);
 }

 static void make_widget_update_signal_based(struct iio_widget *widgets,
						unsigned int num_widgets)
 {
	char signal_name[25];
	unsigned int i;

	for (i = 0; i < num_widgets; i++) {
		if (GTK_IS_SPIN_BUTTON(widgets[i].widget) &&
				widgets[i].priv_progress != NULL) {
			iio_spin_button_progress_activate(&widgets[i]);
		} else {
			g_signal_connect(G_OBJECT(widgets[i].widget),
					 signal_name,
					 G_CALLBACK(save_widget_value),
					 &widgets[i]);
		}
	}
 }

static void cn0540_get_channels()
{
	gboolean direction = TRUE;
	int idx = -1;
	char label[10] = "voltage0";

	adc_ch = iio_device_find_channel(iio_adc, ADC_DEVICE_CH, FALSE);
	dac_ch = iio_device_find_channel(iio_dac, DAC_DEVICE_CH, TRUE);

	while(1) {
		if (idx == MAX_NUM_GPIOS) {
			fprintf(stderr, "CN0540 plugin supports 64 GPIOs max\n");
			break;
		}

		gpio_ch[++idx].gpio = iio_device_find_channel(iio_gpio, label,
								  direction);
		if (gpio_ch[idx].gpio != NULL){
			iio_channel_attr_read(gpio_ch[idx].gpio, "label",
						  gpio_ch[idx].label, 30);
			label[7]++;
		} else if (direction && (gpio_ch[idx].gpio == NULL)) {
			direction = !direction;
			label[7] = '0';
			idx--;
		} else {
			break;
		}
	}

	if(iio_voltage_mon != NULL) {
		if(!strcmp(iio_device_get_name(iio_voltage_mon),
			   VOLTAGE_MONITOR_1)) {
			label[7] = '9';
			label[8] = 0;
		} else {
			label[7] = '0';
			label[8] = 0;
		}
		for(idx = 0; idx < NUM_ANALOG_PINS; idx++) {
			analog_in[idx] = iio_device_find_channel(
						iio_voltage_mon, label, FALSE);
			label[strlen(label) - 1]++;
			if (label[7] == ':') {
				label[7] = '1';
				label[8] = '0';
				label[9] = 0;
			}
		}
	}
}

static void cn0540_plugin_interface_init(GtkBuilder *builder)
{
	cn0540_panel = GTK_WIDGET(gtk_builder_get_object(builder,
				"cn0540_panel"));
	tgbtn_shutdown = GTK_CHECK_BUTTON(gtk_builder_get_object(builder,
				"tgbtn_shutdown"));
	tgbtn_fda = GTK_CHECK_BUTTON(gtk_builder_get_object(builder,
				"tgbtn_fda"));
	tgbtn_fda_mode = GTK_CHECK_BUTTON(gtk_builder_get_object(builder,
				"tgbtn_fda_mode"));
	tgbtn_cc = GTK_CHECK_BUTTON(gtk_builder_get_object(builder,
				"tgbtn_cc"));
	btn_get_sw_ff = GTK_BUTTON(gtk_builder_get_object(builder,
				"btn_get_sw_ff"));
	sw_ff_status = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"sw_ff_status"));
	shutdown_status = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"shutdown_status"));
	fda_status = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"fda_status"));
	fda_mode_status = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"fda_mode_status "));
	cc_status = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"cc_status "));
	voltage_status[0] = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"voltage_0_status"));
	voltage_status[1] = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"voltage_1_status"));
	voltage_status[2] = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"voltage_2_status"));
	voltage_status[3] = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"voltage_3_status"));
	voltage_status[4] = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"voltage_4_status"));
	voltage_status[5] = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"voltage_5_status"));
	sw_ff_buffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"sw_ff_buffer"));
	shutdown_buffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"shutdown_buffer"));
	fda_buffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"fda_buffer"));
	fda_mode_buffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"fda_mode_buffer"));
	voltage_buffer[0] = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"voltage_0_buffer"));
	voltage_buffer[1] = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"voltage_1_buffer"));
	voltage_buffer[2] = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"voltage_2_buffer"));
	voltage_buffer[3] = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"voltage_3_buffer"));
	voltage_buffer[4] = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"voltage_4_buffer"));
	voltage_buffer[5] = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"voltage_5_buffer"));
	calib_btn = GTK_BUTTON(gtk_builder_get_object(builder,
				"calib_btn"));
	read_btn = GTK_BUTTON(gtk_builder_get_object(builder,
				"read_btn"));
	write_btn = GTK_BUTTON(gtk_builder_get_object(builder,
				"write_btn"));
	readvsensor_btn = GTK_BUTTON(gtk_builder_get_object(builder,
				"readvsensor_btn"));
	calib_status = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"calib_status"));
	calib_buffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"calib_buffer"));
	vshift_log = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"vshift_log"));
	vshift_buf = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"vshift_buf"));
	vsensor_log = GTK_TEXT_VIEW(gtk_builder_get_object(builder,
				"vsensor_log"));
	vsensor_buf = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"vsensor_buf"));
	cc_buf = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,
				"cc_buffer"));

	make_widget_update_signal_based(iio_widgets, num_widgets);

	g_signal_connect(G_OBJECT(&tgbtn_shutdown->toggle_button), "toggled",
		G_CALLBACK(monitor_shutdown), NULL);
	g_signal_connect(G_OBJECT(&tgbtn_fda->toggle_button), "toggled",
		G_CALLBACK(monitor_fda), NULL);
	g_signal_connect(G_OBJECT(&tgbtn_fda_mode->toggle_button), "toggled",
		G_CALLBACK(monitor_fda_mode), NULL);
	g_signal_connect(G_OBJECT(&tgbtn_cc->toggle_button), "toggled",
		G_CALLBACK(monitor_cc), NULL);
	g_signal_connect(G_OBJECT(btn_get_sw_ff), "clicked",
		G_CALLBACK(monitor_sw_ff), &btn_get_sw_ff);
	g_signal_connect(G_OBJECT(calib_btn),"clicked",
		G_CALLBACK(calib),&calib_btn);
	g_signal_connect(G_OBJECT(read_btn),"clicked",
		G_CALLBACK(read_vshift),&read_btn);
	g_signal_connect(G_OBJECT(write_btn),"clicked",
		G_CALLBACK(write_vshift),&write_btn);
	g_signal_connect(G_OBJECT(readvsensor_btn),"clicked",
		G_CALLBACK(read_vsensor),&readvsensor_btn);

	iio_update_widgets(iio_widgets, num_widgets);
}

static void cn0540_init()
{
	int idx;

	gtk_toggle_button_toggled(&tgbtn_cc->toggle_button);
	gtk_toggle_button_toggled(&tgbtn_shutdown->toggle_button);
	gtk_toggle_button_toggled(&tgbtn_fda->toggle_button);
	gtk_toggle_button_toggled(&tgbtn_fda_mode->toggle_button);
	gtk_button_clicked(btn_get_sw_ff);
	gtk_button_clicked(calib_btn);

	if (iio_voltage_mon != NULL)
		g_timeout_add_seconds(1, (GSourceFunc)update_voltages,
				     iio_voltage_mon);
	else {
		for(idx = 0; idx < NUM_ANALOG_PINS; idx++) {
			gtk_text_buffer_set_text(voltage_buffer[idx],
						"INACTIVE", -1);
		}
	}
}

static GtkWidget *cn0540_plugin_init(struct osc_plugin *plugin,
				     GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;

	builder = gtk_builder_new();

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	if (osc_load_glade_file(builder, "cn0540") < 0) {
		osc_destroy_context(ctx);
		return NULL;
	}

	block_diagram_init(builder, 1, "CN0540.jpg");

	cn0540_get_channels();
	cn0540_plugin_interface_init(builder);
	cn0540_init();

	return cn0540_panel;
}

static bool cn0540_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	/* Get the iio devices */
	iio_adc = iio_context_find_device(osc_ctx, ADC_DEVICE);
	iio_dac = iio_context_find_device(osc_ctx, DAC_DEVICE);
	iio_gpio = iio_context_find_device(osc_ctx, GPIO_CTRL);
	iio_voltage_mon = iio_context_find_device(osc_ctx, VOLTAGE_MONITOR_1);
	if (!iio_voltage_mon)
		iio_voltage_mon = iio_context_find_device(osc_ctx,
							  VOLTAGE_MONITOR_2);

	if (!iio_adc || !iio_dac || !iio_gpio) {
		printf("Could not find expected iio devices\n");
		return FALSE;
	}

	return TRUE;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	g_source_remove_by_user_data(iio_voltage_mon);
	osc_destroy_context(ctx);
}

static void cn0540_get_preferred_size(const struct osc_plugin *plugin,
					  int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page,
				   gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = cn0540_identify,
	.init = cn0540_plugin_init,
	.update_active_page = update_active_page,
	.get_preferred_size = cn0540_get_preferred_size,
	.destroy = context_destroy,
};
