/**
 * Copyright (C) 2012-2014 Analog Devices, Inc.
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

#define THIS_DRIVER "Partial Reconfiguration"

#define ARRAY_SIZE(x) (!sizeof(x) ?: sizeof(x) / sizeof((x)[0]))

#define PHY_DEVICE	"ad9361-phy"
#define DEVICE_NAME_ADC	"cf-ad9361-lpc"
#define DEVICE_NAME_DAC	"cf-ad9361-dds-core-lpc"

#define PR_STATUS_ADDR	0x800000B8
#define PR_CONTROL_ADDR	0x800000BC

#define PR_LOGIC_DEFAULT_ID	0xA0
#define PR_LOGIC_BIST_ID	0xA1
#define PR_LOGIC_QPSK_ID	0xA2

#define IS_PARTIAL_BITSTREAM_FILEPATH "/sys/bus/platform/devices/f8007000.devcfg/is_partial_bitstream"
#define XDEVCFG_FILEPATH "/dev/xdevcfg"

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
static GtkWidget *pr_config_panel;
static gboolean plugin_detached;

static const char * pr_config_driver_attribs[] = {
	"config_file",
	"adc_active",
};

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

	ssize_t status = 0;
	int ret, fd;

	fd = open(pr_bin_path, O_RDONLY);
	if(fd < 0) {
		return "Could not open file!";
	} else {
		status = read(fd, buf_pr, BUF_SIZE);
		if(status < 0) {
			close(fd);
			return "Could not read file!";
		}
		close(fd);
	}

	/* set is_partial_bitfile device attribute */
	fd_is_partial = open(IS_PARTIAL_BITSTREAM_FILEPATH, O_RDWR);
	if (fd_is_partial < 0) {
		return "Could not open "IS_PARTIAL_BITSTREAM_FILEPATH;
	} else {
		ret = write(fd_is_partial, "1", 2);
		close(fd_is_partial);
	}
	if (ret != 2)
		return "Could not write to "IS_PARTIAL_BITSTREAM_FILEPATH;

	/* write partial bitfile to devcfg device */
	fd_devcfg = open(XDEVCFG_FILEPATH, O_RDWR);
	if(fd_devcfg < 0) {
		return "Could not open "XDEVCFG_FILEPATH;
	} else {
		ret = write(fd_devcfg, buf_pr, BUF_SIZE);
		sleep(1);
		close(fd_devcfg);
	}
	if (ret != BUF_SIZE)
		return "Could not write to "XDEVCFG_FILEPATH;

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

static int getPrId() {

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

static int pr_config_handle_driver(struct osc_plugin *plugin, const char *attrib, const char *value)
{
	if (MATCH_ATTRIB("config_file")) {
		pr_config_file_apply(value);
	} else if (MATCH_ATTRIB("adc_active")) {
		gtk_combo_box_set_active(GTK_COMBO_BOX(regmap_select),
				atoi(value) ? ADC_REGMAP : DAC_REGMAP);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int pr_config_handle(struct osc_plugin *plugin, int line, const char *attrib, const char *value)
{
	return osc_plugin_default_handle(ctx, line, attrib, value,
			pr_config_handle_driver, NULL);
}

static void load_profile(struct osc_plugin *plugin, const char *ini_fn)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pr_config_driver_attribs); i++) {
		char *value = read_token_from_ini(ini_fn, THIS_DRIVER,
				pr_config_driver_attribs[i]);
		if (value) {
			pr_config_handle_driver(NULL,
					pr_config_driver_attribs[i], value);
			free(value);
		}
	}
}

static GtkWidget * pr_config_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;

	builder = gtk_builder_new();

	if (osc_load_glade_file(builder, "pr_config") < 0)
		return NULL;

	pr_config_panel = GTK_WIDGET(gtk_builder_get_object(builder, "pr_config_panel"));
	reconf_chooser = GTK_WIDGET(gtk_builder_get_object(builder, "filechooserbutton_reconf"));
	reconf_path_text = GTK_WIDGET(gtk_builder_get_object(builder, "textview_reconf_file_path"));
	regmap_select = GTK_WIDGET(gtk_builder_get_object(builder, "comboboxtext_regmap_select"));
	pr_stat_text = GTK_WIDGET(gtk_builder_get_object(builder, "entry_pr_stat"));
	pr_conf_text = GTK_WIDGET(gtk_builder_get_object(builder, "entry_pr_ctrl"));
	reg_read = GTK_WIDGET(gtk_builder_get_object(builder, "button_regs_read"));
	reg_write = GTK_WIDGET(gtk_builder_get_object(builder, "button_regs_write"));

	if (ini_fn)
		load_profile(NULL, ini_fn);

	g_signal_connect(reconf_chooser, "file-set",
		G_CALLBACK(reconfig_file_set_cb), NULL);
	g_signal_connect(regmap_select, "changed",
		G_CALLBACK(device_changed_cb), NULL);
	g_signal_connect(reg_read, "clicked",
		G_CALLBACK(reg_read_clicked_cb), NULL);
	g_signal_connect(reg_write, "clicked",
		G_CALLBACK(reg_write_clicked_cb), NULL);

	gtk_combo_box_set_active(GTK_COMBO_BOX(regmap_select), ADC_REGMAP);

	return pr_config_panel;
}

static void update_active_page(struct osc_plugin *plugin, gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static void pr_config_get_preferred_size(const struct osc_plugin *plugin, int *width, int *height)
{
	if (width)
		*width = 640;
	if (height)
		*height = 480;
}

static void save_widgets_to_ini(FILE *f)
{
	fprintf(f, "config_file = %s\n"
			"adc_active = %i\n",
			config_file_path,
			gtk_combo_box_get_active(GTK_COMBO_BOX(regmap_select)) == ADC_REGMAP);
}

static void save_profile(const struct osc_plugin *plugin, const char *ini_fn)
{
	FILE *f = fopen(ini_fn, "a");
	if (f) {
		save_widgets_to_ini(f);
		fclose(f);
	}
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	save_profile(NULL, ini_fn);
	osc_destroy_context(ctx);
}

struct osc_plugin plugin;

static bool pr_config_identify(const struct osc_plugin *plugin)
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
		osc_destroy_context(ctx);

	return init;
}

struct osc_plugin plugin = {
	.name = THIS_DRIVER,
	.identify = pr_config_identify,
	.init = pr_config_init,
	.update_active_page = update_active_page,
	.get_preferred_size = pr_config_get_preferred_size,
	.handle_item = pr_config_handle,
	.save_profile = save_profile,
	.load_profile = load_profile,
	.destroy = context_destroy,
};
