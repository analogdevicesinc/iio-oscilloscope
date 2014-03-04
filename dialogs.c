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
#include <sys/types.h>
#include <sys/stat.h>

#include "fru.h"
#include "osc.h"
#include "iio_utils.h"
#include "config.h"

typedef struct _Dialogs Dialogs;
struct _Dialogs
{
	GtkWidget *about;
	GtkWidget *connect;
	GtkWidget *connect_fru;
	GtkWidget *connect_iio;
	GtkWidget *serial_num;
	GtkWidget *load_save_profile;

};

static Dialogs dialogs;
static GtkWidget *serial_num;
static GtkWidget *fru_date;
static GtkWidget *fru_file_list;

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
	char *ser_num;
	time_t frutime;
	FILE *fp = NULL;
	size_t i;
	time_t tmp;
	struct tm *tmp2;
	char buf[256];
	struct dirent *ent;
	DIR *d;
	GtkListStore *store;

	d = opendir(FRU_FILES);
	/* No fru files, don't bother */
	if (!d) {
		printf("didn't find FRU_Files in %s at %s(%s)\n", FRU_FILES, __FILE__, __func__);
		return 0;
	}

	g_object_set(dialogs.serial_num, "secondary_text", eeprom, NULL);

	ser_num = malloc (128);
	memset(ser_num, 0, 128);

	fp = fopen(".serialnum", "r");
	if (fp) {
		i = fread(ser_num, 1, 128, fp);
		if (i >= 0)
			gtk_entry_set_text(GTK_ENTRY(serial_num), (const gchar*)&ser_num[1]);
		fclose(fp);
	}

	store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(fru_file_list)));
	gtk_list_store_clear (store);

	while ((ent = readdir(d))) {
		if (ent->d_type != DT_REG)
			continue;
		if (!str_endswith(ent->d_name, ".bin"))
			continue;
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fru_file_list), ent->d_name);
	}

	gtk_combo_box_set_active(GTK_COMBO_BOX(fru_file_list), ser_num[0]);
	free(ser_num);

	frutime = mins_since_jan_1_1996();
	tmp = min2date(frutime);
	tmp2 = gmtime(&tmp);

	strftime(buf, sizeof(buf), "%a %b %d %H:%M %Y", tmp2);

	gtk_entry_set_text(GTK_ENTRY(fru_date), buf);

	result = gtk_dialog_run(GTK_DIALOG(dialogs.serial_num));

	i = 0;
	switch (result) {
		case GTK_RESPONSE_OK:
			file = gtk_combo_box_get_active_text(GTK_COMBO_BOX(fru_file_list));
			serial = gtk_entry_get_text(GTK_ENTRY(serial_num));
			fflush(NULL);
			sprintf(buf, "fru-dump -i " FRU_FILES "%s -o %s -s %s -d %d 2>&1",
					file, eeprom, serial, (unsigned int)frutime);
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
			break;
		case GTK_RESPONSE_DELETE_EVENT:
			break;
		default:
			printf("unknown response %d in %s\n", result, __func__);
			break;
	}
	gtk_widget_hide(GTK_WIDGET(dialogs.serial_num));

	return i;
}
#else
static size_t write_fru(char *eeprom) {

	return 0;
}
#endif

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

void connect_fillin(Dialogs *data)
{
	char eprom_names[128];
	unsigned char *raw_input_data = NULL;
	FILE *efp, *fp;
	GtkTextBuffer *buf;
	GtkTextIter iter;
	char text[256];
	int num, i;
	char *devices=NULL, *device;
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
		return;
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
						if (!(text[i] == 0x00 || text[i] == 0xFF)) {
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

	buf = gtk_text_buffer_new(NULL);
	gtk_text_buffer_get_iter_at_offset(buf, &iter, 0);

	num = find_iio_names(&devices, NULL);
	device=devices;
	if (num > 0) {
		for (; num > 0; num--) {
			sprintf(text, "%s\n", devices);
			gtk_text_buffer_insert(buf, &iter, text, -1);
			devices += strlen(devices) + 1;
		}
	} else {
		sprintf(text, "No iio devices found\n");
		gtk_text_buffer_insert(buf, &iter, text, -1);
	}
	free(device);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(data->connect_iio), buf);
	g_object_unref(buf);

	return;
}

G_MODULE_EXPORT gint cb_connect(GtkButton *button, Dialogs *data)
{
	/* Connect Dialog */
	gint ret;
	connect_fillin(data);

	do {
		ret = gtk_dialog_run(GTK_DIALOG(data->connect));
		if (ret == GTK_RESPONSE_APPLY)
			connect_fillin(data);
	} while (ret == GTK_RESPONSE_APPLY);

	switch(ret) {
		case GTK_RESPONSE_CANCEL:
		case GTK_RESPONSE_DELETE_EVENT:
			break;
		case GTK_RESPONSE_OK:
			break;
		default:
			printf("unknown response (%i) in %s(%s)\n", ret, __FILE__, __func__);
			break;
	}
	gtk_widget_hide(data->connect);

	return ret;
}
gint fru_connect(void)
{
	return cb_connect(NULL, &dialogs);
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

				save_all_plugins(name, NULL);
				break;
			case 2:
				/* load_ini */
				if (!strncasecmp(&filename[strlen(filename)-4], ".ini", 4))
					strcpy(name, filename);
				else
					sprintf(name, "%s.ini", filename);

				restore_all_plugins(name, NULL);
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
	char buf[1024];
	int len;
	gint run;

	va_start(args, str);
	len = vsnprintf(buf, 1024, str, args);
	va_end(args);

	if (len < 0)
		return -1;

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

void dialogs_init(GtkBuilder *builder)
{
	GtkWidget *tmp, *tmp2;

	dialogs.about = GTK_WIDGET(gtk_builder_get_object(builder, "About_dialog"));
	dialogs.connect = GTK_WIDGET(gtk_builder_get_object(builder, "connect_dialog"));
	dialogs.connect_fru = GTK_WIDGET(gtk_builder_get_object(builder, "fru_info"));
	dialogs.serial_num = GTK_WIDGET(gtk_builder_get_object(builder, "serial_number_popup"));
	dialogs.connect_iio = GTK_WIDGET(gtk_builder_get_object(builder, "connect_iio_devices"));
	dialogs.load_save_profile = GTK_WIDGET(gtk_builder_get_object(builder, "load_save_profile"));
	gtk_builder_connect_signals(builder, &dialogs);

	/* Bind some dialogs radio buttons to text/labels */
	tmp2 = GTK_WIDGET(gtk_builder_get_object(builder, "connect_net"));
	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "connect_net_label"));
	serial_num = GTK_WIDGET(gtk_builder_get_object(builder, "serial_number"));
	fru_date = GTK_WIDGET(gtk_builder_get_object(builder, "fru_date"));
	fru_file_list = GTK_WIDGET(gtk_builder_get_object(builder, "FRU_files"));

	g_object_bind_property(tmp2, "active", tmp, "sensitive", 0);
	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "connect_net_IP"));
	g_object_bind_property(tmp2, "active", tmp, "sensitive", 0);
}
