/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>

#include <iio.h>

#include "compat.h"
#include "fru.h"
#include "osc.h"
#include "config.h"
#include "phone_home.h"

#ifndef GIT_VERSION
#define GIT_VERSION	""
#endif

#ifdef SERIAL_BACKEND
#include <libserialport.h> /* cross platform serial port lib */
#endif

#if defined(FRU_FILES) && !defined(__linux__)
#undef FRU_FILES
#endif

typedef struct _Dialogs Dialogs;
struct _Dialogs
{
	GtkBuilder *builder;
	GtkWidget *about;
	GtkWidget *connect;
	GtkWidget *connect_fru;
	GtkWidget *connect_iio;
	GtkWidget *ctx_info;
	GtkWidget *serial_num;
	GtkWidget *load_save_profile;
	GtkWidget *connect_net;
	GtkWidget *net_ip;
	GtkWidget *connect_usb;
	GtkWidget *connect_usbd;
	GtkWidget *connect_serial;
	GtkWidget *connect_seriald;
	GtkWidget *connect_serialbr;
	GtkWidget *ok_btn;
	GtkWidget *latest_version;
	GtkWidget *ver_progress_window;
	GtkWidget *ver_progress_bar;
};

static Dialogs dialogs;
static GtkWidget *serial_num;
static GtkWidget *fru_date;
static GtkWidget *fru_file_list;
static volatile bool ver_check_done;
static Release *release;

static gchar *usb_pids[128];
static int active_pid = -1;

#ifdef FRU_FILES
static time_t mins_since_jan_1_1996(void)
{
	struct tm j;
	time_t now;

	j.tm_year= 1996 - 1900;
	j.tm_mon= 0;
	j.tm_mday=1;
	j.tm_hour=0;
	j.tm_min= 0;
	j.tm_sec= 0;
	j.tm_isdst=0;

	time(&now);

	return (time_t) (now - (time_t)mktime(&j)) / 60;
}

static size_t write_fru(char *eeprom)
{
	gint result;
	const char *serial, *file;
	char *ser_num, *filename;
	time_t frutime;
	FILE *fp = NULL;
	size_t i;
	time_t tmp;
	struct tm *tmp2;
	char buf[256];
	int j, n;
	struct dirent **namelist;
	GtkListStore *store;

	n = scandir(FRU_FILES, &namelist, 0, alphasort);
	/* No fru files, don't bother */
	if (n < 0) {
		printf("didn't find FRU_Files in %s at %s(%s)\n", FRU_FILES, __FILE__, __func__);
		return 0;
	}

	g_object_set(dialogs.serial_num, "secondary_text", eeprom, NULL);

	filename = g_malloc(PATH_MAX);
	ser_num = malloc(128);
	memset(ser_num, 0, 128);

	fp = fopen(".serialnum", "r");
	if (fp) {
		i = fread(ser_num, 1, 128, fp);
		if (!ferror(fp) && (i == 128 || feof(fp)))
			gtk_entry_set_text(GTK_ENTRY(serial_num), (const gchar*)&ser_num[1]);
		fclose(fp);
	}

	store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(fru_file_list)));
	gtk_list_store_clear(store);

	for (j = 0; j < n; j++) {
		if (is_dirent_reqular_file(namelist[j]) && str_endswith(namelist[j]->d_name, ".bin"))
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fru_file_list), namelist[j]->d_name);
		free(namelist[j]);
	}
	free(namelist);

	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fru_file_list), "Other...");

	gtk_combo_box_set_active(GTK_COMBO_BOX(fru_file_list), ser_num[0]);
	free(ser_num);

	frutime = mins_since_jan_1_1996();
	tmp = min2date(frutime);
	tmp2 = gmtime(&tmp);

	strftime(buf, sizeof(buf), "%a %b %d %H:%M %Y", tmp2);

	gtk_entry_set_text(GTK_ENTRY(fru_date), buf);

get_serial_and_file:
	result = gtk_dialog_run(GTK_DIALOG(dialogs.serial_num));

	i = 0;
	switch (result) {
		case GTK_RESPONSE_OK:
			serial = gtk_entry_get_text(GTK_ENTRY(serial_num));
			if (strlen(serial) == 0) {
				create_blocking_popup(GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
					"", "Serial number required");
				goto get_serial_and_file;
			}

			file = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(fru_file_list));

			if (strncmp(file, "Other...", 8) != 0) {
				snprintf(filename, PATH_MAX, FRU_FILES "%s", file);
			} else {
				/* manually choose fru file */
				GtkWidget *dialog;

				dialog = gtk_file_chooser_dialog_new("Select FRU file",
							GTK_WINDOW(dialogs.serial_num),
							GTK_FILE_CHOOSER_ACTION_OPEN,
							GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
							GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
							NULL);

				if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
					filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
				}
				gtk_widget_destroy(dialog);
			}

			if (filename) {
				fflush(NULL);
				sprintf(buf, "fru-dump -i %s -o %s -s %s -d %d 2>&1",
						filename, eeprom, serial, (unsigned int)frutime);
#if DEBUG
				printf("%s\n", buf);
#else
				fp = popen(buf, "r");
#endif
				if (!fp) {
					printf("can't execute \"%s\"\n", buf);
				} else {
					i = 0;
					while(fgets(buf, sizeof(buf), fp) != NULL){
						/* fru-dump not installed */
						if (strstr(buf, "not found"))
							printf("no fru-tools installed\n");
						if (strstr(buf, "wrote") && strstr(buf, "bytes to") && strstr(buf, eeprom))
							i = 1;
					}
					pclose(fp);
				}
				fp = fopen(".serialnum", "w");
				if (fp) {
					fprintf(fp, "%c%s", gtk_combo_box_get_active(GTK_COMBO_BOX(fru_file_list)), serial);
					fclose(fp);
				}
			}
			break;
		case GTK_RESPONSE_DELETE_EVENT:
			break;
		default:
			printf("unknown response %d in %s\n", result, __func__);
			break;
	}
	gtk_widget_hide(GTK_WIDGET(dialogs.serial_num));

	g_free(filename);
	return i;
}

static int is_eeprom_fru(char *eeprom_file, GtkTextBuffer *buf, GtkTextIter *iter)
{
	FILE *fp;
	unsigned char *raw_input_data = NULL;
	size_t bytes;
	struct FRU_DATA *fru = NULL;
	char text[256];

	fp = fopen(eeprom_file, "rb");
	if(fp == NULL) {
		int errsv = errno;
		printf_err("Cannot open file %s\n%s\n", eeprom_file, strerror(errsv));
		return 0;
	}

	raw_input_data = x_calloc(1, 1024);
	bytes = fread(raw_input_data, 1024, 1, fp);
	fclose(fp);

	/* Since EOF should be reached, bytes should be zero */
	if (!bytes)
		fru = parse_FRU(raw_input_data);

	if (fru) {
		sprintf(text, "Found %s:\n", eeprom_file);
		gtk_text_buffer_insert(buf, iter, text, -1);

		if (fru->Board_Area->manufacturer && fru->Board_Area->manufacturer[0] & 0x3F) {
			sprintf(text, "FMC Manufacture : %s\n", &fru->Board_Area->manufacturer[1]);
			gtk_text_buffer_insert(buf, iter, text, -1);
		}
		if (fru->Board_Area->product_name && fru->Board_Area->product_name[0] & 0x3F) {
			sprintf(text, "Product Name: %s\n", &fru->Board_Area->product_name[1]);
			gtk_text_buffer_insert(buf, iter, text, -1);
		}
		if (fru->Board_Area->part_number && fru->Board_Area->part_number[0] & 0x3F) {
			sprintf(text, "Part Number : %s\n", &fru->Board_Area->part_number[1]);
			gtk_text_buffer_insert(buf, iter, text, -1);
		}
		if (fru->Board_Area->serial_number && fru->Board_Area->serial_number[0] & 0x3F) {
			sprintf(text, "Serial Number : %s\n", &fru->Board_Area->serial_number[1]);
			gtk_text_buffer_insert(buf, iter, text, -1);
		}
		if (fru->Board_Area->mfg_date) {
			time_t tmp = min2date(fru->Board_Area->mfg_date);
			sprintf(text, "Date of Man : %s", ctime(&tmp));
			gtk_text_buffer_insert(buf, iter, text, -1);
		}

		sprintf(text, "\n");
		gtk_text_buffer_insert(buf, iter, text, -1);

		free(fru);
		fru = NULL;

		return 1;
	}
	return 0;
}
#endif /* FRU_FILES */

static bool widget_set_cursor(GtkWidget *widget, GdkCursorType type)
{
	GdkCursor *watchCursor;
	GdkWindow *gdkWindow;

	g_return_val_if_fail(widget, false);

	gdkWindow = gtk_widget_get_window(widget);
	g_return_val_if_fail(gdkWindow, false);

	watchCursor = gdk_cursor_new(type);
	gdk_window_set_cursor(gdkWindow, watchCursor);

	while (gtk_events_pending())
		gtk_main_iteration();


	return true;
}

static bool widget_use_parent_cursor(GtkWidget *widget)
{
	GdkWindow *gdkWindow;

	g_return_val_if_fail(widget, false);

	gdkWindow = gtk_widget_get_window(widget);
	g_return_val_if_fail(gdkWindow, false);

	gdk_window_set_cursor(gdkWindow, NULL);

	return true;
}

static struct iio_context * get_context(Dialogs *data)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialogs.connect_net))) {
		const char *hostname = gtk_entry_get_text(GTK_ENTRY(dialogs.net_ip));
		if (!hostname[0])
			hostname = NULL;

		return iio_create_network_context(hostname);
	} else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialogs.connect_usb))) {
		struct iio_context *ctx;
		gchar *uri = gtk_combo_box_text_get_active_text(
				GTK_COMBO_BOX_TEXT(dialogs.connect_usbd));
		gchar *uri2 = uri + strlen(uri);

		while(*uri2 != '[')
			uri2--;

		/* take off the [] */
		uri2++;
		uri2[strlen(uri2)-1] = 0;

		active_pid = gtk_combo_box_get_active(GTK_COMBO_BOX(dialogs.connect_usbd));

		/* try to open, if fail & busy, it's likely we are the same */

		ctx = iio_create_context_from_uri(uri2);
		if (!ctx && errno == EBUSY &&
				!strcmp("usb", iio_context_get_name(get_context_from_osc()))) {
			return get_context_from_osc();
		}
		return ctx;
#ifdef SERIAL_BACKEND
	} else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialogs.connect_serial))) {
		struct iio_context *ctx;
		gchar *port = gtk_combo_box_text_get_active_text(
				GTK_COMBO_BOX_TEXT(dialogs.connect_seriald));
		gchar *baud_rate = gtk_combo_box_text_get_active_text(
				GTK_COMBO_BOX_TEXT(dialogs.connect_serialbr));

		/* Size is +3: for ':', ',' and '\0' */
		gchar *result = g_strdup_printf("serial:%s,%s", port, baud_rate);

		ctx = iio_create_context_from_uri(result);
		g_free(result);
		if (!ctx && errno == EBUSY &&
				!strcmp("serial", iio_context_get_name(get_context_from_osc()))) {
			return get_context_from_osc();
		}
		return ctx;
#endif
	} else {
		return iio_create_local_context();
	}
}

static void refresh_usb(void)
{
	struct iio_scan_context *ctxs;
	struct iio_context_info **info;
	GtkListStore *liststore;
	ssize_t ret;
	unsigned int i = 0;
	gint index = 0;
	gchar *tmp, *tmp1, *pid, *buf;
	char *current = NULL;

	/* get the active setting (if there is one) */
	if(active_pid != -1 && usb_pids[active_pid])
		current = strdup(usb_pids[active_pid]);

	for(i = 0; i < 127 ; i++) {
		if (usb_pids[i]) {
			free(usb_pids[i]);
			usb_pids[i] = NULL;
		}
	}

	/* clear everything, and scan again */
	liststore = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(dialogs.connect_usbd)));
	gtk_list_store_clear(liststore);

	i = 0;
	ctxs = iio_create_scan_context("usb", 0);
	if (!ctxs)
		goto nope;

	ret = iio_scan_context_get_info_list(ctxs, &info);
	if (ret < 0)
		goto err_free_ctxs;
	if (!ret)
		goto err_free_info_list;

	for (i = 0; i < (size_t) ret; i++) {
		tmp = strdup(iio_context_info_get_description(info[i]));
		pid = strdup(iio_context_info_get_description(info[i]));

		/* skip the PID/VID : xxxx:xxxx */
		tmp1 = strchr(tmp, '(');
		if (tmp1 && strstr(tmp1, ")), serial=")) {
			/* skip the '(' char */
			tmp1++;
			/* find the serial number */
			if (strstr(tmp1, ")), serial=")) {
				tmp1[strstr(tmp1, ")), serial=") - tmp1 + 1] = 0;
				memmove(pid + strlen("xxxx:xxxx "),
					strstr(pid, ")), serial=") + strlen(")), serial="),
					strlen(pid) - (strstr(pid, ")), serial=") - pid -
						strlen(")), serial=")));
			}
		}
		if (active_pid != -1 && current && !strcmp(pid, current)) {
			index = i;
		}
		usb_pids[i]=pid;

		if (!tmp1)
			tmp1 = tmp;
		buf = malloc(strlen(iio_context_info_get_uri(info[i])) +
				strlen(tmp1) + 5);
		sprintf(buf, "%s [%s]", tmp1,
				iio_context_info_get_uri(info[i]));
		tmp1 = NULL;

		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialogs.connect_usbd), buf);
		free(buf);
		free(tmp);
	}

err_free_info_list:
	iio_context_info_list_free(info);
err_free_ctxs:
	iio_scan_context_destroy(ctxs);
nope:

	if (!i) {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialogs.connect_usbd), "None");
		gtk_combo_box_set_active(GTK_COMBO_BOX(dialogs.connect_usbd),0);
		gtk_widget_set_sensitive(dialogs.connect_usbd, false);
		gtk_widget_set_sensitive(dialogs.connect_usb, false);
		return;
	}

	gtk_widget_set_sensitive(dialogs.connect_usb, true);
	gtk_widget_set_sensitive(dialogs.connect_usbd,true);

	gtk_combo_box_set_active(GTK_COMBO_BOX(dialogs.connect_usbd), index);

	if (current) {
		free(current);
		current = NULL;
	}
}

#ifdef SERIAL_BACKEND
static void refresh_serial(GtkBuilder *builder)
{
	GtkListStore *liststore;
	struct sp_port **ports;
	int i;

	/* clear everything, and scan again */
	liststore = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(dialogs.connect_seriald)));
	gtk_list_store_clear(liststore);

	/*get serial ports*/
	enum sp_return error = sp_list_ports(&ports);
	if (error == SP_OK) {
	for (i = 0; ports[i]; i++) {
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialogs.connect_seriald), sp_get_port_name(ports[i]));
	}
		sp_free_port_list(ports);
	} else {
		gtk_combo_box_set_active(GTK_COMBO_BOX(dialogs.connect_seriald),0);
		gtk_widget_set_sensitive(dialogs.connect_seriald, false);
		gtk_widget_set_sensitive(dialogs.connect_serialbr, false);
		gtk_widget_set_sensitive(dialogs.connect_serial, false);
	}
}
#else
static void refresh_serial(GtkBuilder *builder)
{
	/* Serial Backend - hide if not supported */
	gtk_widget_hide(dialogs.connect_seriald);
	gtk_widget_hide(dialogs.connect_serial);
	gtk_widget_hide(dialogs.connect_serialbr);
	gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "connect_serial_label")));
}
#endif

char * usb_get_serialnumber(struct iio_context *context)
{
	const char *name = iio_context_get_name(context);

	if (name && !strcmp(name, "usb") && active_pid >= 0)
		return usb_pids[active_pid];

	return NULL;
}

void usb_set_serialnumber(char * value)
{
	int i;

	/* make sure to fill out usb_pids list */
	if (active_pid == -1)
		refresh_usb();

	for(i = 0; i < 127; i++) {
		if (usb_pids[i] && strstr(usb_pids[i], value)) {
			active_pid = i;
			break;
		}
	}

	/* select the right one in the list */
	refresh_usb();
}


static bool connect_clear(GtkToggleButton *button)
{
	GtkTextBuffer *buf;
	GtkTextIter iter;
	gchar tmp[2] = " ";

	buf = gtk_text_buffer_new(NULL);
	gtk_text_buffer_get_iter_at_offset(buf, &iter, 0);
	gtk_text_buffer_insert(buf, &iter, tmp, -1);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(dialogs.connect_fru), buf);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(dialogs.connect_iio), buf);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(dialogs.ctx_info), buf);
	g_object_unref(buf);

	return true;
}

static bool connect_fillin(Dialogs *data)
{
	GtkTextBuffer *buf;
	GtkTextIter iter;
	char text[256];
	unsigned int num;
	size_t i;
	struct iio_context *ctx;
	const char *desc;

#ifdef FRU_FILES
	char eprom_names[128];
	unsigned char *raw_input_data = NULL;
	FILE *efp, *fp;
	struct stat st;

	/* flushes all open output streams */
	fflush(NULL);
#if DEBUG
	fp = popen("find ./ -name \"fru*.bin\"", "r");
#else
	fp = popen("find /sys -name eeprom 2>/dev/null", "r");
#endif

	if(fp == NULL) {
		fprintf(stderr, "can't execute find\n");
		return false;
	}

	buf = gtk_text_buffer_new(NULL);
	gtk_text_buffer_get_iter_at_offset(buf, &iter, 0);

	num = 0;

	while(fgets(eprom_names, sizeof(eprom_names), fp) != NULL){
		num++;
		/* strip trailing new lines */
		if (eprom_names[strlen(eprom_names) - 1] == '\n')
			eprom_names[strlen(eprom_names) - 1] = '\0';

		/* FRU EEPROMS are exactly 256 */
		if(stat(eprom_names, &st) !=0)
			continue;
		if(st.st_size != 256) {
			printf("skipping %s (size == %d)\n", eprom_names, (int)st.st_size);
			continue;
		}

		i = 0;
		if (!is_eeprom_fru(eprom_names, buf, &iter)) {
			/* Wasn't a FRU file, but is it a blank, writeable EEPROM? */
			efp = fopen(eprom_names, "w+");
			if (efp) {
				i = fread(text, 1, 256, efp);
				if (i == 256) {
					for (i = 0; i < 256; i++){
						if (!(text[i] == 0x00 || ((unsigned char) text[i]) == 0xFF)) {
							i = 0;
							break;
						}
					}
				}
				fclose(efp);

				/* dump the info into it */
				if (i == 256) {
					if (write_fru(eprom_names))
						if(!is_eeprom_fru(eprom_names, buf, &iter))
							i = 0;
				}
			} else {
				int errsv = errno;
				printf("Can't open %s in %s\n%s\n", eprom_names, __func__, strerror(errsv));
			}
			if (i == 0) {
				sprintf(text, "No FRU information in %s\n", eprom_names);
				gtk_text_buffer_insert(buf, &iter, text, -1);
			}
		}

		free (raw_input_data);
	}
	pclose(fp);

	if (!num) {
		sprintf(text, "No eeprom files found in /sys/\n");
		gtk_text_buffer_insert(buf, &iter, text, -1);
	}
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(data->connect_fru), buf);
	g_object_unref(buf);
#endif

	ctx = get_context(data);
	if (!ctx) {
		char buf[1024];
		printf("%s:%s():%i error getting context\n", __FILE__, __func__, __LINE__);
		iio_strerror(errno, buf, sizeof(buf));
		printf("error : %s\n", buf);
	}

	desc = ctx ? iio_context_get_description(ctx) : "";

	buf = gtk_text_buffer_new(NULL);
	gtk_text_buffer_get_iter_at_offset(buf, &iter, 0);
	gtk_text_buffer_insert(buf, &iter, desc, -1);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(data->ctx_info), buf);
	g_object_unref(buf);

	buf = gtk_text_buffer_new(NULL);
	gtk_text_buffer_get_iter_at_offset(buf, &iter, 0);

	num = ctx ? iio_context_get_devices_count(ctx) : 0;
	if (num > 0) {
		for (i = 0; i < num; i++) {
			struct iio_device *dev = iio_context_get_device(ctx, i);
			sprintf(text, "%s\n", iio_device_get_name(dev));
			gtk_text_buffer_insert(buf, &iter, text, -1);
		}
	} else {
		sprintf(text, "No iio devices found\n");
		gtk_text_buffer_insert(buf, &iter, text, -1);
	}

	gtk_text_view_set_buffer(GTK_TEXT_VIEW(data->connect_iio), buf);
	g_object_unref(buf);

	if (ctx && ctx != get_context_from_osc())
		iio_context_destroy(ctx);

	return !!ctx;
}

static bool hide_ok_btn()
{
	gtk_widget_set_sensitive(dialogs.ok_btn, false);

	return true;
}

static gint fru_connect_dialog(Dialogs *data, bool load_profile)
{
	/* Connect Dialog */
	gint ret;
	struct iio_context *ctx;
	const char *name = NULL;
	bool has_context = false;
	gchar *ip_addr;

	/* Preload the device list and FRU info only if we can use the local
	 * backend */
	ctx = get_context_from_osc();
	if (ctx)
		name = iio_context_get_name(ctx);

	if (name && !strcmp(name, "local"))
		has_context = connect_fillin(data);

	if (name && !strcmp(name, "network")) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialogs.connect_net), true);
		ip_addr = (char *) iio_context_get_description(ctx);
		ip_addr = strtok(ip_addr, " ");
		gtk_entry_set_text(GTK_ENTRY(dialogs.net_ip), ip_addr);
		has_context = connect_fillin(data);
	}

	if (name && !strcmp(name, "usb")) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialogs.connect_usb), true);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialogs.connect_usb), true);
		has_context = connect_fillin(data);
	}

#ifdef SERIAL_BACKEND
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialogs.connect_serial), true);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dialogs.connect_serial), true);
#endif

	while (true) {
		gtk_widget_set_sensitive(data->ok_btn, has_context);

		ret = gtk_dialog_run(GTK_DIALOG(data->connect));
		switch (ret) {
		case GTK_RESPONSE_APPLY:
			/* Refresh button */
			widget_set_cursor(data->connect, GDK_WATCH);
			connect_clear(NULL);
			has_context = connect_fillin(data);
			widget_use_parent_cursor(data->connect);
			refresh_usb();
			continue;
		case GTK_RESPONSE_OK:
			ctx = get_context(data);
			widget_set_cursor(data->connect, GDK_WATCH);
			widget_use_parent_cursor(data->connect);
			if (!ctx)
				continue;

			if (ctx != get_context_from_osc()) {
				application_reload(ctx, false);
			}
			break;
		default:
			printf("unknown response (%i) in %s(%s)\n", ret, __FILE__, __func__);
		case GTK_RESPONSE_CANCEL:
		case GTK_RESPONSE_DELETE_EVENT:
			break;
		}

		gtk_widget_hide(data->connect);
		return ret;
	}
}

G_MODULE_EXPORT gint cb_connect(GtkButton *button, Dialogs *data)
{
	return fru_connect_dialog(data, true);
}

gint fru_connect(void)
{
	return fru_connect_dialog(&dialogs, false);
}

gint connect_dialog(bool load_profile)
{
	return fru_connect_dialog(&dialogs, load_profile);
}

G_MODULE_EXPORT void cb_show_about(GtkButton *button, Dialogs *data)
{
	/* About dialog */
	gtk_dialog_run(GTK_DIALOG(data->about));
	gtk_widget_hide(data->about);
}

G_MODULE_EXPORT void load_save_profile_cb(GtkButton *button, Dialogs *data)
{
	/* Save as Dialog */
	gint ret;
	static char *filename = NULL;
	char *name;

	gtk_file_chooser_set_action(GTK_FILE_CHOOSER (data->load_save_profile), GTK_FILE_CHOOSER_ACTION_SAVE);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(data->load_save_profile), TRUE);

	if(!filename) {
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (data->load_save_profile), OSC_PROFILES_FILE_PATH);
		gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER (data->load_save_profile), "profile1");
	} else {
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER (data->load_save_profile), filename);
		g_free(filename);
		filename = NULL;

	}

	ret = gtk_dialog_run(GTK_DIALOG(data->load_save_profile));

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (data->load_save_profile));
	if (filename) {
		name = malloc(strlen(filename) + 5);
		switch(ret) {
			/* Response Codes encoded in glade file */
			case GTK_RESPONSE_DELETE_EVENT:
			case GTK_RESPONSE_CANCEL:
				break;
			case 1:
				/* save_ini */
				if (!strncasecmp(&filename[strlen(filename)-4], ".ini", 4))
					strcpy(name, filename);
				else
					sprintf(name, "%s.ini", filename);

				save_complete_profile(name);
				break;
			case 2:
				/* load_ini */
				if (!strncasecmp(&filename[strlen(filename)-4], ".ini", 4))
					strcpy(name, filename);
				else
					sprintf(name, "%s.ini", filename);

				load_complete_profile(name);
				break;
			default:
				printf("ret : %i\n", ret);
		}
		free(name);
	}
	gtk_widget_hide(data->load_save_profile);
}

G_MODULE_EXPORT void cb_quit(GtkButton *button, Dialogs *data)
{
	application_quit();
}

GtkWidget * create_nonblocking_popup(GtkMessageType type,
		const char *title, const char *str, ...)
{
	GtkWidget *dialog;
	va_list args;
	char buf[1024];
	int len;

	va_start(args, str);
	len = vsnprintf(buf, 1024, str, args);
	va_end(args);

	if (len < 0)
		return NULL;

	dialog = gtk_message_dialog_new(NULL,
			GTK_DIALOG_MODAL,
			type,
			GTK_BUTTONS_NONE,
			buf, NULL);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_widget_show (dialog);

	return dialog;
}

gint create_blocking_popup(GtkMessageType type, GtkButtonsType button,
		const char *title, const char *str, ...)
{
	GtkWidget *dialog;
	va_list args;
	char buf[1024], *newline;
	int len;
	gint run;

	va_start(args, str);
	len = vsnprintf(buf, 1024, str, args);
	va_end(args);

	if (len < 0)
		return -1;

	/* replace the str "\n" with the actual newline char */
	while ( (newline = strstr(buf, "\\n")) ) {
		*newline = '\n';
		/* strlen + 1 to get the termination char */
		memmove(newline+1, newline+2, (strlen(newline+2)) + 1);
	}

	dialog = gtk_message_dialog_new(NULL,
			GTK_DIALOG_MODAL,
			type,
			button,
			buf, NULL);
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	run = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	return run;
}

static glong date_compare_against_build_date(const char *iso8601_date)
{
#ifdef GIT_COMMIT_TIMESTAMP
	glong build_time = atol(GIT_COMMIT_TIMESTAMP);
	glong ret = 0;

#if GLIB_CHECK_VERSION(2, 56, 0)

	GDateTime *time = g_date_time_new_from_iso8601(iso8601_date, NULL);
	if (time != NULL) {
		ret = g_date_time_to_unix(time) - build_time;
		g_date_time_unref(time);
	} else {
		printf("%s could not parse date. Not a ISO 8601 format.", __func__);
	}

#else

	GTimeVal time;
	gboolean parsed;

	parsed = g_time_val_from_iso8601(iso8601_date, &time);
	if (parsed) {
		ret = time.tv_sec - build_time;
	} else {
		printf("%s could not parse date. Not a ISO 8601 format.", __func__);
	}

#endif

	return ret;
#else
	return 0;
#endif
}

/*
 * Dispaly the up-to-date status of the software. If a new release is available
 * display information about it.
 */
static gboolean version_info_show(gpointer data)
{
	Dialogs *_dialogs = (data) ? (Dialogs *)data : &dialogs;
	GtkBuilder *builder = _dialogs->builder;
	GtkWidget *r_name, *r_link, *r_dld_link;
	GtkWidget *internal_vbox;
	gchar *buf;

	/* don't bother showing the dialog if version check failed (applies only at startup) */
	if (!release && !data)
		return false;

	gdk_threads_enter();
	internal_vbox = GTK_WIDGET(gtk_builder_get_object(builder,
				"msg_dialog_vbox"));

	if (!release) {
		g_object_set(G_OBJECT(_dialogs->latest_version), "text",
				"Failed to get the latest version."
				"Make sure you have an internet connection.",
				NULL);
		gtk_widget_hide(internal_vbox);
	} else if (strncmp(GIT_VERSION, release->commit, 7)) {

		if (date_compare_against_build_date(release->build_date) > 0) {
			g_object_set(G_OBJECT(_dialogs->latest_version), "text",
				"A new version is available", NULL);
		} else {
			/* No data means that a silent version checking has been
			   requested. The progress bar has already been hidden
			   and so should the message dialog be. */
			if (!data)
				goto end;
			g_object_set(G_OBJECT(_dialogs->latest_version), "text",
				"This software is newer than the latest release",
				NULL);
		}

		r_name = GTK_WIDGET(gtk_builder_get_object(builder,
					"latest_version_name"));
		r_link = GTK_WIDGET(gtk_builder_get_object(builder,
					"latest_version_link"));
		r_dld_link = GTK_WIDGET(gtk_builder_get_object(builder,
					"latest_version_donwnload_link"));

		buf = g_strdup_printf("<b>%s</b>", release->name);
		gtk_label_set_markup(GTK_LABEL(r_name), buf);
		g_free(buf);
		gtk_link_button_set_uri(GTK_LINK_BUTTON(r_link), release->url);
		gtk_link_button_set_uri(GTK_LINK_BUTTON(r_dld_link),
					release->windows_dld_url);
		#ifndef __MINGW32__
		gtk_widget_hide(r_dld_link);
		#endif

		gtk_widget_show(internal_vbox);
	} else {
		/* No data means that a silent version checking has been
		   requested. The progress bar has already been hidden and so
		   should the message dialog be. */
		if (!data)
			goto end;
		g_object_set(G_OBJECT(_dialogs->latest_version), "text",
				"This software is up to date", NULL);
		gtk_widget_hide(internal_vbox);
	}

	release_dispose(release);
	release = NULL;


	gtk_widget_set_visible(GTK_WIDGET(gtk_builder_get_object(builder,
		"version_check_dont_show_again")), !data);

	gtk_dialog_run(GTK_DIALOG(_dialogs->latest_version));
	gtk_widget_hide(_dialogs->latest_version);

end:
	gdk_threads_leave();

	return false;
}

/*
 * Use CURL to get the latest release of the software.
 * All release information will be stored globally variable.
 * Use release_dispose() when the release information are no longer needed.
 */
static gpointer version_check(gpointer data)
{
	bool status = true;

	ver_check_done = false;
	if (phone_home_init()) {
		release = release_get_latest();
		phone_home_terminate();
	} else {
		status = false;
	}
	ver_check_done = true;

	return (gpointer)status;
}

/*
 * Periodically animate the progress bar as long as the version checking thread
 * is still working. When the thread has finished, signal (using g_idle_add())
 * the GUI to run the dialog that contains latest version information.
 */
static gboolean version_progress_update(gpointer data)
{
	Dialogs *_dialogs = (Dialogs *)data;

	if (_dialogs)
		gtk_progress_bar_pulse(GTK_PROGRESS_BAR(_dialogs->ver_progress_bar));
	if (ver_check_done) {
		if (_dialogs)
			gtk_widget_hide(_dialogs->ver_progress_window);
		g_idle_add(version_info_show, (gpointer)_dialogs);
	}

	return !ver_check_done;
}

/*
 * Start the version checking in a new thread.
 * @dialogs - The Dialogs structure to be used. Set it to NULL if a process bar
 *            is not required to be shown during version checking.
 */
void version_check_start(Dialogs *_dialogs)
{
	g_thread_new("version-check", (GThreadFunc)version_check, NULL);
	g_timeout_add(150, version_progress_update, _dialogs);
	if (_dialogs)
		gtk_widget_show(_dialogs->ver_progress_window);
}

/*
 * If someone hits tab, or return, virtually click on the apply button
 */
static gboolean connect_key_press_cb (GtkWidget *w, GdkEvent *ev, GtkWidget *dialog)
{
	GdkEventKey *key = (GdkEventKey*)ev;

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dialogs.connect_net)) && (key)) {
		if ((key->keyval == GDK_Tab) ||
		    (key->keyval == GDK_Return))
			   gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);
	}

	return FALSE;
}

G_MODULE_EXPORT void cb_check_for_updates(GtkCheckMenuItem *item, Dialogs *_dialogs)
{
	version_check_start(_dialogs);
}

void dialogs_init(GtkBuilder *builder)
{
	const char *name = NULL;
	struct iio_context *ctx;
	GtkWidget *tmp;

	dialogs.builder = builder;

	dialogs.about = GTK_WIDGET(gtk_builder_get_object(builder, "About_dialog"));
	dialogs.connect = GTK_WIDGET(gtk_builder_get_object(builder, "connect_dialog"));
	dialogs.connect_fru = GTK_WIDGET(gtk_builder_get_object(builder, "fru_info"));
	dialogs.serial_num = GTK_WIDGET(gtk_builder_get_object(builder, "serial_number_popup"));
	dialogs.connect_iio = GTK_WIDGET(gtk_builder_get_object(builder, "connect_iio_devices"));
	dialogs.ctx_info = GTK_WIDGET(gtk_builder_get_object(builder, "connect_iio_ctx_info"));
	dialogs.load_save_profile = GTK_WIDGET(gtk_builder_get_object(builder, "load_save_profile"));
	dialogs.connect_net = GTK_WIDGET(gtk_builder_get_object(builder, "connect_net"));
	dialogs.net_ip = GTK_WIDGET(gtk_builder_get_object(builder, "connect_net_IP"));
	dialogs.connect_usb = GTK_WIDGET(gtk_builder_get_object(builder, "connect_usb"));
	dialogs.connect_usbd = GTK_WIDGET(gtk_builder_get_object(builder, "connect_usb_devices"));
	dialogs.connect_serial = GTK_WIDGET(gtk_builder_get_object(builder, "connect_serial"));
	dialogs.connect_seriald = GTK_WIDGET(gtk_builder_get_object(builder, "connect_serial_devices"));
	dialogs.connect_serialbr = GTK_WIDGET(gtk_builder_get_object(builder, "connect_serial_baudrate"));
	dialogs.ok_btn = GTK_WIDGET(gtk_builder_get_object(builder, "button3"));
	dialogs.latest_version = GTK_WIDGET(gtk_builder_get_object(builder, "latest_version_popup"));
	dialogs.ver_progress_window = GTK_WIDGET(gtk_builder_get_object(builder, "progress_window"));
	dialogs.ver_progress_bar = GTK_WIDGET(gtk_builder_get_object(builder, "progressbar"));
	gtk_builder_connect_signals(builder, &dialogs);

	/* Bind some dialogs radio buttons to text/labels */
	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "connect_net_label"));
	serial_num = GTK_WIDGET(gtk_builder_get_object(builder, "serial_number"));
	fru_date = GTK_WIDGET(gtk_builder_get_object(builder, "fru_date"));
	fru_file_list = GTK_WIDGET(gtk_builder_get_object(builder, "FRU_files"));

	g_object_bind_property(dialogs.connect_net, "active", tmp, "sensitive", 0);
	g_object_bind_property(dialogs.connect_net, "active", dialogs.net_ip, "sensitive", 0);

	g_object_bind_property(dialogs.connect_serial, "active",
		       GTK_WIDGET(gtk_builder_get_object(builder, "connect_serial_label")), "sensitive", 0);
	g_object_bind_property(dialogs.connect_serial, "active", dialogs.connect_seriald, "sensitive", 0);
	g_object_bind_property(dialogs.connect_serial, "active", dialogs.connect_serialbr, "sensitive", 0);

	g_signal_connect(G_OBJECT(dialogs.connect_seriald), "changed",
			(GCallback) hide_ok_btn, NULL);

	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(builder, "connect_usb_label")),
			false);
	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object(builder, "connect_serial_label")),
			false);

	g_signal_connect(G_OBJECT(dialogs.connect_serial), "toggled",
			(GCallback) connect_clear, NULL);
	gtk_widget_set_sensitive(dialogs.connect_seriald, false);
	gtk_widget_set_sensitive(dialogs.connect_serialbr, false);

	/* test USB backend & hide if it's not supported */
	if (!iio_has_backend("usb")) {
		gtk_widget_hide(dialogs.connect_usbd);
		gtk_widget_hide(dialogs.connect_usb);
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "connect_usb_label")));
	} else {
		g_object_bind_property(dialogs.connect_usb, "active",
				GTK_WIDGET(gtk_builder_get_object(builder, "connect_usb_label")),
				"sensitive", 0);
		g_object_bind_property(dialogs.connect_usb, "active", dialogs.connect_usbd,
				"sensitive", 0);
		g_signal_connect(G_OBJECT(dialogs.connect_usbd), "changed",
				(GCallback) hide_ok_btn, NULL);
		g_signal_connect(G_OBJECT(dialogs.connect_usb), "toggled",
				(GCallback) connect_clear, NULL);
		gtk_widget_set_sensitive(dialogs.connect_usbd, false);
	}

	/* Grey out the "local context" option if it is not available */
	ctx = get_context_from_osc();
	if (ctx)
		name = iio_context_get_name(ctx);
	if (!name || strcmp(name, "local")) {
		ctx = iio_create_local_context();
		if (ctx) {
			iio_context_destroy(ctx);
		} else {
			GtkWidget *local = GTK_WIDGET(gtk_builder_get_object(
						builder, "connect_local"));
			gtk_widget_set_sensitive(local, false);
			gtk_toggle_button_set_active(
					GTK_TOGGLE_BUTTON(dialogs.connect_net), true);
		}
	}

	refresh_usb();
	refresh_serial(builder);

	g_signal_connect(dialogs.net_ip, "key-press-event",
			(GCallback) connect_key_press_cb, dialogs.connect);

}
