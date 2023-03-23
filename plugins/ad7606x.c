/**
 * Copyright (C) 2023 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <glib.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../libini2.h"

#define THIS_DRIVER "AD7606X"

#define ADC_COMMON_WR_CONFIG		0x80000080
#define ADC_COMMON_RD_CONFIG		0x80000084
#define ADC_COMMON_CFG_CTRL		0x8000008C
#define ADC_COMMON_RD_STATUS		0x8000005C
#define AD7606X_HDL_CFG_MODE		0x8000004C

#define AD7606X_REG_STATUS		0x1
#define AD7606X_REG_CONFIG		0x2
#define AD7606X_REG_RANGE_CH(x)		(0x3 + (x) * 0x1)
#define AD7606X_REG_BANDWIDTH		0x7
#define AD7606X_REG_OVERSAMPLING	0x8
#define AD7606X_REG_GAIN_CH(x)		(0x9 + (x) * 0x1)
#define AD7606X_REG_OFFSET_CH(x)	(0x11 + (x) * 0x1)
#define AD7606X_REG_PHASE_CH(x)		(0x19 + (x) * 0x1)
#define AD7606X_REG_DIGITAL_DIAG_EN	0x21
#define AD7606X_REG_DIGITAL_DIAG_ERR	0x22
#define AD7606X_REG_OPEN_DETECT_EN	0x23
#define AD7606X_REG_OPEN_DETECTED	0x24
#define AD7606X_REG_DIAG_MUX_CH(x)	(0x28 + (x) * 0x1)
#define AD7606X_REG_OPEN_DETECT_QE	0x2C
#define AD7606X_REG_FS_CLK_COUNTER	0x2D
#define AD7606X_REG_OS_CLK_COUNTER	0x2E
#define AD7606X_REG_ID			0x2F

/*
 * REG_CONFIG - Status setup
 * BITS (default value)
 * [7 - RESERVED (0), 6 - STATUS_HEADER (0), 5 - EXT_OS_CLOCK (0), 4:3 - DOUT_FORMAT (1), 2 - RESERVED (0), 1:0 - OPERATION_MODE (0)]
 * STATUS_HEADER setup is ['b01001000]
 *
 * REG_DIGITAL_DIAG_EN - CRC setup
 * BITS (default value)
 * [7 - INTERFACE_CHECK_EN (0), 6 - CLK_FS_OS_COUNTER_EN (0), 5 - BUSY_STUCK_HIGH_ERR_EN (0), 4 - SPI_READ_ERR_EN (0), 3 - SPI_WRITE_ERR_EN (0), 2 - INT_CRC_ERR_EN (0), 1 - MM_CRC_ERR_EN (0), 0 - ROM_CRC_ERR_EN (1)]
 * CRC setup is ['b00000101]
 *
 * CTRL_WR, DEFAULT
 *  0x1 - Write request in REG_ADC_COMMON_CFG_CTRL
 *  0x0 - Set wr/rd bit to default state in REG_ADC_COMMON_CFG_CTRL
 *
 * CTRL_UNSET_REG_MODE
 *  0x0 - Exit the register mode of AD7606X (sent using REG_ADC_COMMON_WR_CONFIG and CTRL_WR)
 *
 */

#define AD7606X_CRC_EN_BITS		0x5
#define AD7606X_DIGITAL_DIAG_DEFAULT	0x1
#define AD7606X_CTRL_WR			0x1
#define AD7606X_CTRL_DEFAULT		0x0
#define AD7606X_CTRL_UNSET_REG_MODE	0x0
#define AD7606X_STATUS_EN_BITS		0x48
#define AD7606X_CONFIG_DEFAULT		0x8

#define SET_SIMPLE_MODE			0x100
#define SET_CRC_EN_MODE			0x101
#define SET_STATUS_EN_MODE		0x102
#define SET_CRC_STATUS_EN_MODE		0x103

static struct iio_context *ctx;
static struct iio_device *iio_ad7606x;

static GtkWidget *ad7606x_op_mode;
static GtkWidget *ad7606x_op_mode_set;
static gint this_page;
static GtkWidget *ad7606x_config_panel;
static gboolean plugin_detached;

/******************************************************************************/
/**************************** Functions prototypes ****************************/
/******************************************************************************/

static int ad7606x_check_op_mode(struct iio_device *dev, short int op_check);

static void entry_set_hex_int(GtkWidget *entry, unsigned data)
{
	gchar *buf;

	g_return_if_fail(GTK_ENTRY(entry));

	buf = g_strdup_printf("0x%.8x", data);
	gtk_entry_set_text(GTK_ENTRY(entry), buf);
	g_free(buf);
}

static void ad7606x_wr_req (void)
{
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_CFG_CTRL, AD7606X_CTRL_WR);
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_CFG_CTRL, AD7606X_CTRL_DEFAULT);
}

static void ad7606x_set_simple_op_mode (void)
{
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_WR_CONFIG, (AD7606X_REG_DIGITAL_DIAG_EN << 8) + (AD7606X_DIGITAL_DIAG_DEFAULT << 0));
	ad7606x_wr_req();
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_WR_CONFIG, (AD7606X_REG_CONFIG << 8) + (AD7606X_CONFIG_DEFAULT << 0));
	ad7606x_wr_req();
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_WR_CONFIG, AD7606X_CTRL_UNSET_REG_MODE);
	ad7606x_wr_req();
	iio_device_reg_write(iio_ad7606x, AD7606X_HDL_CFG_MODE, SET_SIMPLE_MODE);
}

static void ad7606x_set_crc_op_mode (void)
{
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_WR_CONFIG, (AD7606X_REG_DIGITAL_DIAG_EN << 8) + (AD7606X_CRC_EN_BITS << 0));
	ad7606x_wr_req();
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_WR_CONFIG, AD7606X_CTRL_UNSET_REG_MODE);
	ad7606x_wr_req();
	iio_device_reg_write(iio_ad7606x, AD7606X_HDL_CFG_MODE, SET_CRC_EN_MODE);
}

static void ad7606x_set_status_op_mode (void)
{
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_WR_CONFIG, (AD7606X_REG_CONFIG << 8) + (AD7606X_STATUS_EN_BITS << 0));
	ad7606x_wr_req();
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_WR_CONFIG, AD7606X_CTRL_UNSET_REG_MODE);
	ad7606x_wr_req();
	iio_device_reg_write(iio_ad7606x, AD7606X_HDL_CFG_MODE, SET_STATUS_EN_MODE);
}

static void ad7606x_set_crc_status_op_mode (void)
{
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_WR_CONFIG, (AD7606X_REG_DIGITAL_DIAG_EN << 8) + (AD7606X_CRC_EN_BITS << 0));
	ad7606x_wr_req();
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_WR_CONFIG, (AD7606X_REG_CONFIG << 8) + (AD7606X_STATUS_EN_BITS << 0));
	ad7606x_wr_req();
	iio_device_reg_write(iio_ad7606x, ADC_COMMON_WR_CONFIG, AD7606X_CTRL_UNSET_REG_MODE);
	ad7606x_wr_req();
	iio_device_reg_write(iio_ad7606x, AD7606X_HDL_CFG_MODE, SET_CRC_STATUS_EN_MODE);
}

static void ad7606x_op_mode_cb(GtkButton *button, gpointer data)
{
	gchar *active_mode;

	active_mode = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(ad7606x_op_mode));

	if (!active_mode)
		return;
	if (!strcmp(active_mode, "SIMPLE"))
	{
		ad7606x_set_simple_op_mode();
	} else if (!strcmp(active_mode, "CRC_EN")) {
		ad7606x_set_crc_op_mode();
		g_print("CRC_EN selected");
	} else if (!strcmp(active_mode, "STATUS_EN")) {
		ad7606x_set_status_op_mode();
	} else if (!strcmp(active_mode, "CRC_STATUS_EN")) {
		ad7606x_set_crc_status_op_mode();
	}

}

void cmb_op_mode_cb(GtkWidget *widget, gpointer *data)
{
	g_print ("Operation mode changed\n");
}

static GtkWidget * ad7606x_config_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;
	
	builder = gtk_builder_new();

	if (osc_load_glade_file(builder, "ad7606x") < 0) {
		osc_destroy_context(ctx);
		return NULL;
	}

	ad7606x_config_panel = GTK_WIDGET(gtk_builder_get_object(builder, "ad7606x_config_panel"));
	ad7606x_op_mode = GTK_WIDGET(gtk_builder_get_object(builder, "cmb_ad7606x_op_mode"));
	ad7606x_op_mode_set = GTK_WIDGET(gtk_builder_get_object(builder, "button_ad7606x_set_mode"));

	g_signal_connect(ad7606x_op_mode, "changed",
		G_CALLBACK(cmb_op_mode_cb), NULL);
	g_signal_connect(ad7606x_op_mode_set, "clicked",
		G_CALLBACK(ad7606x_op_mode_cb), "NULL");

	return ad7606x_config_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void ad7606x_config_get_preferred_size(const struct osc_plugin *plugin, int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}
static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	osc_destroy_context(ctx);
}

struct osc_plugin plugin;

static bool ad7606x_config_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	iio_ad7606x = iio_context_find_device(osc_ctx, "ad7606x");

	if (!iio_ad7606x) {
		printf("Could not find ad7606x iio device\n");
		return FALSE;
	}

	return TRUE;
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = ad7606x_config_identify,
	.init = ad7606x_config_init,
	.update_active_page = update_active_page,
	.get_preferred_size = ad7606x_config_get_preferred_size,
	.destroy = context_destroy,
};
