/**
* Copyright (C) 2012-2013 Analog Devices, Inc.
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
#include <libxml/xpath.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>

#include <iio.h>

#include "../osc.h"
#include "../iio_utils.h"
#include "../xml_utils.h"
#include "../osc_plugin.h"
#include "../config.h"

typedef struct _reg reg;
typedef struct _bgroup bgroup;
typedef struct _option option;

struct _reg
{
	char   *name;        /* name of the register */
	char   *notes;       /* additional notes */
	char   *width;       /* width of the register */
	int    def_val;      /* default value of the register */
	int    bgroup_cnt;   /* total count of bit groups */
	bgroup *bgroup_list; /* list of the bit groups */
};

struct _bgroup
{
	char   *name;        /* name of the bit group */
	int    width;        /* width of the bit group */
	int    offset;       /* register start position of the bit group */
	int    def_val;      /* default value of the bit group */
	char   *access;      /* access type of the bit group */
	char   *description; /* description of the bit group */
	int    options_cnt;  /* total count of the options available for the bit group */
	option *option_list; /* list of the options */
};

struct _option
{
	char *text; /* option description */
	int  value; /* option value */
};

enum register_map_source {
	REG_MAP_SPI,
	REG_MAP_AXI_CORE
};

/* GUI widgets */
static GtkWidget *combobox_device_list;
static GtkWidget *spin_btn_reg_addr;
static GtkWidget *spin_btn_reg_value;
static GtkWidget *label_reg_hex_addr;
static GtkWidget *label_reg_hex_value;
static GtkWidget *btn_read_reg;
static GtkWidget *btn_write_reg;
static GtkWidget *label_reg_descrip;
static GtkWidget *label_reg_def_val;
static GtkWidget *label_reg_notes;
static GtkWidget *label_notes_tag;
static GtkWidget *warning_label;
static GtkWidget *reg_autoread;
static GtkWidget *reg_map_type;
static GtkWidget *toggle_detailed_regmap;

/* IIO Scan Elements widgets */
static GtkWidget *scanel_read;
static GtkWidget *scanel_write;
static GtkWidget *scanel_value;
static GtkWidget *combobox_attr_type;
static GtkWidget *combobox_debug_scanel;
static GtkWidget *scanel_options;
static GtkWidget *scanel_filename;
static gulong attr_type_hid;
static gulong debug_scanel_hid;

/* Register map widgets */
static GtkWidget *register_section;
static GtkWidget *scrollwin_regmap;
static GtkWidget *reg_map_container;
static GtkWidget *hbox_bits_container;
static GtkWidget **lbl_bits;
static GtkWidget **hboxes;
static GtkWidget **vboxes;
static GtkWidget **bit_descrip_list;
static GtkWidget **elem_frames;
static GtkWidget **bit_comboboxes;
static GtkWidget **bit_no_read_lbl;
static GtkWidget **bit_spinbuttons;
static GtkWidget **bit_spin_adjustments;

/* Libiio variables */
static struct iio_context *ctx;
static struct iio_device *dev;
static struct iio_channel *current_ch;
static bool attribute_has_options;

/* Register map variables */
static int *reg_addr_list;     /* Pointer to the list of addresses of all registers */
static int reg_list_size;      /* Number of register addresses */
static int reg_bit_width;      /* The size in bits that all registers have in common */
static reg soft_reg;           /* Holds all information of a register and of the contained bits  */
static gulong reg_map_hid;     /* The handler id of the register map type combobox */
static gulong reg_addr_hid;    /* The handler id of the register address spin button */
static gulong reg_val_hid;     /* The handler id of the register value spin button */
static gulong *combo_hid_list; /* Handler ids of the bit options(comboboxes) */
static gulong *spin_hid_list;  /* Handler ids of the bit options(spinbuttons) */

/* XML data pointers */
static xmlDocPtr xml_doc; /* The reference to the xml file */
static xmlNodePtr root; /* The reference to the first element in the xml file */
static xmlXPathObjectPtr register_list; /* List of register references */

static int context_created = 0; /* register data allocation flag */
static int soft_reg_exists = 0; /* keeps track of alloc-free calls */
static int xml_file_opened = 0; /* a open file flag */

/* The path of the directory containing the xml files. */
static char xmls_folder_path[512];

/******************************************************************************/
/**************************** Functions prototypes ****************************/
/******************************************************************************/

static int alloc_widget_arrays(int reg_length);
static void free_widget_arrays(void);
static void fill_soft_register(reg *p_reg, int reg_index);
static void free_soft_register(reg *p_reg);
static int display_reg_info(int pos_reg_addr);
static void block_bit_option_signals(void);
static void unblock_bit_option_signals(void);
static int get_reg_default_val(reg *p_reg);
static int get_option_index(int option_value, bgroup *bit);
static void draw_reg_map(int valid_register);
static void reveal_reg_map(void);
static void hide_reg_map(void);
static void create_reg_map(void);
static void clean_gui_reg_info(void);
static int get_default_reg_width(void);
static int * get_reg_addr_list(void);
static int get_reg_pos(int *regList, int reg_addr);
static int update_regmap(int data);
static void create_device_context(void);
static void destroy_device_context(void);
static void destroy_regmap_widgets(void);
static void gtk_combo_box_text_remove_all (GtkWidget *combo_box);
static void combo_box_text_sort(GtkComboBoxText *box, int column, int order);
static bool combo_box_text_set_active_text(GtkComboBoxText *comboboxtext,
		const char *text);
static void combo_box_text_add_default_text(GtkComboBoxText *box,
		const char *text);
static void debug_register_section_init(struct iio_device *iio_dev);
static void reg_map_chooser_init(struct iio_device *dev);
static bool xml_file_exists(const char *filename);
static bool pcore_get_version(const char *dev_name, int *major);

/******************************************************************************/
/******************************** Callbacks ***********************************/
/******************************************************************************/
static void scanel_read_clicked(GtkButton *btn, gpointer data)
{
	char *attr;
	char *attr_val = NULL;
	int ret;

	attr = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox_debug_scanel));
	if (strcmp(attr, "None") == 0)
		goto abort_read;

	attr_val = g_new(char, IIO_ATTR_MAX_BYTES);
	if (!attr_val)
		goto abort_read;

	if (current_ch)
		ret = iio_channel_attr_read(current_ch, attr, attr_val, IIO_ATTR_MAX_BYTES);
	else
		ret = iio_device_attr_read(dev, attr, attr_val, IIO_ATTR_MAX_BYTES);

	if (ret <= 0)
		goto abort_read;

	if (attribute_has_options)
		combo_box_text_set_active_text(GTK_COMBO_BOX_TEXT(scanel_options), attr_val);
	else
		gtk_entry_set_text(GTK_ENTRY(scanel_value), attr_val);

	g_free(attr);
	g_free(attr_val);
	return;

abort_read:
	gtk_entry_set_text(GTK_ENTRY(scanel_value), "");

	g_free(attr);
	g_free(attr_val);
}

static void scanel_write_clicked(GtkButton *btn, gpointer data)
{
	char *attr_name;
	char *attr_val;

	attr_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox_debug_scanel));
	if (attribute_has_options)
		attr_val = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(scanel_options));
	else
		attr_val = (char *)gtk_entry_get_text(GTK_ENTRY(scanel_value));

	if (current_ch)
		iio_channel_attr_write(current_ch, attr_name, attr_val);
	else
		iio_device_attr_write(dev, attr_name, attr_val);

	if (attribute_has_options)
		g_free(attr_val);

	scanel_read_clicked(GTK_BUTTON(scanel_read), data);
}

static void debug_scanel_changed_cb(GtkComboBoxText *cmbText, gpointer data)
{
	char *options_attr_val;
	const char *options_attr;
	char *attr;
	char buf[256];

	attr = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(cmbText));
	sprintf(buf, "%s_available", attr);
	if (current_ch)
		options_attr = iio_channel_find_attr(current_ch, buf);
	else
		options_attr = iio_device_find_attr(dev, buf);

	if (options_attr) {
		gchar **elems;
		gchar *elem;
		int i = 0;

		options_attr_val = g_new(char, IIO_ATTR_MAX_BYTES);
		if (!options_attr_val)
			goto cleanup;

		attribute_has_options = true;
		if (current_ch)
			iio_channel_attr_read(current_ch, options_attr, options_attr_val,
						IIO_ATTR_MAX_BYTES);
		else
			iio_device_attr_read(dev, options_attr, options_attr_val,
						IIO_ATTR_MAX_BYTES);

		if (options_attr_val[0] == '[') {
			/* Don't treat [min step max] as combobox items */
			attribute_has_options = false;
			gtk_widget_show(scanel_value);
			gtk_widget_hide(scanel_options);
		} else {

			gtk_combo_box_text_remove_all(scanel_options);
			elems = g_strsplit(options_attr_val, " ", -1);
			elem = elems[0];
			while (elem) {
				gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(scanel_options), elem);
				elem = elems[++i];
			}
			g_strfreev(elems);
			gtk_widget_show(scanel_options);
			gtk_widget_hide(scanel_value);
		}
		g_free(options_attr_val);
	} else {
		attribute_has_options = false;
		gtk_widget_show(scanel_value);
		gtk_widget_hide(scanel_options);
	}

	if (current_ch)
		gtk_entry_set_text(GTK_ENTRY(scanel_filename),
				iio_channel_attr_get_filename(current_ch, attr));
	else
		gtk_entry_set_text(GTK_ENTRY(scanel_filename), attr);

	scanel_read_clicked(GTK_BUTTON(scanel_read), NULL);

cleanup:
	g_free(attr);
}

static void attribute_type_changed_cb(GtkComboBoxText *cmbtext, gpointer data)
{
	int i, nb_attrs;
	struct iio_channel *ch;
	const char *attr;
	bool is_output_ch = false;
	bool global_attr;
	char *ch_id = NULL;

	char *attr_type;
	char **elems;
	attr_type = gtk_combo_box_text_get_active_text(cmbtext);
	elems = g_strsplit(attr_type, " ", 0);
	if (!strcmp(elems[0], "global")) {
		global_attr = true;
	} else {
		global_attr = false;
		if (!strcmp(elems[0], "output"))
			is_output_ch = true;
		else if (!strcmp(elems[0], "input"))
			is_output_ch = false;
		ch_id = strdup(elems[1]);
	}
	g_strfreev(elems);

	g_signal_handler_block(combobox_debug_scanel, debug_scanel_hid);
	gtk_combo_box_text_remove_all(combobox_debug_scanel);

	if (global_attr) {
		current_ch = NULL;
		nb_attrs = iio_device_get_attrs_count(dev);
		for (i = 0; i < nb_attrs; i++) {
			attr = iio_device_get_attr(dev, i);
			if (g_strstr_len(attr, -1, "_available\0"))
				continue;
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combobox_debug_scanel), attr);
		}
	} else {
		ch = iio_device_find_channel(dev, ch_id, is_output_ch);
		if (!ch)
			return;
		if (ch_id)
			g_free(ch_id);
		current_ch = ch;
		nb_attrs = iio_channel_get_attrs_count(ch);
		for (i = 0; i < nb_attrs; i++) {
			attr = iio_channel_get_attr(ch, i);
			if (g_strstr_len(attr, -1, "_available\0"))
				continue;
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combobox_debug_scanel), attr);
		}
	}
	if (nb_attrs == 0)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combobox_debug_scanel), "None");

	combo_box_text_sort(GTK_COMBO_BOX_TEXT(combobox_debug_scanel), 0, GTK_SORT_ASCENDING);
	g_signal_handler_unblock(combobox_debug_scanel, debug_scanel_hid);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combobox_debug_scanel), 0);
}

static void debug_device_list_cb(GtkButton *btn, gpointer data)
{
	char *current_device;
	int i = 0;

	destroy_regmap_widgets();
	destroy_device_context();
	clean_gui_reg_info();

	current_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox_device_list));
	if (g_strcmp0("None\0", current_device)) {
		dev = iio_context_find_device(ctx, current_device);
		reg_map_chooser_init(dev);
		debug_register_section_init(dev);

		gtk_widget_show(scanel_read);
		gtk_widget_show(scanel_write);
		gtk_widget_show(scanel_value);
		gtk_widget_hide(scanel_options);

		int nb_channels = iio_device_get_channels_count(dev);
		char tmp[1024];

		g_signal_handler_block(combobox_attr_type, attr_type_hid);
		gtk_combo_box_text_remove_all(combobox_attr_type);
		for (i = 0; i < nb_channels; i++) {
			struct iio_channel *ch = iio_device_get_channel(dev, i);

			if (iio_channel_get_name(ch))
				sprintf(tmp, "%s %s (%s)",
						iio_channel_is_output(ch) ? "output" : "input",
						iio_channel_get_id(ch), iio_channel_get_name(ch));
			else
				sprintf(tmp, "%s %s",
						iio_channel_is_output(ch) ? "output" : "input",
						iio_channel_get_id(ch));
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combobox_attr_type),
							tmp);
		}
		combo_box_text_sort(GTK_COMBO_BOX_TEXT(combobox_attr_type), 0, GTK_SORT_ASCENDING);
		gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(combobox_attr_type),
						0, "global");
		g_signal_handler_unblock(combobox_attr_type, attr_type_hid);
		gtk_combo_box_set_active(GTK_COMBO_BOX(combobox_attr_type), 0);
	} else {
		gtk_widget_set_sensitive(register_section, false);
		gtk_widget_hide(scanel_read);
		gtk_widget_hide(scanel_write);
		gtk_widget_hide(warning_label);
		gtk_entry_set_text(GTK_ENTRY(scanel_filename), "");
		gtk_entry_set_text(GTK_ENTRY(scanel_value), "");
		g_signal_handler_block(combobox_attr_type, attr_type_hid);
		g_signal_handler_block(combobox_debug_scanel, debug_scanel_hid);
		gtk_combo_box_text_remove_all(combobox_attr_type);
		combo_box_text_add_default_text(GTK_COMBO_BOX_TEXT(combobox_attr_type), "None");
		gtk_combo_box_text_remove_all(combobox_debug_scanel);
		combo_box_text_add_default_text(GTK_COMBO_BOX_TEXT(combobox_debug_scanel), "None");
		g_signal_handler_unblock(combobox_attr_type, attr_type_hid);
		g_signal_handler_unblock(combobox_debug_scanel, debug_scanel_hid);
	}
	g_free(current_device);
}

static void reg_read_clicked(GtkButton *button, gpointer user_data)
{
	uint32_t i;
	uint32_t address;
	char buf[16];
	int ret;

	address = (uint32_t)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin_btn_reg_addr));
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(reg_map_type)) == REG_MAP_AXI_CORE)
		address |= 0x80000000;
	ret = iio_device_reg_read(dev, address, &i);
	if (ret == 0) {
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_value), i);
		if (i == 0)
			g_signal_emit_by_name(spin_btn_reg_value, "value-changed");
		snprintf(buf, sizeof(buf), "%u", i);
		gtk_label_set_text(GTK_LABEL(label_reg_hex_value), buf);
		if (xml_file_opened)
			reveal_reg_map();
	} else {
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_value), 0);
		snprintf(buf, sizeof(buf), "<error>");
		gtk_label_set_text(GTK_LABEL(label_reg_hex_value), buf);
	}
	gtk_widget_show(btn_write_reg);
}

static void reg_write_clicked(GtkButton *button, gpointer user_data)
{
	uint32_t address;
	uint32_t val;

	address = (uint32_t)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin_btn_reg_addr));

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(reg_map_type)) == REG_MAP_AXI_CORE)
		address |= 0x80000000;

	val = (uint32_t)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin_btn_reg_value));
	iio_device_reg_write(dev, address, val);
}

static void reg_address_value_changed_cb(GtkSpinButton *spinbutton,
					gpointer user_data)
{
	static short block_signal = 0;
	static short prev_addr = -1;
	static short crt_pos = -1;
	int reg_addr;
	int spin_step;
	char buf[10];

	if (!xml_file_opened) {
		reg_addr = gtk_spin_button_get_value(spinbutton);
		snprintf(buf, sizeof(buf), "%d", reg_addr);
		gtk_label_set_text(GTK_LABEL(label_reg_hex_addr), buf);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_value), (gdouble)0);
		gtk_label_set_text(GTK_LABEL(label_reg_hex_value), "<unknown>");
		gtk_widget_hide(btn_write_reg);
		goto reg_autoread;
	}
	gtk_widget_set_sensitive(spin_btn_reg_addr, FALSE);
	if (!block_signal) {
		gtk_widget_hide(btn_write_reg);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_value),
			(gdouble)0);
		gtk_label_set_text(GTK_LABEL(label_reg_hex_value), "<unknown>");
		reg_addr = (int)gtk_spin_button_get_value(spinbutton);
		spin_step = reg_addr - prev_addr;
		/* When user changes address from "up" and "down" arrows (keyboard or mouse),
		   overwrite the spin button with the address of next/previous register */
		if (spin_step == 1){
			if (crt_pos < (reg_list_size - 1))
				crt_pos++;
			goto spin_overwrite;
		} else if (spin_step == -1) {
			if (crt_pos > 0)
				crt_pos--;
			else crt_pos = 0;
			goto spin_overwrite;
		} else {
			goto updt_addr;
		}
	} else {
		block_signal = 0;
		goto restore_widget;
	}

spin_overwrite:
	if (reg_addr_list[crt_pos] != reg_addr)
		block_signal = 1; /* Block next signal emitted by gtk_spin_button_set_value */
	gtk_spin_button_set_value(spinbutton, (gdouble)reg_addr_list[crt_pos]);
	reg_addr = (int)gtk_spin_button_get_value(spinbutton);
	prev_addr = reg_addr;
updt_addr:
	crt_pos = get_reg_pos(reg_addr_list, reg_addr);
	display_reg_info(crt_pos);
	hide_reg_map();
	reg_addr = (int)gtk_spin_button_get_value(spinbutton);
	prev_addr = reg_addr;
	snprintf(buf, sizeof(buf), "%d", reg_addr);
	gtk_label_set_text((GtkLabel *)label_reg_hex_addr, buf);
restore_widget:
	gtk_widget_set_sensitive(spin_btn_reg_addr, TRUE);
	gtk_widget_grab_focus(spin_btn_reg_addr);
	gtk_editable_select_region(GTK_EDITABLE(spinbutton), 0, 0);
reg_autoread:
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(reg_autoread)))
		g_signal_emit_by_name(btn_read_reg, "clicked", NULL);
}

static void reg_value_change_value_cb(GtkSpinButton *btn, gpointer user_data)
{
	unsigned value;
	char buf[16];

	if (!xml_file_opened) {
		value = gtk_spin_button_get_value(btn);
		snprintf(buf, sizeof(buf), "%u", value);
		gtk_label_set_text(GTK_LABEL(label_reg_hex_value), buf);
		return;
	}
	value = gtk_spin_button_get_value(btn);
	block_bit_option_signals();
	update_regmap(value);
	unblock_bit_option_signals();
}

static void spin_or_combo_changed_cb(GtkSpinButton *spinbutton,
					gpointer user_data)
{
	int i;
	int k;
	int opt_val = 0;
	int spin_val = 0;
	int offs = 0;
	int width = 0;
	gchar buf[10];

	for (i = 0; i < soft_reg.bgroup_cnt; i++){
		offs = soft_reg.bgroup_list[i].offset;
		width = soft_reg.bgroup_list[i].width;
		if (GTK_IS_SPIN_BUTTON(spinbutton)){
				if (spinbutton == (GtkSpinButton *)bit_spinbuttons[offs]){
					opt_val = (int)gtk_spin_button_get_value(spinbutton);
					opt_val = opt_val << offs;
					break;
				}
		} else {
			if ((GtkComboBoxText *)spinbutton == (GtkComboBoxText *)bit_comboboxes[offs]){
				k = gtk_combo_box_get_active((GtkComboBox *)spinbutton);
				opt_val = soft_reg.bgroup_list[i].option_list[k].value;
				opt_val = opt_val << offs;
				break;
			}
		}
	}
	spin_val = gtk_spin_button_get_value((GtkSpinButton *)spin_btn_reg_value);
	spin_val = spin_val & ~(((1ul << width) - 1) << offs);
	spin_val = spin_val | opt_val;
	g_signal_handler_block(spin_btn_reg_value, reg_val_hid);
	gtk_spin_button_set_value((GtkSpinButton *)spin_btn_reg_value, spin_val);
	g_signal_handler_unblock(spin_btn_reg_value, reg_val_hid);
	snprintf(buf, sizeof(buf), "%d", spin_val);
	gtk_label_set_text((GtkLabel *)label_reg_hex_value, buf);
}

static void detailed_regmap_toggled_cb(GtkToggleButton *btn, gpointer data)
{
	char *current_device;

	destroy_regmap_widgets();
	destroy_device_context();
	clean_gui_reg_info();

	current_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combobox_device_list));
	if (g_strcmp0("None\0", current_device)) {
		dev = iio_context_find_device(ctx, current_device);
		debug_register_section_init(dev);
	}
	g_free(current_device);
}

static void reg_map_type_changed_cb(GtkComboBox box, gpointer data)
{
	detailed_regmap_toggled_cb(GTK_TOGGLE_BUTTON(toggle_detailed_regmap), NULL);
}

static void debug_panel_destroy_cb(GObject *object, gpointer user_data)
{
	destroy_device_context();
}

static gint spinbtn_input_cb(GtkSpinButton *btn, gpointer new_value, gpointer data)
{
	gdouble value;
	const char *entry_buf;

	entry_buf = gtk_entry_get_text(GTK_ENTRY(btn));
	value = g_strtod(entry_buf , NULL);
	*((gdouble *)new_value) = value;

	return TRUE;
}

static gboolean spinbtn_output_cb(GtkSpinButton *spin, gpointer data)
{
	GtkAdjustment *adj;
	gchar *text;
	unsigned value;

	adj = gtk_spin_button_get_adjustment(spin);
	value = (unsigned)gtk_adjustment_get_value(adj);
	text = g_strdup_printf("0x%X", value);
	gtk_entry_set_text(GTK_ENTRY(spin), text);
	g_free(text);

	return TRUE;
}

/******************************************************************************/
/*************************** Functions definitions ****************************/
/******************************************************************************/

/*
 * Allocate memory for widgets that draw register map.
 */
static int alloc_widget_arrays(int reg_length)
{
	lbl_bits = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	hboxes = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	vboxes = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	bit_descrip_list = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	elem_frames = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	bit_comboboxes = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	bit_no_read_lbl = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	bit_spinbuttons = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	bit_spin_adjustments = (GtkWidget **)malloc(sizeof(GtkWidget *) * reg_length);
	combo_hid_list = (gulong*)malloc(sizeof(gulong) * reg_length);
	spin_hid_list = (gulong*)malloc(sizeof(gulong) * reg_length);

	return 0;
}

/*
 * Free memory allocated by alloc_widget_arrays().
 */
static void free_widget_arrays(void)
{
	free(lbl_bits);
	free(hboxes);
	free(vboxes);
	free(bit_descrip_list);
	free(elem_frames);
	free(bit_comboboxes);
	free(bit_no_read_lbl);
	free(bit_spinbuttons);
	free(bit_spin_adjustments);
	free(combo_hid_list);
	free(spin_hid_list);
}

/*
 * Allocate memory for a reg structure and fill it with data read from the xml
 * file.
 */
static void fill_soft_register(reg *p_reg, int reg_index)
{
	xmlNodePtr crtnode;
	xmlNodeSetPtr nodeset;
	xmlNodePtr *bit_list;
	xmlNodePtr *opt_list;
	bgroup *p_bit;
	option *p_option;
	int i, j;

	/* Increment this flag every function call */
	soft_reg_exists++;

	/* Get the root that points to the list of the registers from the xml file. */
	nodeset = register_list->nodesetval;

	/* Get the name, notes and width of the register. */
	p_reg->name = read_string_element(xml_doc, nodeset->nodeTab[reg_index], "Description");
	p_reg->notes = read_string_element(xml_doc, nodeset->nodeTab[reg_index], "Notes");
	p_reg->width = read_string_element(xml_doc, nodeset->nodeTab[reg_index], "Width");

	/* Get the "BitFields" reference */
	crtnode = get_child_by_name(nodeset->nodeTab[reg_index], "BitFields");

	/* Get the reference list of the bit groups */
	bit_list = get_children_by_name(crtnode, "BitField", &p_reg->bgroup_cnt);

	/* Allocate space for the found bit fields(bit groups) */
	p_reg->bgroup_list = (bgroup *)malloc(sizeof(bgroup) * p_reg->bgroup_cnt);

	/* Go through all bit groups and get information: description, width, offset, options, etc. */
	for (i = 0; i < p_reg->bgroup_cnt; i++){
		p_bit = &p_reg->bgroup_list[i];
		p_bit->name = read_string_element(xml_doc, bit_list[i], "Description");
		p_bit->width = read_integer_element(xml_doc, bit_list[i], "Width");
		p_bit->offset = read_integer_element(xml_doc, bit_list[i], "RegOffset");
		p_bit->def_val = read_integer_element(xml_doc, bit_list[i], "DefaultValue");
		p_bit->access = read_string_element(xml_doc, bit_list[i], "Access");
		p_bit->description = read_string_element(xml_doc, bit_list[i], "Notes");

		/* Get the "Options" reference if exist. Else go to the next bit group. */
		crtnode = get_child_by_name(bit_list[i], "Options");
		if (crtnode == NULL){
			p_bit->options_cnt = 0;
			p_bit->option_list = NULL;
			continue;
		}
		/* Get the reference list of all options */
		opt_list = get_children_by_name(crtnode, "Option", &p_bit->options_cnt);

		/* Allocate space for the found options */
		p_bit->option_list = (option *)malloc(sizeof(option) * p_bit->options_cnt);
		for (j = 0; j < p_bit->options_cnt; j++) {
			p_option = &p_bit->option_list[j];
			p_option->text = read_string_element(xml_doc, opt_list[j], "Description");
			p_option->value = read_integer_element(xml_doc, opt_list[j], "Value");
		}
		free(opt_list); /* This list options is no longer required */
	}
	free(bit_list); /* List of bit groups is no longer required */

	/* Calculate the register default value using the bits default values */
	p_reg->def_val = get_reg_default_val(p_reg);
}

/*
 * Free the memory allocated by fill_soft_register.
 */
static void free_soft_register(reg *p_reg)
{
	bgroup* p_bit;
	int i, j;

	/* Free memory only when a call of fill_soft_register() already occurred */
	if (soft_reg_exists == 0)
		return;

	soft_reg_exists--;

	/* xmlFree() is used to free all strings allocated by read_string_element() function */
	for (i = 0; i < p_reg->bgroup_cnt; i++){
		p_bit = &p_reg->bgroup_list[i];
		for (j = 0; j < p_bit->options_cnt; j++){
			xmlFree((xmlChar *)p_bit->option_list[j].text);
		}
		free(p_bit->option_list);
		p_bit->def_val = 0;
		p_bit->options_cnt = 0;
		p_bit->width = 0;
		p_bit->offset = 0;
		xmlFree((xmlChar *)p_bit->description);
		xmlFree((xmlChar *)p_bit->access);
		xmlFree((xmlChar *)p_bit->name);
	}
	free(p_reg->bgroup_list);
	p_reg->def_val = 0;
	p_reg->bgroup_cnt = 0;
	xmlFree(p_reg->width);
	xmlFree(p_reg->notes);
	xmlFree(p_reg->name);
}

/*
 * Display all register related information.
 */
static int display_reg_info(int pos_reg_addr)
{
	/* Free the previously used register */
	free_soft_register(&soft_reg);

	/* Display no register information when address is not valid */
	if (pos_reg_addr < 0){
		draw_reg_map(0);
		return 0;
	}
	/* Extract data from xml file and fill a "reg" structure with it */
	fill_soft_register(&soft_reg, pos_reg_addr);

	/* Display the register map using data from the "reg" structure */
	draw_reg_map(1);

	return 0;
}

/*
 * Block comboboxes and spinbuttons handlers.
 */
static void block_bit_option_signals(void)
{
	int i = 0;

	for (; i < reg_bit_width; i++) {
		g_signal_handler_block(bit_comboboxes[i], combo_hid_list[i]);
		g_signal_handler_block(bit_spinbuttons[i], spin_hid_list[i]);
	}
}

/*
 * Unblock comboboxes and spinbuttons handlers.
 */
static void unblock_bit_option_signals(void)
{
	int i = 0;

	for (; i < reg_bit_width; i++) {
		g_signal_handler_unblock(bit_comboboxes[i], combo_hid_list[i]);
		g_signal_handler_unblock(bit_spinbuttons[i], spin_hid_list[i]);
	}
}

/*
 * Read the default values of the bits within a register and calculate the
 * default value of the register.
 */
 static int get_reg_default_val(reg *p_reg)
 {
	 int i = 0;
	 int def_val = 0;
	 bgroup *p_bit;

	 for (; i < p_reg->bgroup_cnt; i++) {
		 p_bit = &p_reg->bgroup_list[i];
		 def_val |= p_bit->def_val << p_bit->offset;
	 }

	 return def_val;
 }

/*
 * Search for a value from the option list and return the position inside the
 * list.
 */
static int get_option_index(int option_value, bgroup *bit)
{
	int i;

	for (i = 0; i < bit->options_cnt; i++)
		if (bit->option_list[i].value == option_value)
			return i;

	return -1;
}

/*
 * Helper function. Remove all elements of a combobox.
 */
static void gtk_combo_box_text_remove_all (GtkWidget *combo_box)
{
	GtkListStore *store;

	store = GTK_LIST_STORE (gtk_combo_box_get_model (GTK_COMBO_BOX (combo_box)));
	gtk_list_store_clear (store);
}

/*
 * Sort strings naturally
 *
 * This function will sort strings naturally, this means when a number is
 * encountered in both strings at the same offset it is compared as a number
 * rather than comparing the individual digits.
 *
 * This makes sure that e.g. in_voltage9 is placed before in_voltage10
 */
static gint combo_box_sort_natural(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
	gchar *s1, *s2;
	int cmp_ret;

	gtk_tree_model_get(model, a, 0, &s1, -1);
	gtk_tree_model_get(model, b, 0, &s2, -1);

	cmp_ret = str_natural_cmp(s1, s2);

	g_free(s1);
	g_free(s2);

	return cmp_ret;
}

/*
 * Sort all elements of a GtkComboBoxText column in GTK_SORT_ASCENDING order or
 * GTK_SORT_DESCENDING order.
 */
static void combo_box_text_sort(GtkComboBoxText *box, int column, int order)
{
	GtkTreeSortable *sortable;

	sortable = GTK_TREE_SORTABLE(gtk_combo_box_get_model(GTK_COMBO_BOX(box)));

	gtk_tree_sortable_set_sort_column_id(sortable, column, GTK_SORT_ASCENDING);
	gtk_tree_sortable_set_sort_func(sortable, column, combo_box_sort_natural, NULL, NULL);
}
/*
 * Set the active text of a GtkComboBoxText to the one desired by the user.
 * Returns true is the operation is successful and false otherwise.
 */
static bool combo_box_text_set_active_text(GtkComboBoxText *box, const char *text)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean has_iter;
	gchar *item;

	model = gtk_combo_box_get_model(GTK_COMBO_BOX(box));
	has_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (has_iter) {
		gtk_tree_model_get(model, &iter, 0, &item, -1);
		if (strcmp(text, item) == 0) {
			gtk_combo_box_set_active_iter(GTK_COMBO_BOX(box), &iter);
			g_free(item);
			return true;
		}
		g_free(item);
		has_iter = gtk_tree_model_iter_next(model, &iter);
	}

	return false;
}

/*
 * Adds a default text element to a GtkComboBoxText that is empty and the sets
 * the default text which sould have id 0 to be active.
 */
static void combo_box_text_add_default_text(GtkComboBoxText *box, const char *text)
{
	gtk_combo_box_text_append_text(box, text);
	gtk_combo_box_set_active(GTK_COMBO_BOX(box), 0);
}

/*
 * Given the number of bit groups, their widths and offsets, the function
 * rearranges the boxes and labels so that the register map will look the same
 * with the one from the datasheet.
 */
static void draw_reg_map(int valid_register)
{
	int i, j;
	reg    *p_reg;
	bgroup *p_bit;
	option *p_option;
	const gchar *label_str;
	char buf[12];
	GtkRequisition r;

	block_bit_option_signals();
	/* Reset all bits to the "reseverd" status and clear all options */
	for (i = (reg_bit_width - 1); i >= 0; i--){
		gtk_widget_reparent(lbl_bits[i], hboxes[i]);
		gtk_label_set_text((GtkLabel *)bit_descrip_list[i], "Reserved");
		gtk_label_set_width_chars((GtkLabel *)bit_descrip_list[i], 13);
		gtk_combo_box_text_remove_all(bit_comboboxes[i]);
		g_object_set(bit_comboboxes[i], "sensitive", TRUE, NULL);
		gtk_widget_hide(bit_comboboxes[i]);
		gtk_widget_hide(bit_spinbuttons[i]);
		gtk_widget_set_tooltip_text(vboxes[i], "");
	}

	/* Dispaly register information */
	if (valid_register){
		snprintf(buf, sizeof(buf), "0x%X", soft_reg.def_val);
		gtk_label_set_text((GtkLabel *)label_reg_descrip, (gchar *)soft_reg.name);
		gtk_label_set_text((GtkLabel *)label_reg_def_val, (gchar *)buf);
		gtk_label_set_text((GtkLabel *)label_reg_notes, (gchar *)soft_reg.notes);
		gtk_widget_show(label_reg_notes);
		gtk_widget_show(label_reg_descrip);
		gtk_widget_show(label_reg_def_val);
		if (strlen(soft_reg.notes) > 0)
			gtk_widget_show(label_notes_tag);
		else
			gtk_widget_hide(label_notes_tag);
	} else {
		gtk_label_set_text((GtkLabel *)label_reg_descrip, (gchar *)"");
		gtk_label_set_text((GtkLabel *)label_reg_def_val, (gchar *)"");
		gtk_label_set_text((GtkLabel *)label_reg_notes, (gchar *)"");
		gtk_widget_show_all(reg_map_container);
		gtk_widget_hide(label_reg_notes);
		gtk_widget_hide(label_notes_tag);
		gtk_widget_hide(label_reg_descrip);
		gtk_widget_hide(label_reg_def_val);
		unblock_bit_option_signals();
		return;
	}
	gtk_widget_show_all(reg_map_container);

	/* Start rearranging boxes and labels to form bit groups */
	p_reg = &soft_reg;
	for (i = 0; i < p_reg->bgroup_cnt; i++){
		p_bit = &p_reg->bgroup_list[i];
		for (j = (p_bit->width - 1); j >= 0 ; j--){
			gtk_widget_reparent(lbl_bits[p_bit->offset + j], hboxes[p_bit->offset]);
			gtk_box_reorder_child((GtkBox *)hboxes[p_bit->offset], lbl_bits[p_bit->offset + j], -1);
			if (j > 0)
				gtk_widget_hide(elem_frames[p_bit->offset + j]);
		}
		/* Set description labels properties */
		gtk_label_set_justify((GtkLabel *)bit_descrip_list[p_bit->offset], GTK_JUSTIFY_CENTER);
		gtk_label_set_text((GtkLabel *)bit_descrip_list[p_bit->offset], p_bit->name);
		gtk_label_set_width_chars((GtkLabel *)bit_descrip_list[p_bit->offset], 13 * p_bit->width);

		/* Add the bit group options */
		for (j = 0; j < p_bit->options_cnt; j++){
			p_option = &p_bit->option_list[j];
			gtk_combo_box_text_append_text((GtkComboBoxText *)(bit_comboboxes[p_bit->offset]), p_option->text);
		}

		/* Bit groups without option get a spin button(except Reserved bits) and bit groups with options get a combo box*/
		if (!xmlStrcmp((xmlChar *)p_bit->name, (xmlChar *)"Reserved to 1")){
			gtk_combo_box_text_append_text((GtkComboBoxText *)(bit_comboboxes[p_bit->offset]), (gchar *)"1");
			gtk_combo_box_set_active((GtkComboBox *)(bit_comboboxes[p_bit->offset]), 0);
			g_object_set(bit_comboboxes[p_bit->offset], "sensitive", FALSE, NULL);
			gtk_widget_show(bit_comboboxes[p_bit->offset]);

		} else if (p_bit->options_cnt > 0){
			/* Set the combo box active value to the default value of the bit group */
			gtk_combo_box_set_active((GtkComboBox *)(bit_comboboxes[p_bit->offset]), get_option_index(p_bit->def_val, p_bit));
			gtk_widget_show(bit_comboboxes[p_bit->offset]);
		} else {
			/* Configure the adjustment according with the bit group size */
			gtk_adjustment_set_upper((GtkAdjustment *)bit_spin_adjustments[p_bit->offset], (1ul << p_bit->width) - 1);
			gtk_adjustment_set_value((GtkAdjustment *)bit_spin_adjustments[p_bit->offset], 0);
			/* Set the spin button value to the default value of the bit group */
			gtk_spin_button_set_value((GtkSpinButton *)bit_spinbuttons[p_bit->offset], p_bit->def_val);

			/* Replace combo box with spin button */
			gtk_widget_hide(bit_comboboxes[p_bit->offset]);
			gtk_widget_show(bit_spinbuttons[p_bit->offset]);
		}

		/* Add tooltips with bit descriptions */
		gtk_widget_set_tooltip_text(vboxes[p_bit->offset], p_bit->description);
	}

	/* Set default value of the combobox to 0 for all bits with "Reserved" tag. Also inactivate the combobox */
	for (i = 0; i < reg_bit_width; i++){
		label_str = gtk_label_get_text((GtkLabel *)bit_descrip_list[i]);
		if (!xmlStrcmp((xmlChar *)label_str, (xmlChar *)"Reserved")){
			gtk_combo_box_text_append_text((GtkComboBoxText *)(bit_comboboxes[i]), (gchar *)"0");
			gtk_combo_box_set_active((GtkComboBox *)(bit_comboboxes[i]), 0);
			g_object_set(bit_comboboxes[i], "sensitive", FALSE, NULL);
			gtk_widget_show(bit_comboboxes[i]);
		}
	}

	/* Set the height of the regmap scrolled window. */
	gtk_widget_size_request(reg_map_container, &r);
	gtk_widget_set_size_request(scrollwin_regmap, -1, r.height);

	unblock_bit_option_signals();
}

/*
 * Show bit options. Hide "Not read" labels.
 */
static void reveal_reg_map(void)
{
	int i = 0;
	reg    *p_reg;
	bgroup *p_bit;
	const gchar *label_str;

	p_reg = &soft_reg;
	for (; i < p_reg->bgroup_cnt; i++){
		p_bit = &p_reg->bgroup_list[i];
		if (!xmlStrcmp((xmlChar *)p_bit->name, (xmlChar *)"Reserved to 1"))
			gtk_widget_show(bit_comboboxes[p_bit->offset]);
		else if (p_bit->options_cnt > 0)
			gtk_widget_show(bit_comboboxes[p_bit->offset]);
		else
			gtk_widget_show(bit_spinbuttons[p_bit->offset]);
		gtk_widget_hide(bit_no_read_lbl[p_bit->offset]);
	}
	for (i = 0; i < reg_bit_width; i++){
		label_str = gtk_label_get_text((GtkLabel *)bit_descrip_list[i]);
		if (!xmlStrcmp((xmlChar *)label_str, (xmlChar *)"Reserved"))
			gtk_widget_show(bit_comboboxes[i]);
		gtk_widget_hide(bit_no_read_lbl[i]);
	}
}

/*
 * Hide bit options. Show "Not read" labels.
 */
static void hide_reg_map(void)
{
	int i = 0;
	reg    *p_reg;
	bgroup *p_bit;
	const gchar *label_str;

	p_reg = &soft_reg;
	for (; i < p_reg->bgroup_cnt; i++){
		p_bit = &p_reg->bgroup_list[i];
		if (!xmlStrcmp((xmlChar *)p_bit->name, (xmlChar *)"Reserved to 1"))
			gtk_widget_hide(bit_comboboxes[p_bit->offset]);
		else if (p_bit->options_cnt > 0)
			gtk_widget_hide(bit_comboboxes[p_bit->offset]);
		else
			gtk_widget_hide(bit_spinbuttons[p_bit->offset]);
		gtk_widget_show(bit_no_read_lbl[p_bit->offset]);
	}
	for (i = 0; i < reg_bit_width; i++){
		label_str = gtk_label_get_text((GtkLabel *)bit_descrip_list[i]);
		if (!xmlStrcmp((xmlChar *)label_str, (xmlChar *)"Reserved"))
			gtk_widget_hide(bit_comboboxes[i]);
	}
}

/*
 * Create all boxes and labels required for displaying the register bit map.
 */
static void create_reg_map(void)
{
	gchar str_bit_field[10];
	short i;

	gtk_label_set_line_wrap((GtkLabel *)label_reg_descrip, TRUE);
	gtk_label_set_line_wrap((GtkLabel *)label_reg_notes, TRUE);
	gtk_label_set_width_chars((GtkLabel *)label_reg_descrip, -1);
	gtk_label_set_width_chars((GtkLabel *)label_reg_notes, 110);

	hbox_bits_container = gtk_hbox_new(FALSE, 0);
	gtk_container_add((GtkContainer *)reg_map_container,
							hbox_bits_container);

	for (i = (reg_bit_width - 1); i >= 0; i--){
		elem_frames[i] = gtk_frame_new("");
		gtk_container_add((GtkContainer *)hbox_bits_container,
								elem_frames[i]);
		vboxes[i] = gtk_vbox_new(FALSE, 0);
		gtk_container_add((GtkContainer *)elem_frames[i], vboxes[i]);

		hboxes[i] = gtk_hbox_new(FALSE, 0);
		gtk_box_pack_start((GtkBox *)vboxes[i], hboxes[i], FALSE,
								FALSE, 0);
		snprintf(str_bit_field, 10, "Bit %d", i);
		lbl_bits[i] = gtk_label_new((gchar *)str_bit_field);
		gtk_widget_set_size_request(lbl_bits[i], 50, -1);
		gtk_box_pack_start((GtkBox *)hboxes[i], lbl_bits[i], TRUE, FALSE,
									0);
		gtk_misc_set_alignment((GtkMisc *)lbl_bits[i], 0.5, 0.0);
		bit_descrip_list[i] = gtk_label_new("Reserved");
		gtk_box_pack_start((GtkBox *)vboxes[i], bit_descrip_list[i], TRUE, TRUE,
									0);
		bit_comboboxes[i] = gtk_combo_box_text_new();
		gtk_widget_set_size_request(bit_comboboxes[i], 50, -1);
		gtk_box_pack_start((GtkBox *)vboxes[i], bit_comboboxes[i],
							FALSE, FALSE, 0);
		gtk_widget_set_no_show_all(bit_comboboxes[i], TRUE);
		bit_spin_adjustments[i] = (GtkWidget *)gtk_adjustment_new(0, 0, 0, 1, 0, 0);

		bit_spinbuttons[i] = gtk_spin_button_new((GtkAdjustment *)bit_spin_adjustments[i], 1, 0);
		gtk_entry_set_alignment((GtkEntry *)bit_spinbuttons[i], 1);
		gtk_box_pack_start((GtkBox *)vboxes[i], bit_spinbuttons[i],
							FALSE, FALSE, 0);
		gtk_widget_set_no_show_all(bit_spinbuttons[i], TRUE);

		bit_no_read_lbl[i] = gtk_label_new("Not Read");
		gtk_widget_set_size_request(bit_no_read_lbl[i], 50, -1);
		gtk_box_pack_start((GtkBox *)vboxes[i], bit_no_read_lbl[i],
							FALSE, FALSE, 0);
		gtk_misc_set_padding((GtkMisc *)bit_no_read_lbl[i], 0, 10);

		gtk_label_set_line_wrap((GtkLabel *)bit_descrip_list[i], TRUE);
		gtk_misc_set_alignment((GtkMisc *)bit_descrip_list[i], 0.5, 0.5);
		gtk_misc_set_padding((GtkMisc *)bit_descrip_list[i], 0, 10);

		combo_hid_list[i] = g_signal_connect(G_OBJECT(bit_comboboxes[i]),
			"changed", G_CALLBACK(spin_or_combo_changed_cb), NULL);
		spin_hid_list[i] = g_signal_connect(G_OBJECT(bit_spinbuttons[i]),
			"changed", G_CALLBACK(spin_or_combo_changed_cb), NULL);
	}
}

static void clean_gui_reg_info(void)
{
	gtk_label_set_text(GTK_LABEL(label_reg_descrip), "");
	gtk_label_set_text(GTK_LABEL(label_reg_def_val), "");
	gtk_label_set_text(GTK_LABEL(label_reg_notes), "");
	gtk_widget_hide(label_notes_tag);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_addr), (gdouble)0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin_btn_reg_value), (gdouble)0);
	gtk_label_set_text(GTK_LABEL(label_reg_hex_addr), "0x0000");
	gtk_label_set_text(GTK_LABEL(label_reg_hex_value), "<unknown>");
	gtk_widget_set_size_request(scrollwin_regmap, -1, -1);
}

/*
 * Find the bit width of the registers.
 */
static int get_default_reg_width(void)
{

	xmlNodePtr node;
	int default_width;

	node = root->xmlChildrenNode;
	while (node != NULL){
		if (!xmlStrcmp(node->name, (xmlChar *)"Register"))
			break;
	node = node->next;
	}
	if (node == NULL)
		return 0;
	/* Now node is pointing to the first <Register> that holds the <Width> element. */
	default_width = read_integer_element(xml_doc, node, "Width");

	return default_width;
}

/*
 * Get the list of the public registers of a ADI part.
 */
static int * get_reg_addr_list(void)
{
	xmlNodeSetPtr nodeset;
	char *reg_addr;
	int *list;
	int i = 0;

	nodeset = register_list->nodesetval;
	reg_list_size = nodeset->nodeNr;
	list = malloc(sizeof(*list) * reg_list_size);
	if (list == NULL){
		printf("Memory allocation failed\n");
		return NULL;
	}

	for (; i < nodeset->nodeNr; i++){
		reg_addr = read_string_element(xml_doc, nodeset->nodeTab[i], "Address");
		sscanf(reg_addr, "%x", (int *)(list + i));
		xmlFree((xmlChar *)reg_addr);
	}

	return list;
}

/*
 * Find the register position in the given list.
 */
static int get_reg_pos(int *regList, int reg_addr)
{
	int i = 0;

	for (; i < reg_list_size; i++){
		if (regList[i] == reg_addr)
			return i;
	}

	return -1;
}

/*
 * Update bit options using raw data read from the device or provided by user.
 * Check if options exists for the provided data and reconstruct data from only
 * the options that exist.
 * Retrurn - the actual data that can be set.
 */
static int update_regmap(int data)
{
    int i;
    int bg_value; /* bit group value */
    int mask;
    int opt_pos;
    int new_data = 0;
    reg *p_reg;
    bgroup *p_bit;

    p_reg = &soft_reg;
    for (i = 0; i < p_reg->bgroup_cnt; i++){
        p_bit = &p_reg->bgroup_list[i];
        mask = (1ul << p_bit->width) - 1;
        mask = mask << p_bit->offset;
        bg_value = (data & mask) >> p_bit->offset;
        if (p_bit->options_cnt > 0){
		opt_pos = get_option_index(bg_value, p_bit);
		if (opt_pos == -1)
			opt_pos = 0;
		gtk_combo_box_set_active((GtkComboBox *)(bit_comboboxes[p_bit->offset]), opt_pos);
		new_data |= p_bit->option_list[opt_pos].value << p_bit->offset;
        } else {
		if (xmlStrcmp((xmlChar *)p_bit->name, (xmlChar *)"Reserved to 1")) {
			gtk_spin_button_set_value((GtkSpinButton *) bit_spinbuttons[p_bit->offset], bg_value);
			new_data |= bg_value << p_bit->offset;
		} else {
			new_data |= 1ul << p_bit->offset;
		}
        }
    }

    return new_data;
}

/*
 * Search for xml files that contain the name of the device within their
 * filename.
 * Fill parameter "filename" with the name of the found xml file or fill with
 * empty string otherwise.
 */
static void device_xml_file_selection(const char *device_name, char *filename)
{
	struct iio_device *iio_dev = iio_context_find_device(ctx, device_name);

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(reg_map_type)) == REG_MAP_SPI) {
		/* Find the device corresponding xml file */
		find_device_xml_file(xmls_folder_path, (char *)device_name, filename);
	} else if (gtk_combo_box_get_active(GTK_COMBO_BOX(reg_map_type)) == REG_MAP_AXI_CORE) {
		char *adc_regmap_name, *dac_regmap_name;
		int pcore_major;

		if (pcore_get_version(device_name, &pcore_major) && pcore_major > 8) {
			adc_regmap_name = g_strdup_printf("adi_regmap_adc_v%d.xml", pcore_major);
			dac_regmap_name = g_strdup_printf("adi_regmap_dac_v%d.xml", pcore_major);
		} else {
			adc_regmap_name = g_strdup("adi_regmap_adc.xml");
			dac_regmap_name = g_strdup("adi_regmap_dac.xml");
		}
		/* Attempt to associate AXI Core ADC xml or AXI Core DAC xml to the device */
		if (is_input_device(iio_dev) && xml_file_exists(adc_regmap_name))
			sprintf(filename, "%s", adc_regmap_name);
		else if (is_output_device(iio_dev) && xml_file_exists(dac_regmap_name))
			sprintf(filename, "%s", dac_regmap_name);

		if (adc_regmap_name)
			g_free(adc_regmap_name);
		if (dac_regmap_name)
			g_free(dac_regmap_name);
	} else {
		filename[0] = '\0';
	}
}

/*
 * Read data from xml file and initialise the context of the device
 */
static int device_xml_file_load(char *filename)
{
	char *temp_path;
	int ret = 0;

	temp_path = malloc(strlen(xmls_folder_path) + strlen(filename) + 2);
	if (!temp_path) {
		printf("Failed to allocate memory with malloc\n");
		return -1;
	}
	sprintf(temp_path, "%s/%s", xmls_folder_path, filename);
	xml_doc = open_xml_file(temp_path, &root);
	if (xml_doc) {
		xml_file_opened = 1;
		create_device_context();
		g_signal_emit_by_name(spin_btn_reg_addr, "value-changed");
	} else {
		printf("Cannot load the file %s\n", temp_path);
		ret = -1;
	}
	free(temp_path);

	return ret;
}

/*
 * Check if the given file exists.
 * The function also prefixes the file name with the xml directory path.
 */
static bool xml_file_exists(const char *filename)
{
	char *temp_path;
	bool exists;

	temp_path = malloc(strlen(xmls_folder_path) + strlen(filename) + 2);
	if (!temp_path) {
		printf("Failed to allocate memory with malloc\n");
		return false;
	}
	sprintf(temp_path, "%s/%s", xmls_folder_path, filename);

	exists = !access(temp_path, F_OK);
	free(temp_path);
	return exists;
}

/*
 * Initialize GUI and all data for the register section of the debug plugin
 */
static void debug_register_section_init(struct iio_device *iio_dev)
{
	char xml_filename[128] = { 0 };

	if (!iio_dev)
		return;

	if (iio_device_get_debug_attrs_count(dev) > 0) {
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle_detailed_regmap))) {
			device_xml_file_selection(iio_device_get_name(iio_dev), xml_filename);
			if (xml_filename[0])
				device_xml_file_load(xml_filename);
		}
		gtk_widget_set_sensitive(register_section, true);
	} else {
#ifdef __linux__
		int id = getuid();
		if (id != 0) {
			gtk_widget_show(warning_label);
			gtk_widget_set_sensitive(register_section, false);
		}
#endif
	}
}

/*
 * Initialize the combobox that is used for register map selection.
 * Not all combobox options should be available for every device.
 */
static void reg_map_chooser_init(struct iio_device *dev)
{
	static const char *spi_option = "SPI";
	static const char *axi_option = "AXI_CORE";

	if (!dev)
		return;

	g_signal_handler_block(reg_map_type, reg_map_hid);

	gtk_combo_box_text_remove_all(reg_map_type);
	combo_box_text_add_default_text(GTK_COMBO_BOX_TEXT(reg_map_type), spi_option);
	if (is_input_device(dev) || is_output_device(dev))
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(reg_map_type), axi_option);

	g_signal_handler_unblock(reg_map_type, reg_map_hid);
}

/*
 * Create a context for the selected ADI device. Retrieve a list of all
 * registers and their addresses. Find the width that applies to all registers.
 * Allocate memory for all lists of widgets. Create new widgets and draw a
 * register map.
 */
static void create_device_context(void)
{
	if (!context_created) {
		/* Search for all elements called "Register" */
		register_list = retrieve_all_elements(xml_doc, "//Register");
		reg_addr_list = get_reg_addr_list();
		/* Init data */
		reg_bit_width = get_default_reg_width();
		if (reg_bit_width) {
			alloc_widget_arrays(reg_bit_width);
			create_reg_map();
			context_created = 1;
		}
	}
}

/*
 * Destroy a context for the selected ADI device. Free memory allocated for
 * all widget lists. Close xml file of the ADI device.
 */
static void destroy_device_context(void)
{
/* Free resources */
	if (context_created == 1) {
		free(reg_addr_list);
		free_widget_arrays();
		free_soft_register(&soft_reg);
		xmlXPathFreeObject(register_list);
		close_xml_file(xml_doc);
		xml_file_opened = 0;
		reg_bit_width = 0;
	}
	context_created = 0;
}

/*
 * Destroy all widgets within the register map.
 */
static void destroy_regmap_widgets(void)
{
	/* Remove all widgets (and their children) that create regmap */
	if (context_created == 1)
		gtk_widget_destroy(hbox_bits_container);
}

#define PCORE_VERSION_MAJOR(version) (version >> 16)

/*
 * Find the PCORE major version.
 * Return true on success and false otherwise.
 */
static bool pcore_get_version(const char *dev_name, int *major)
{
	struct iio_device *dev;
	int ret;

	if (!ctx) {
		fprintf(stderr, "Invalid context in %s\n", __func__);
		goto fail;
	}

	dev = iio_context_find_device(ctx, dev_name);
	if (!dev) {
		fprintf(stderr, "Could not invalid device %s in %s\n",
			dev_name, __func__);
		goto fail;
	}

	uint32_t value, address = 0x80000000;

	ret = iio_device_reg_read(dev, address, &value);
	if (ret < 0) {
		fprintf(stderr, "%s in %s", strerror(ret), __func__);
		goto fail;
	}
	*major = (int)PCORE_VERSION_MAJOR(value);

	return true;

fail:
	return false;
}

/*
 *  Main function
 */
static GtkWidget * debug_init(struct osc_plugin *plugin, GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *debug_panel;
	GtkWidget *vbox_device_list;
	GtkWidget *vbox_scanel;
	DIR *d;

	ctx = osc_create_context();
	if (!ctx)
		return NULL;

	/* Check the local xmls folder first */
	d = opendir("./xmls");
	if (!d) {
		snprintf(xmls_folder_path, sizeof(xmls_folder_path), "%s", OSC_XML_PATH);
	} else {
		closedir(d);
		snprintf(xmls_folder_path, sizeof(xmls_folder_path), "%s", "./xmls");
	}

	builder = gtk_builder_new();
	if (osc_load_glade_file(builder, "debug") < 0)
		return NULL;

	debug_panel = GTK_WIDGET(gtk_builder_get_object(builder, "reg_debug_panel"));
	vbox_device_list = GTK_WIDGET(gtk_builder_get_object(builder, "device_list_container"));
	register_section = GTK_WIDGET(gtk_builder_get_object(builder, "frameRegister"));
	btn_read_reg = GTK_WIDGET(gtk_builder_get_object(builder, "debug_read_reg"));
	btn_write_reg = GTK_WIDGET(gtk_builder_get_object(builder, "debug_write_reg"));
	label_reg_hex_addr = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_address_hex"));
	label_reg_hex_value = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_value_hex"));
	label_reg_descrip = GTK_WIDGET(gtk_builder_get_object(builder, "labelRegDescripText"));
	label_reg_def_val = GTK_WIDGET(gtk_builder_get_object(builder, "label_reg_def_val_txt"));
	label_reg_notes = GTK_WIDGET(gtk_builder_get_object(builder, "labelRegNotesText"));
	label_notes_tag = GTK_WIDGET(gtk_builder_get_object(builder, "labelRegNotes"));
	spin_btn_reg_addr = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_address"));
	spin_btn_reg_value = GTK_WIDGET(gtk_builder_get_object(builder, "debug_reg_value"));
	scrollwin_regmap = GTK_WIDGET(gtk_builder_get_object(builder, "scrolledwindow_regmap"));
	reg_map_container = GTK_WIDGET(gtk_builder_get_object(builder, "regmap_container"));
	warning_label = GTK_WIDGET(gtk_builder_get_object(builder, "label_warning"));
	reg_autoread = GTK_WIDGET(gtk_builder_get_object(builder, "register_autoread"));
	toggle_detailed_regmap = GTK_WIDGET(gtk_builder_get_object(builder, "toggle_detailed_regmap"));
	reg_map_type = GTK_WIDGET(gtk_builder_get_object(builder, "cmb_RegisterMapType"));

	vbox_scanel =  GTK_WIDGET(gtk_builder_get_object(builder, "scanel_container"));
	scanel_read = GTK_WIDGET(gtk_builder_get_object(builder, "debug_read_scan"));
	scanel_write = GTK_WIDGET(gtk_builder_get_object(builder, "debug_write_scan"));
	scanel_value = GTK_WIDGET(gtk_builder_get_object(builder, "debug_scanel_value"));
	scanel_options = GTK_WIDGET(gtk_builder_get_object(builder, "debug_scanel_options"));
	combobox_attr_type = GTK_WIDGET(gtk_builder_get_object(builder, "cmbtxt_attr_type"));
	scanel_filename = GTK_WIDGET(gtk_builder_get_object(builder, "entry_attribute_filename"));

	/* Create comboboxes for the Device List and for the Scan Elements */
	combobox_device_list = gtk_combo_box_text_new();
	combobox_debug_scanel = gtk_combo_box_text_new();
	/* Put in the correct values */
	gtk_box_pack_start((GtkBox *)vbox_device_list, combobox_device_list,
				TRUE, TRUE, 0);
	combo_box_text_add_default_text(GTK_COMBO_BOX_TEXT(combobox_device_list), "None");
	gtk_box_pack_start((GtkBox *)vbox_scanel, combobox_debug_scanel,
				TRUE, TRUE, 0);
	combo_box_text_add_default_text(GTK_COMBO_BOX_TEXT(combobox_attr_type), "None");
	combo_box_text_add_default_text(GTK_COMBO_BOX_TEXT(combobox_debug_scanel), "None");

	/* Fill in device list */
	int nb_devs = iio_context_get_devices_count(ctx);
	int i;

	for (i = 0; i < nb_devs; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		const gchar *dev_name = iio_device_get_name(dev);
		if (!dev_name)
			continue;
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combobox_device_list), dev_name);
	}

	/* Connect signals */
	g_signal_connect(G_OBJECT(debug_panel), "destroy",
			G_CALLBACK(debug_panel_destroy_cb), NULL);
	g_signal_connect(G_OBJECT(btn_read_reg), "clicked",
			G_CALLBACK(reg_read_clicked), NULL);
	g_signal_connect(G_OBJECT(btn_write_reg), "clicked",
			G_CALLBACK(reg_write_clicked), NULL);
	reg_addr_hid = g_signal_connect(G_OBJECT(spin_btn_reg_addr),
		"value-changed", G_CALLBACK(reg_address_value_changed_cb), NULL);
	reg_val_hid = g_signal_connect(G_OBJECT(spin_btn_reg_value),
		"value-changed", G_CALLBACK(reg_value_change_value_cb), NULL);
	g_signal_connect(G_OBJECT(combobox_device_list), "changed",
			G_CALLBACK(debug_device_list_cb), NULL);
	attr_type_hid = g_signal_connect(G_OBJECT(combobox_attr_type), "changed",
				G_CALLBACK(attribute_type_changed_cb), NULL);
	debug_scanel_hid = g_signal_connect(G_OBJECT(combobox_debug_scanel),
			 "changed", G_CALLBACK(debug_scanel_changed_cb), NULL);
	reg_map_hid = g_signal_connect(G_OBJECT(reg_map_type), "changed",
			G_CALLBACK(reg_map_type_changed_cb), NULL);
	g_signal_connect(G_OBJECT(scanel_read), "clicked",
			G_CALLBACK(scanel_read_clicked), NULL);
	g_signal_connect(G_OBJECT(scanel_write), "clicked",
			G_CALLBACK(scanel_write_clicked), NULL);
	g_signal_connect(G_OBJECT(toggle_detailed_regmap), "toggled",
			G_CALLBACK(detailed_regmap_toggled_cb), NULL);
	g_signal_connect(G_OBJECT(spin_btn_reg_addr), "input",
			G_CALLBACK(spinbtn_input_cb), NULL);
	g_signal_connect(G_OBJECT(spin_btn_reg_addr), "output",
			G_CALLBACK(spinbtn_output_cb), NULL);
	g_signal_connect(G_OBJECT(spin_btn_reg_value), "input",
			G_CALLBACK(spinbtn_input_cb), NULL);
	g_signal_connect(G_OBJECT(spin_btn_reg_value), "output",
			G_CALLBACK(spinbtn_output_cb), NULL);

	gtk_widget_show_all(debug_panel);

	return debug_panel;
}

static void context_destroy(struct osc_plugin *plugin, const char *ini_fn)
{
	osc_destroy_context(ctx);
}

static bool debug_identify(const struct osc_plugin *plugin)
{
	/* Use the OSC's IIO context just to detect the devices */
	struct iio_context *osc_ctx = get_context_from_osc();
	return iio_context_get_devices_count(osc_ctx) > 0;
}

struct osc_plugin plugin = {
	.name = "Debug",
	.identify = debug_identify,
	.init = debug_init,
	.destroy = context_destroy,
};
