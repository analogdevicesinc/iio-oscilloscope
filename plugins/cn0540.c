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
#include <gtk/gtk.h>
#include <glib.h>
#include <math.h>

#include "../osc.h"
#include "../osc_plugin.h"
#include "../iio_widget.h"

#define THIS_DRIVER	"CN0540"
#define ADC_DEVICE	"ad7768-1"
#define DAC_DEVICE	"ltc2606"
#define GPIO_CTRL	"one-bit-adc-dac"
/*
 * For now leave this define as it is but, in future it would be nice if
 * the scaling is done in the driver so that, the plug in does not have
 * to knwow about  DAC_MAX_AMPLITUDE.
 */

#define DAC_SCALE               0.0625
#define ADC_SCALE               0.000488
#define G                       0.3
#define G_FDA                   2.667
#define V_OCM                   2.5
#define DAC_GAIN                1.22
#define AVG_COUNT               5

#define DAC_DEFAULT_VAL         55616

static struct iio_context *ctx;
static struct iio_channel *adc_ch;
static struct iio_channel *dac_ch;
static struct iio_channel *sw_ff;
static struct iio_channel *sw_in;
static struct iio_widget iio_widgets[25];
static unsigned int num_widgets;

static GtkWidget *cn0540_panel;
static GtkToggleButton *swin_enable;
static GtkToggleButton *swff_enable;
static GtkToggleButton *fdadis_enable;
static GtkToggleButton *fdamode_pow;
static GtkWidget *calib_btn;
static GtkWidget *write_btn;
static GtkWidget *read_btn;
static GtkWidget *readvsensor_btn;
static GtkTextView *vshift_log;
static GtkTextView *vsensor_log;
static GtkTextView *calib_status;
static GtkTextBuffer *calib_buffer;
static GtkTextBuffer *vsensor_buf;
static GtkTextBuffer *vshift_buf;

static gboolean plugin_detached;
static gint this_page;

static void enable_fdadis(GtkButton *btn)
{
	printf("FDA_DIS callback\n");
	gtk_text_buffer_set_text(calib_buffer,"FDA_DIS callback",-1);
}
static void set_fdapow(GtkToggleButton *btn)
{
	printf("FDA_POW callback\n");
	fflush(stdout);
}
static void enable_swff(GtkToggleButton *btn)
{
	gboolean button_state;
	button_state = gtk_toggle_button_get_active(swff_enable);
	if(button_state)
		iio_channel_attr_write_longlong(sw_ff, "cn0540_sw_ff_gpio_raw", 1);
	else
		iio_channel_attr_write_longlong(sw_ff, "cn0540_sw_ff_gpio_raw", 0);
}
static void enable_swin(GtkButton *btn)
{
	gboolean button_state;
	button_state = gtk_toggle_button_get_active(swin_enable);
	if(button_state)
		iio_channel_attr_write_longlong(sw_in, "cn0540_shutdown_gpio_raw", 1);
	else
		iio_channel_attr_write_longlong(sw_in, "cn0540_shutdown_gpio_raw", 0);
}
static double vout_function(int32_t adc_code)
{        
	double vout = (adc_code * ADC_SCALE) / 1000.0;
	return vout;

}
static double compute_vshift_cal(double vsensor)
{
	double vshift = (V_OCM +(G*vsensor))/(G+1);
	return vshift;
}

static double dac_code_to_v(uint16_t code)
{
	double ret = ((code*DAC_SCALE)/1000.0) * DAC_GAIN;
	return ret;
}
static uint16_t v_to_dac_code(double v)
{
	uint16_t ret = (uint16_t)(((v*1000.0)/DAC_SCALE)/DAC_GAIN);
	return ret;

}

static double vout_1st_stage(double vout)
{
	double ret = V_OCM - vout/G_FDA;
	return ret;

}
static double vsensor_function(double vout1st, double vshift)
{
	double vsensor = (((G+1)*vshift) - vout1st)/G;
	return vsensor;
}
static double get_vsensor_from_code(int32_t adc_code, uint16_t dac_code)
{
	double vout = vout_function(adc_code);
	double v1st = vout_1st_stage(vout);
	double vsensor = vsensor_function(v1st, dac_code_to_v(dac_code));
	return vsensor;
}
static void read_vshift(GtkButton *btn)
{
	char vshift_string[20];
	long long val;
	iio_channel_attr_read_longlong(dac_ch,"raw",&val);
	double vshift = dac_code_to_v(val);
	snprintf(vshift_string, sizeof(vshift_string), "%f", vshift);
	gtk_text_buffer_set_text(vshift_buf,vshift_string, -1);
}
static void write_vshift(GtkButton *btn)
{
	gchar *vshift_string;
	long long val;
	static GtkTextIter start, end;

	gtk_text_buffer_get_start_iter(vshift_buf,&start);
	gtk_text_buffer_get_end_iter(vshift_buf,&end);
	vshift_string = gtk_text_buffer_get_text(vshift_buf,&start,&end, -1);
	val = v_to_dac_code(atof(vshift_string));
	printf("val %lld\n",val);
	iio_channel_attr_write_longlong(dac_ch,"raw",val);
	g_free(vshift_string);
	fflush(stdout);
}
static void read_vsensor(GtkButton *btn)
{
	char vsensor_string[20];
	long long adc_code;
	long long dac_code;

	iio_channel_attr_read_longlong(adc_ch,"raw",&adc_code);
	iio_channel_attr_read_longlong(adc_ch,"raw",&dac_code);
	double vsensor = get_vsensor_from_code(adc_code,dac_code);
	snprintf(vsensor_string,sizeof(vsensor_string),"%f",vsensor);
	gtk_text_buffer_set_text(vsensor_buf,vsensor_string,-1);
}
static void calib(GtkButton *btn)
{
	static GtkTextIter iter;
	long long val;
	int32_t sum=0;
	char vshift_string[20], vsensor_string[20];
	for(int i = 0; i < AVG_COUNT;i++)
	{
		iio_channel_attr_read_longlong(adc_ch,"raw",&val);
		sum = sum + val;
	}
	val = (int32_t)(sum/AVG_COUNT);
	double vsensor = get_vsensor_from_code(val, DAC_DEFAULT_VAL);
	double vshift_cal = compute_vshift_cal(vsensor);
	uint16_t dac_code = v_to_dac_code(vshift_cal);
	iio_channel_attr_write_longlong(dac_ch,"raw",dac_code);
	iio_channel_attr_read_longlong(adc_ch, "raw", &val);
	vsensor = get_vsensor_from_code(val, dac_code);
	snprintf(vshift_string, sizeof(vshift_string), "%f", vshift_cal);
	snprintf(vsensor_string, sizeof(vsensor_string), "%f", vsensor);

	gtk_text_buffer_set_text(calib_buffer, "V(sensor)[V] --> ", -1);
	gtk_text_buffer_get_end_iter(calib_buffer, &iter);
	gtk_text_buffer_insert(calib_buffer,&iter,vsensor_string,-1);

	gtk_text_buffer_get_end_iter(calib_buffer, &iter);
	gtk_text_buffer_insert(calib_buffer, &iter,"Vshift[V] --> ", -1);
	gtk_text_buffer_get_end_iter(calib_buffer, &iter);
	gtk_text_buffer_insert(calib_buffer,&iter,vshift_string,-1);
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
		if (GTK_IS_CHECK_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_TOGGLE_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "toggled");
		else if (GTK_IS_SPIN_BUTTON(widgets[i].widget))
			sprintf(signal_name, "%s", "value-changed");
		else if (GTK_IS_COMBO_BOX_TEXT(widgets[i].widget))
			sprintf(signal_name, "%s", "changed");
		else
			printf("unhandled widget type, attribute: %s\n",
			       widgets[i].attr_name);

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

static GtkWidget *cn0540_init(struct osc_plugin *plugin, GtkWidget *notebook,
			      const char *ini_fn)
{
	GtkBuilder *builder;
	struct iio_device *adc;
	struct iio_device *dac;
	struct iio_device *gpio;

	builder = gtk_builder_new();

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	if (osc_load_glade_file(builder, "cn0540") < 0)
		return NULL;

	adc = iio_context_find_device(ctx, ADC_DEVICE);
	dac = iio_context_find_device(ctx, DAC_DEVICE);
	gpio = iio_context_find_device(ctx, GPIO_CTRL);

	if (!adc || !dac) {
		printf("Could not find expected iio devices\n");
		return NULL;
	}

	adc_ch = iio_device_find_channel(adc, "voltage0", false);
	dac_ch = iio_device_find_channel(dac, "voltage0", true);
	sw_ff = iio_device_find_channel(gpio, "voltage2", true);
	sw_in = iio_device_find_channel(gpio, "voltage3",true);
	iio_channel_attr_write_longlong(dac_ch, "raw", DAC_DEFAULT_VAL);

	cn0540_panel = GTK_WIDGET(gtk_builder_get_object(builder,
							 "cn0540_panel"));
	swff_enable = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder,"SWFF_enable"));
	swin_enable = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder,"SWIN_enable"));
	fdadis_enable = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder,"FDADIS_enable"));
	fdamode_pow = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder,"FDAMODE_power"));
	calib_btn = GTK_WIDGET(gtk_builder_get_object(builder,"calib_btn"));
	read_btn = GTK_WIDGET(gtk_builder_get_object(builder,"read_btn"));
	write_btn = GTK_WIDGET(gtk_builder_get_object(builder,"write_btn"));
	readvsensor_btn = GTK_WIDGET(gtk_builder_get_object(builder,"readvsensor_btn"));
	calib_status = GTK_TEXT_VIEW(gtk_builder_get_object(builder,"calib_status"));
	calib_buffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,"calib_buffer"));
	vshift_log = GTK_TEXT_VIEW(gtk_builder_get_object(builder,"vshift_log"));
	vshift_buf = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,"vshift_buf"));
	vsensor_log = GTK_TEXT_VIEW(gtk_builder_get_object(builder,"vsensor_log"));
	vsensor_buf = GTK_TEXT_BUFFER(gtk_builder_get_object(builder,"vsensor_buf"));


	iio_toggle_button_init_from_builder(&iio_widgets[num_widgets++],
			adc, NULL, "en", builder,
			"SWFF_enable", 0);

	iio_toggle_button_init_from_builder(&iio_widgets[num_widgets++],
			adc, NULL, "en", builder,
			"SWIN_enable", 0);

	iio_toggle_button_init_from_builder(&iio_widgets[num_widgets++],
			adc, NULL, NULL, builder,
			"FDADIS_enable", 1);

	iio_toggle_button_init_from_builder(&iio_widgets[num_widgets++],
			adc, NULL, NULL, builder,
			"FDAMODE_power", 1);

	iio_button_init_from_builder(&iio_widgets[num_widgets++],
			adc,NULL,NULL,builder,"calib_btn");

	iio_button_init_from_builder(&iio_widgets[num_widgets++],
			adc,NULL,NULL,builder,
			"readvsensor_btn");

	iio_button_init_from_builder(&iio_widgets[num_widgets++],
			dac,NULL,NULL,builder,
			"read_btn");

	iio_button_init_from_builder(&iio_widgets[num_widgets++],
			dac,NULL,NULL,builder,
			"write_btn");

	make_widget_update_signal_based(iio_widgets, num_widgets);
	iio_update_widgets(iio_widgets, num_widgets);


	g_signal_connect(G_OBJECT(swff_enable), "toggled",
			 G_CALLBACK(enable_swff), NULL);
	g_signal_connect(G_OBJECT(swin_enable), "toggled",
			 G_CALLBACK(enable_swin), NULL);

	g_signal_connect(G_OBJECT(fdadis_enable), "toggled",
			 G_CALLBACK(enable_fdadis), NULL);
	g_signal_connect(G_OBJECT(fdamode_pow), "toggled",
			 G_CALLBACK(set_fdapow), NULL);
	g_signal_connect(G_OBJECT(calib_btn),"clicked",
			 G_CALLBACK(calib),NULL);
	g_signal_connect(G_OBJECT(read_btn),"clicked",
			 G_CALLBACK(read_vshift),NULL);
	g_signal_connect(G_OBJECT(write_btn),"clicked",
			 G_CALLBACK(write_vshift),NULL);
	g_signal_connect(G_OBJECT(readvsensor_btn),"clicked",
			 G_CALLBACK(read_vsensor),NULL);

	return cn0540_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page,
			       gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void cn0540_get_preferred_size(const struct osc_plugin *plugin,
				      int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	g_source_remove_by_user_data(ctx);
	osc_destroy_context(ctx);
}

struct osc_plugin plugin;

static bool cn0540_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	return !!iio_context_find_device(osc_ctx, ADC_DEVICE) &&
			!!iio_context_find_device(osc_ctx, DAC_DEVICE) &&
			!!iio_context_find_device(osc_ctx, GPIO_CTRL);
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = cn0540_identify,
	.init = cn0540_init,
	.update_active_page = update_active_page,
	.get_preferred_size = cn0540_get_preferred_size,
	.destroy = context_destroy,
};
