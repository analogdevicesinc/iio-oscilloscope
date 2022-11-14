#include <errno.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <iio.h>
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "osc.h"
#include "backtrace.h"

extern GtkWidget *notebook;
extern GtkWidget *infobar;
extern GtkWidget *tooltips_en;
extern GtkWidget *versioncheck_en;
extern GtkWidget *main_window;
extern struct iio_context *ctx;
extern bool ctx_destroyed_by_do_quit;

extern void version_check_start(void *_dialogs);

static void infobar_hide_cb(GtkButton *btn, gpointer user_data)
{
	gtk_widget_set_visible(infobar, false);
}

static void infobar_reconnect_cb(GtkMenuItem *btn, gpointer user_data)
{
	struct iio_context *new_ctx = iio_context_clone(ctx);
	if (new_ctx) {
		application_reload(new_ctx, true);
		gtk_widget_set_visible(infobar, false);
	}
}

static void tooltips_enable_cb (GtkCheckMenuItem *item, gpointer data)
{
	gboolean enable;
	GdkScreen *screen;
	GtkSettings *settings;

	screen = gtk_window_get_screen(GTK_WINDOW(main_window));
	settings = gtk_settings_get_for_screen(screen);
	enable = gtk_check_menu_item_get_active(item);
	g_object_set(settings, "gtk-enable-tooltips", enable, NULL);
}

static GtkWidget * gui_connection_infobar_new(GtkWidget **out_infobar_close,
			GtkWidget **out_infobar_reconnect)
{
	GtkWidget *infobar;
	GtkWidget *msg_label, *content_area, *action_area;

	msg_label = gtk_label_new("");
	gtk_label_set_markup(GTK_LABEL(msg_label),
				"<b>ERROR: Connection lost</b>");
	gtk_widget_show(msg_label);

	infobar = gtk_info_bar_new();
	gtk_info_bar_set_message_type(GTK_INFO_BAR(infobar), GTK_MESSAGE_ERROR);
	content_area = gtk_info_bar_get_content_area(GTK_INFO_BAR(infobar));
	gtk_container_add(GTK_CONTAINER(content_area), msg_label);
	*out_infobar_close = gtk_info_bar_add_button(GTK_INFO_BAR(infobar),
					"Close", 0);
	*out_infobar_reconnect = gtk_info_bar_add_button(GTK_INFO_BAR(infobar),
					"Reconnect", 0);
	gtk_widget_set_no_show_all(infobar, TRUE);


	action_area = gtk_info_bar_get_action_area(GTK_INFO_BAR(infobar));
	gtk_orientable_set_orientation(GTK_ORIENTABLE(action_area),
					GTK_ORIENTATION_HORIZONTAL);

	return infobar;
}

static void vcheck_dont_show_cb(GtkToggleButton *btn, gpointer data);

static void versioncheck_en_cb(GtkCheckMenuItem *item, gpointer data)
{
	g_signal_handlers_block_by_func(data, vcheck_dont_show_cb, data);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data),
		!gtk_check_menu_item_get_active(item));
	g_signal_handlers_unblock_by_func(data, vcheck_dont_show_cb, data);
}

static void vcheck_dont_show_cb(GtkToggleButton *btn, gpointer data)
{
	g_signal_handlers_block_by_func(data, versioncheck_en_cb, data);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(data),
		!gtk_toggle_button_get_active(btn));
	g_signal_handlers_unblock_by_func(data, versioncheck_en_cb, data);
}

static void init_application ()
{
	GtkBuilder *builder;
	GtkWidget  *window;
	GtkWidget  *btn_capture;
	GtkWidget  *infobar_close, *infobar_reconnect;
	GtkWidget  *infobar_box;
	GtkWidget  *vcheck_dont_show;
	GtkAboutDialog *about = NULL;
	unsigned int major, minor;
	char patch[9];
	const gchar *tmp;
	gchar tmp2[1024];

	builder = gtk_builder_new();

	if (!gtk_builder_add_from_file(builder, "./glade/osc.glade", NULL)) {
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "osc.glade", NULL);
	} else {
		GtkImage *logo;
		GdkPixbuf *pixbuf;
		GError *err = NULL;

		/* We are running locally, so load the local files */
		logo = GTK_IMAGE(gtk_builder_get_object(builder, "about_ADI_logo"));
		g_object_set(logo, "file","./icons/ADIlogo.png", NULL);
		logo = GTK_IMAGE(gtk_builder_get_object(builder, "about_IIO_logo"));
		g_object_set(logo, "file","./icons/IIOlogo.png", NULL);
		about = GTK_ABOUT_DIALOG(gtk_builder_get_object(builder, "About_dialog"));
		pixbuf = gdk_pixbuf_new_from_file("./icons/osc128.png", &err);
		if (pixbuf) {
			g_object_set(about, "logo", pixbuf,  NULL);
			g_object_unref(pixbuf);
		}
	}

	/* Override version in About menu with git branch and commit hash. */
	if (!about)
		about = GTK_ABOUT_DIALOG(gtk_builder_get_object(builder, "About_dialog"));
	gtk_about_dialog_set_version(about, OSC_VERSION);
	iio_library_get_version(&major, &minor, patch);
	tmp = gtk_label_get_label(GTK_LABEL(gtk_builder_get_object(builder, "libiio_title")));
	sprintf(tmp2, "%s\nlibiio version : %u.%u-%s\n", tmp, major, minor, patch);
	gtk_label_set_label(GTK_LABEL(gtk_builder_get_object(builder, "libiio_title")),
			tmp2);

	window = GTK_WIDGET(gtk_builder_get_object(builder, "main_menu"));
	notebook = GTK_WIDGET(gtk_builder_get_object(builder, "notebook"));
	btn_capture = GTK_WIDGET(gtk_builder_get_object(builder, "new_capture_plot"));
	tooltips_en = GTK_WIDGET(gtk_builder_get_object(builder, "menuitem_tooltips_en"));
	versioncheck_en = GTK_WIDGET(gtk_builder_get_object(builder, "menuitem_vcheck_startup"));
	vcheck_dont_show = GTK_WIDGET(gtk_builder_get_object(builder, "version_check_dont_show_again"));
	infobar_box = GTK_WIDGET(gtk_builder_get_object(builder, "connect_infobar_container"));
	infobar = gui_connection_infobar_new(&infobar_close, &infobar_reconnect);
	gtk_box_pack_start(GTK_BOX(infobar_box), infobar, FALSE, TRUE, 0);
	main_window = window;

	/* Connect signals. */
	g_signal_connect(G_OBJECT(window), "delete-event", G_CALLBACK(application_quit), NULL);
	g_signal_connect(G_OBJECT(btn_capture), "activate", G_CALLBACK(new_plot_cb), NULL);
	g_signal_connect(G_OBJECT(tooltips_en), "toggled", G_CALLBACK(tooltips_enable_cb), NULL);
	g_signal_connect(G_OBJECT(versioncheck_en), "toggled", G_CALLBACK(versioncheck_en_cb), vcheck_dont_show);
	g_signal_connect(G_OBJECT(vcheck_dont_show), "toggled", G_CALLBACK(vcheck_dont_show_cb), versioncheck_en);

	g_signal_connect(G_OBJECT(infobar_close), "clicked", G_CALLBACK(infobar_hide_cb), NULL);
	g_signal_connect(G_OBJECT(infobar_reconnect), "clicked", G_CALLBACK(infobar_reconnect_cb), NULL);

	dialogs_init(builder);

	ctx = osc_create_context();
	if (ctx)
		do_init(ctx);
	gtk_widget_show(window);
}

static void usage(char *program)
{
	printf("%s: the IIO visualization and control tool\n", program);
	printf( " Copyright (C) Analog Devices, Inc. and others\n"
		" This is free software; see the source for copying conditions.\n"
		" There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A\n"
		" PARTICULAR PURPOSE.\n\n");

	/* please keep this list sorted in alphabetical order */
	printf( "Command line options:\n"
		"\t-p\tload specific profile (to skip profile loading use \"-\")\n"
		"\t-c\tIP address of device to connect to (192.168.2.1)\n"
		"\t-u\tUniform Resource Identifer (URI) of device to connect to ('usb:3.2.5')\n");

	printf("\nEnvironmental variables:\n"
		"\tOSC_FORCE_PLUGIN\tforce loading of a specific plugin\n");

	exit(-1);
}

static void sigterm (int signum)
{
	application_quit();
}

gint main (int argc, char **argv)
{
	int c;

	char *profile = NULL;

	init_signal_handlers(argv[0]);

	opterr = 0;
	while ((c = getopt (argc, argv, "c:p:u:")) != -1)
		switch (c) {
			case 'c':
				ctx = iio_create_network_context(optarg);
				if (!ctx) {
					printf("Failed connecting to remote device: %s\n", optarg);
					exit(-1);
				}
				break;
			case 'u':
				ctx = iio_create_context_from_uri(optarg);
				if (!ctx) {
					printf("Failed connecting to remote device: %s\n", optarg);
					exit(-1);
				}
				break;
			case 'p':
				profile = strdup(optarg);
				break;
			case '?':
				usage(argv[0]);
				break;
			default:
				printf("Unknown command line option\n");
				usage(argv[0]);
				break;
		}

#ifndef __MINGW__
	/* XXX: Enabling threading when compiling for Windows will lock the UI
	 * as soon as the main window is moved. */
	gdk_threads_init();
#endif
	gtk_init(&argc, &argv);

	signal(SIGTERM, sigterm);
	signal(SIGINT, sigterm);
#ifndef __MINGW__
	signal(SIGHUP, sigterm);
#endif

	gdk_threads_enter();
	init_application();
	c = load_default_profile(profile, true);
	if (!ctx_destroyed_by_do_quit) {
		if (!ctx)
			connect_dialog(false);

		create_default_plot();
		if (c == 0) {
			if (gtk_check_menu_item_get_active(
					GTK_CHECK_MENU_ITEM(versioncheck_en)))
				version_check_start(NULL);
			gtk_main();
		} else
			application_quit();
	}
	gdk_threads_leave();

	if (profile)
	    free(profile);

	if (c == 0 || c == -ENOTTY)
		return 0;
	else
		return -1;
}
