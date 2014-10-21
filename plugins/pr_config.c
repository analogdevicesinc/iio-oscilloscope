/**
 * Copyright (C) 2012-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <glib/gthread.h>
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
#include <sys/utsname.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../osc_plugin.h"
#include "../config.h"

#define PHY_DEVICE	"ad9361-phy"
#define DEVICE_NAME_ADC	"cf-ad9361-lpc"
#define DEVICE_NAME_DAC	"cf-ad9361-dds-core-lpc"

#define PR_STATUS_ADDR	0x800000B8
#define PR_CONTROL_ADDR	0x800000BC

#define PR_LOGIC_DEFAULT_ID	0xA0
#define PR_LOGIC_BIST_ID	0xA1
#define PR_LOGIC_QPSK_ID	0xA2

#define BUF_SIZE 0x00300000
static char buf_pr[BUF_SIZE];
static int fd_devcfg = 0;
static int fd_is_partial = 0;
static char *config_file_path;

static struct iio_context *ctx;
static struct iio_device *phy, *adc, *dac;
static bool context_is_local;

enum regmaps {
	ADC_REGMAP,
	DAC_REGMAP,
};

static GtkWidget *reconf_chooser;
static GtkWidget *reconf_path_text;
static GtkWidget *regmap_select;
static GtkWidget *pr_stat_text;
static GtkWidget *pr_conf_text;
static GtkWidget *reg_read;
static GtkWidget *reg_write;

static gint this_page;
static GtkNotebook *nbook;
static GtkWidget *pr_config_panel;
static gboolean plugin_detached;

static void entry_set_hex_int(GtkWidget *entry, unsigned data)
{
	gchar *buf;

	g_return_if_fail(GTK_ENTRY(entry));

	buf = g_strdup_printf("0x%.8x", data);
	gtk_entry_set_text(GTK_ENTRY(entry), buf);
	g_free(buf);
}

/*
 * Update the PR buffer with the specified bin
 */
static const char * updatePR(const char * pr_bin_path) {

	uint32_t pr_logic_fp = -1;
	uint32_t status = 0;
	int ret;

	pr_logic_fp = open(pr_bin_path, O_RDONLY);
	if(pr_logic_fp < 0) {
		return "Could not open file!";
	} else {
		status = read(pr_logic_fp, buf_pr, BUF_SIZE);
		if(status < 0) {
			close(pr_logic_fp);
			return "Could not read file!";
		}
		close(pr_logic_fp);
	}

	/* set is_partial_bitfile device attribute */
	fd_is_partial = open("/sys/devices/amba.1/f8007000.devcfg/is_partial_bitstream", O_RDWR);
	if (fd_is_partial < 0) {
		return "Could not open /sys/devices/amba.1/f8007000.devcfg/is_partial_bitstream";
	} else {
		ret = write(fd_is_partial, "1", 2);
		close(fd_is_partial);
	}
	if (ret != 2)
		return "Could not write to /sys/devices/amba.1/f8007000.devcfg/is_partial_bitstream";

	/* write partial bitfile to devcfg device */
	fd_devcfg = open("/dev/xdevcfg", O_RDWR);
	if(fd_devcfg < 0) {
		return "Could not open /dev/xdevcfg";
	} else {
		ret = write(fd_devcfg, buf_pr, BUF_SIZE);
		sleep(1);
		close(fd_devcfg);
	}
	if (ret != BUF_SIZE)
		return "Could not write to /sys/devices/amba.1/f8007000.devcfg/is_partial_bitstream";

	return NULL;
}

static void writeReg(char* device, uint32_t address, uint32_t data) {
	struct iio_device *dev;

	if (!device) {
		perror("writeReg() - device is NULL");
	}

	dev = iio_context_find_device(ctx, device);

	if (!dev) {
		perror("writeReg() - Unable to find device!");
		return;
	}
	/* register write */
	iio_device_reg_write(dev, address, data);
}

static void readReg(char* device, uint32_t address, uint32_t* data) {
	struct iio_device *dev;

	if (!device) {
		perror("readReg() - device is NULL");
	}

	dev = iio_context_find_device(ctx, device);

	if (!dev) {
		perror("readReg() - Unable to find device!");
		return;
	}
	/* register read */
	iio_device_reg_read(dev, address, data);
}

int getPrId() {

	uint32_t data = 0;

	/* read the current status register */
	readReg(DEVICE_NAME_ADC, PR_STATUS_ADDR, &data);

	return (data & 0xFF);
}

static void pr_config_file_apply(const char *filename)
{
	GtkTextBuffer *buffer;
	gchar *msg_error = NULL;
	const gchar *ret_msg = NULL;

	if (!context_is_local) {
		msg_error = g_strdup_printf("Partial Reconfiguration is not supported in remote mode");
	} else if(!filename) {
		msg_error = g_strdup_printf("No file selected");
	} else if (!g_str_has_suffix(filename, ".bin")) {
		msg_error = g_strdup_printf("The selected file is not a .bin file");
	} else {
		ret_msg = updatePR(filename);
		if (ret_msg)
			msg_error = g_strdup_printf("%s", ret_msg);
	}

	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(reconf_path_text));
	if (!msg_error) {
		gtk_text_buffer_set_text(buffer, filename, -1);
	} else {
		gtk_text_buffer_set_text(buffer, msg_error, -1);
	}

	if (msg_error)
		g_free(msg_error);
}

static void reconfig_file_set_cb (GtkFileChooser *chooser, gpointer data)
{
	if (config_file_path)
		g_free(config_file_path);
	config_file_path = gtk_file_chooser_get_filename(chooser);

	pr_config_file_apply(config_file_path);
}

static void device_changed_cb(GtkComboBox *button, gpointer data)
{
	gtk_entry_set_text(GTK_ENTRY(pr_stat_text), "");
	gtk_entry_set_text(GTK_ENTRY(pr_conf_text), "");
}

static void reg_read_clicked_cb(GtkButton *button, gpointer data)
{
	uint32_t stat_reg, ctrl_reg;
	gchar *device;
	gchar *active_device;

	active_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(regmap_select));
	if (!active_device)
		return;
	if (!strcmp(active_device, "ADC"))
		device = DEVICE_NAME_ADC;
	else if (!strcmp(active_device, "DAC"))
		device = DEVICE_NAME_DAC;
	else {
		printf("Unknown device selection\n");
		g_free(active_device);
		return;
	}
	g_free(active_device);

	readReg(device, PR_STATUS_ADDR, &stat_reg);
	readReg(device, PR_CONTROL_ADDR, &ctrl_reg);

	entry_set_hex_int(pr_stat_text, stat_reg);
	entry_set_hex_int(pr_conf_text, ctrl_reg);
}

static void reg_write_clicked_cb(GtkButton *button, gpointer data)
{
	uint32_t reg_data;
	const char *buf;
	gchar *device;
	gchar *active_device;
	int ret;

	active_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(regmap_select));
	if (!active_device)
		return;
	if (!strcmp(active_device, "ADC"))
		device = DEVICE_NAME_ADC;
	else if (!strcmp(active_device, "DAC"))
		device = DEVICE_NAME_DAC;
	else {
		printf("Unknown device selection\n");
		g_free(active_device);
		return;
	}
	g_free(active_device);

	buf = gtk_entry_get_text(GTK_ENTRY(pr_conf_text));

	ret = sscanf(buf, "0x%x", &reg_data);
	if (ret != 1)
		ret = sscanf(buf, "%d", &reg_data);

	if (ret != 1)
		reg_data = 0;

	entry_set_hex_int(pr_conf_text, reg_data);
	writeReg(device, PR_CONTROL_ADDR, reg_data);
}

static int pr_config_init(GtkWidget *notebook)
{
	GtkBuilder *builder;

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (!gtk_builder_add_from_file(builder, "pr_config.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "pr_config.glade", NULL);

	pr_config_panel = GTK_WIDGET(gtk_builder_get_object(builder, "pr_config_panel"));
	reconf_chooser = GTK_WIDGET(gtk_builder_get_object(builder, "filechooserbutton_reconf"));
	reconf_path_text = GTK_WIDGET(gtk_builder_get_object(builder, "textview_reconf_file_path"));
	regmap_select = GTK_WIDGET(gtk_builder_get_object(builder, "comboboxtext_regmap_select"));
	pr_stat_text = GTK_WIDGET(gtk_builder_get_object(builder, "entry_pr_stat"));
	pr_conf_text = GTK_WIDGET(gtk_builder_get_object(builder, "entry_pr_ctrl"));
	reg_read = GTK_WIDGET(gtk_builder_get_object(builder, "button_regs_read"));
	reg_write = GTK_WIDGET(gtk_builder_get_object(builder, "button_regs_write"));

	g_signal_connect(reconf_chooser, "file-set",
		G_CALLBACK(reconfig_file_set_cb), NULL);
	g_signal_connect(regmap_select, "changed",
		G_CALLBACK(device_changed_cb), NULL);
	g_signal_connect(reg_read, "clicked",
		G_CALLBACK(reg_read_clicked_cb), NULL);
	g_signal_connect(reg_write, "clicked",
		G_CALLBACK(reg_write_clicked_cb), NULL);

	gtk_combo_box_set_active(GTK_COMBO_BOX(regmap_select), ADC_REGMAP);

	this_page = gtk_notebook_append_page(nbook, pr_config_panel, NULL);

	return 0;
}

static char *handle_item(struct osc_plugin *plugin, const char *attrib,
			 const char *value)
{
	if (MATCH_ATTRIB("config_file")) {
		if (value) {
			if (value[0])
				pr_config_file_apply(value);
		} else {
			return config_file_path;
		}
	} else if (MATCH_ATTRIB("adc_active")) {
		if (value) {
			if (value[0]) {
				if (atoi(value))
					gtk_combo_box_set_active(GTK_COMBO_BOX(regmap_select), ADC_REGMAP);
				else
					gtk_combo_box_set_active(GTK_COMBO_BOX(regmap_select), DAC_REGMAP);
			}
		} else {
			if (gtk_combo_box_get_active(GTK_COMBO_BOX(regmap_select)) == ADC_REGMAP)
				return "1";
			else
				return "0";
		}
	} else {
		if (value) {
			printf("Unhandled tokens in ini file,\n"
				"\tSection %s\n\tAtttribute : %s\n\tValue: %s\n",
				"Partial Reconfiguration", attrib, value);
			return "FAIL";
		}
	}

	return NULL;
}

static const char *pr_config_sr_attribs[] = {
	"config_file",
	"adc_active",
};

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void pr_config_get_preferred_size(int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void context_destroy(void)
{
	iio_context_destroy(ctx);
}

struct osc_plugin plugin;

static bool pr_config_identify(void)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();

	if (!iio_context_find_device(osc_ctx, PHY_DEVICE) ||
		!iio_context_find_device(osc_ctx, DEVICE_NAME_ADC) ||
		!iio_context_find_device(osc_ctx, DEVICE_NAME_DAC))
		return false;

	ctx = osc_create_context();
	phy = iio_context_find_device(ctx, PHY_DEVICE);
	adc = iio_context_find_device(ctx, DEVICE_NAME_ADC);
	dac = iio_context_find_device(ctx, DEVICE_NAME_DAC);

	context_is_local = !strncmp(iio_context_get_name(ctx), "local", strlen("local"));

	int id;
	bool init = true;

	if (!phy || !adc || !dac) {
		init = false;
	} else {
		id = getPrId();
		if ((id != PR_LOGIC_DEFAULT_ID) &&
				(id != PR_LOGIC_BIST_ID) &&
				(id != PR_LOGIC_QPSK_ID))
			init = false;
	}
	if (phy && !iio_device_get_debug_attrs_count(phy))
		init = false;
	if (!init)
		iio_context_destroy(ctx);

	return init;
}

struct osc_plugin plugin = {
	.name = "Partial Reconfiguration",
	.identify = pr_config_identify,
	.init = pr_config_init,
	.save_restore_attribs = pr_config_sr_attribs,
	.handle_item = handle_item,
	.update_active_page = update_active_page,
	.get_preferred_size = pr_config_get_preferred_size,
	.destroy = context_destroy,
};
