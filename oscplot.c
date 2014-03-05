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
#include <gtkdatabox_markers.h>
#include <errno.h>
#include <stdbool.h>
#include <malloc.h>
#include <math.h>
#include <matio.h>

#include "ini/ini.h"
#include "osc.h"
#include "oscplot.h"
#include "config.h"
#include "iio_widget.h"
#include "datatypes.h"

extern void time_transform_function(Transform *tr, gboolean init_transform);
extern void fft_transform_function(Transform *tr, gboolean init_transform);
extern void constellation_transform_function(Transform *tr, gboolean init_transform);
extern void *find_setup_check_fct_by_devname(const char *dev_name);

extern struct _device_list *device_list;
extern unsigned num_devices;

static int (*plugin_setup_validation_fct)(struct iio_channel_info*, int, char **) = NULL;
static unsigned object_count = 0;

static void create_plot (OscPlot *plot);
static void plot_setup(OscPlot *plot);
static void capture_button_clicked_cb (GtkToggleToolButton *btn, gpointer data);
static void add_grid(OscPlot *plot);
static void rescale_databox(OscPlotPrivate *priv, GtkDatabox *box, gfloat border);
static void call_all_transform_functions(OscPlotPrivate *priv);
static void capture_start(OscPlotPrivate *priv);
static void plot_profile_save(OscPlot *plot, char *filename);
static void add_markers(OscPlot *plot, Transform *transform);
static void osc_plot_finalize(GObject *object);
static void osc_plot_dispose(GObject *object);
static void save_as(OscPlot *plot, const char *filename, int type);
static void treeview_expand_update(OscPlot *plot);
static void treeview_icon_color_update(OscPlot *plot);
static int  cfg_read_handler(void *user, const char* section, const char* name, const char* value);
static int device_find_by_name(const char *name);
static int enabled_channels_of_device(GtkTreeView *treeview, const char *dev_name);
static gboolean get_iter_by_name(GtkTreeView *tree, GtkTreeIter *iter, char *dev_name, char *ch_name);
static void set_marker_labels (OscPlot *plot, gchar *buf, enum marker_types type);
static void channel_color_icon_set_color(GdkPixbuf *pb, GdkColor *color);

/* IDs of signals */
enum {
	CAPTURE_EVENT_SIGNAL,
	DESTROY_EVENT_SIGNAL,
	LAST_SIGNAL
};

/* signals will be configured during class init */
static guint oscplot_signals[LAST_SIGNAL] = { 0 };

/* Columns of the device treestore */
enum {
	ELEMENT_NAME,
	IS_DEVICE,
	IS_CHANNEL,
	DEVICE_SELECTABLE,
	DEVICE_ACTIVE,
	CHANNEL_ACTIVE,
	ELEMENT_REFERENCE,
	EXPANDED,
	CHANNEL_SETTINGS,
	CHANNEL_COLOR_ICON,
	SENSITIVE,
	PLOT_TYPE,
	NUM_COL
};

#define NUM_GRAPH_COLORS 6

static GdkColor color_graph[NUM_GRAPH_COLORS] = {
	{
		.red = 0,
		.green = 60000,
		.blue = 0,
	},
	{
		.red = 60000,
		.green = 0,
		.blue = 0,
	},
	{
		.red = 0,
		.green = 0,
		.blue = 60000,
	},
	{
		.red = 0,
		.green = 60000,
		.blue = 60000,
	},
	{
		.red = 60000,
		.green = 60000,
		.blue = 60000,
	},
	{
		.red = 60000,
		.green = 60000,
		.blue = 0,
	},
};

static GdkColor color_grid = {
	.red = 51000,
	.green = 51000,
	.blue = 0,
};

static GdkColor color_background = {
	.red = 0,
	.green = 0,
	.blue = 0,
};

static GdkColor color_marker = {
	.red = 0xFFFF,
	.green = 0,
	.blue = 0xFFFF,
};

/* Helpers */
#define GET_CHANNEL_PARENT(ch) ((struct extra_info *)(((struct iio_channel_info *)(ch))->extra_field))->device_parent
#define CHANNEL_EXTRA_FIELD(ch) ((struct extra_info *)ch->extra_field)

#define TIME_SETTINGS(obj) ((struct _time_settings *)obj->settings)
#define FFT_SETTINGS(obj) ((struct _fft_settings *)obj->settings)
#define CONSTELLATION_SETTINGS(obj) ((struct _constellation_settings *)obj->settings)

struct int_and_plot {
	int int_obj;
	OscPlot *plot;
};

struct string_and_plot {
	char *string_obj;
	OscPlot *plot;
};

struct _OscPlotPrivate
{
	GtkBuilder *builder;

	int object_id;

	/* Graphical User Interface */
	GtkWidget *window;
	GtkWidget *databox;
	GtkWidget *capture_graph;
	GtkWidget *capture_button;
	GtkWidget *channel_list_view;
	GtkWidget *show_grid;
	GtkWidget *plot_type;
	GtkWidget *plot_domain;
	GtkWidget *enable_auto_scale;
	GtkWidget *hor_scale;
	GtkWidget *marker_label;
	GtkWidget *saveas_button;
	GtkWidget *saveas_dialog;
	GtkWidget *saveas_type_dialog;
	GtkWidget *fullscreen_button;
	GtkWidget *y_axis_max;
	GtkWidget *y_axis_min;
	GtkWidget *viewport_saveas_channels;
	GtkWidget *saveas_channels_list;
	GtkWidget *saveas_select_channel_message;
	GtkWidget *device_combobox;
	GtkWidget *sample_count_widget;
	GtkWidget *fft_size_widget;
	GtkWidget *fft_avg_widget;
	GtkWidget *fft_pwr_offset_widget;
	GtkWidget *channel_settings_menu;
	GtkWidget *channel_color_menuitem;
	GtkWidget *channel_math_menuitem;
	GtkWidget *math_dialog;

	GtkTextBuffer* tbuf;

	int frame_counter;
	time_t last_update;

	int do_a_rescale_flag;

	gulong capture_button_hid;
	gint deactivate_capture_btn_flag;

	/* A reference to the device holding the most recent created transform */
	struct _device_list *current_device;

	/* List of transforms for this plot */
	TrList *transform_list;

	/* Active transform type for this window */
	int active_transform_type;

	/* Transform currently holding the fft marker */
	Transform *tr_with_marker;

	/* Type of "Save As" currently selected*/
	gint active_saveas_type;

	/* The set of markers */
	struct marker_type markers[MAX_MARKERS + 2];
	struct marker_type *markers_copy;
	enum marker_types marker_type;

	/* Settings list of all channel */
	GSList *ch_settings_list;

	/* Databox data */
	GtkDataboxGraph *grid;
	gfloat gridy[25], gridx[25];

	gint redraw_function;
	gint stop_redraw;

	bool profile_loaded_scale;

	char *saveas_filename;

	struct int_and_plot fix_marker;
	struct string_and_plot add_mrk;
	struct string_and_plot remove_mrk;
	struct string_and_plot peak_mrk;
	struct string_and_plot fix_mrk;
	struct string_and_plot single_mrk;
	struct string_and_plot dual_mrk;
	struct string_and_plot image_mrk;
	struct string_and_plot off_mrk;

	gulong fixed_marker_hid;

	gfloat plot_left;
	gfloat plot_right;
	gfloat plot_top;
	gfloat plot_bottom;
	int read_scale_params;

	GMutex g_marker_copy_lock;
};

struct channel_settings {
	GdkColor graph_color;
	bool apply_inverse_funct;
	bool apply_multiply_funct;
	bool apply_add_funct;
	double multiply_value;
	double add_value;
};

G_DEFINE_TYPE(OscPlot, osc_plot, GTK_TYPE_WIDGET)

static void osc_plot_class_init(OscPlotClass *klass)
{
	GObjectClass *gobject_class  = G_OBJECT_CLASS (klass);

	gobject_class->dispose = osc_plot_dispose;
	gobject_class->finalize = osc_plot_finalize;

	oscplot_signals[CAPTURE_EVENT_SIGNAL] = g_signal_new("osc-capture-event",
			G_TYPE_FROM_CLASS (klass),
			G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			G_STRUCT_OFFSET (OscPlotClass, capture_event),
			NULL,
			NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	oscplot_signals[DESTROY_EVENT_SIGNAL] = g_signal_new("osc-destroy-event",
			G_TYPE_FROM_CLASS (klass),
			G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			G_STRUCT_OFFSET (OscPlotClass, destroy_event),
			NULL,
			NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	g_type_class_add_private (gobject_class, sizeof (OscPlotPrivate));
}

static void osc_plot_init(OscPlot *plot)
{
	plot->priv = G_TYPE_INSTANCE_GET_PRIVATE (plot, OSC_PLOT_TYPE, OscPlotPrivate);

	create_plot(plot);

	/* Create a empty list of transforms */
	plot->priv->transform_list = TrList_new();

	/* No active transforms by default */
	plot->priv->active_transform_type = NO_TRANSFORM_TYPE;
}

GtkWidget *osc_plot_new(void)
{
	return GTK_WIDGET(g_object_new(OSC_PLOT_TYPE, NULL));
}

void osc_plot_destroy (OscPlot *plot)
{
	gtk_widget_destroy(plot->priv->window);
	gtk_widget_destroy(GTK_WIDGET(plot));
}

void osc_plot_data_update (OscPlot *plot)
{
	call_all_transform_functions(plot->priv);
}

void osc_plot_update_rx_lbl(OscPlot *plot, bool force_update)
{
	OscPlotPrivate *priv = plot->priv;
	TrList *tr_list = priv->transform_list;
	char buf[20];
	double corr;
	int i;

	/* Skip rescaling graphs, updating labels and others if the redrawing is currently halted. */
	if (priv->redraw_function <= 0 && !force_update)
		return;

	if (priv->active_transform_type == FFT_TRANSFORM || priv->active_transform_type == COMPLEX_FFT_TRANSFORM) {
		sprintf(buf, "%sHz", priv->current_device->adc_scale);
		gtk_label_set_text(GTK_LABEL(priv->hor_scale), buf);
		/* In FFT mode we need to scale the x-axis according to the selected sampling freequency */
		for (i = 0; i < tr_list->size; i++)
			Transform_setup(tr_list->transforms[i]);
		if (priv->active_transform_type == COMPLEX_FFT_TRANSFORM)
			corr = priv->current_device->adc_freq / 2.0;
		else
			corr = 0;
		if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enable_auto_scale)) && !force_update)
			return;
		if (priv->profile_loaded_scale)
			return;
		gtk_databox_set_total_limits(GTK_DATABOX(priv->databox), -5.0 - corr, priv->current_device->adc_freq / 2.0 + 5.0, 0.0, -100.0);
		priv->do_a_rescale_flag = 1;
	} else {
		gtk_label_set_text(GTK_LABEL(priv->hor_scale), "Samples");
	}
}

void osc_plot_restart (OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	if (priv->redraw_function > 0)
	{
		priv->stop_redraw = TRUE;
		plot_setup(plot);
		add_grid(plot);
		gtk_widget_queue_draw(priv->databox);
		priv->frame_counter = 0;
		capture_start(priv);
	}
}

void osc_plot_draw_stop (OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	priv->stop_redraw = TRUE;
	gtk_toggle_tool_button_set_active((GtkToggleToolButton *)priv->capture_button, FALSE);
}

void osc_plot_save_to_ini (OscPlot *plot, char *filename)
{
	plot_profile_save(plot, filename);
}

int osc_plot_ini_read_handler (OscPlot *plot, const char *section, const char *name, const char *value)
{
	return cfg_read_handler(plot, section, name, value);
}

void osc_plot_save_as (OscPlot *plot, char *filename, int type)
{
	save_as(plot, filename, type);
}

char * osc_plot_get_active_device (OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean next_iter;
	gboolean active;
	struct _device_list *device;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->channel_list_view));
	next_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (next_iter) {
		gtk_tree_model_get(model, &iter, ELEMENT_REFERENCE, &device, DEVICE_ACTIVE, &active, -1);
		if (active)
			return device->device_name;
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}

	return NULL;
}

int osc_plot_get_fft_avg (OscPlot *plot)
{
	return gtk_spin_button_get_value(GTK_SPIN_BUTTON(plot->priv->fft_avg_widget));
}

int osc_plot_get_marker_type (OscPlot *plot)
{
	return plot->priv->marker_type;
}

void osc_plot_set_marker_type (OscPlot *plot, int mtype)
{
	plot->priv->marker_type = mtype;
}

void * osc_plot_get_markers_copy(OscPlot *plot)
{
	return plot->priv->markers_copy;
}

void osc_plot_set_markers_copy (OscPlot *plot, void *value)
{
	plot->priv->markers_copy = value;
}

int osc_plot_get_plot_domain (OscPlot *plot)
{
	return gtk_combo_box_get_active(GTK_COMBO_BOX(plot->priv->plot_domain));
}

GMutex * osc_plot_get_marker_lock (OscPlot *plot)
{
	return &plot->priv->g_marker_copy_lock;
}

static void osc_plot_dispose(GObject *object)
{
	G_OBJECT_CLASS(osc_plot_parent_class)->dispose(object);
}

static void osc_plot_finalize(GObject *object)
{
	G_OBJECT_CLASS(osc_plot_parent_class)->finalize(object);
}

static void expand_iter(OscPlot *plot, GtkTreeIter *iter, gboolean expand)
{
	GtkTreeView *tree = GTK_TREE_VIEW(plot->priv->channel_list_view);
	GtkTreeModel *model;
	GtkTreePath *path;

	model = gtk_tree_view_get_model(tree);
	path = gtk_tree_model_get_path(model, iter);
	if (expand)
		gtk_tree_view_expand_row(tree, path, FALSE);
	else
		gtk_tree_view_collapse_row(tree, path);
}

static void foreach_device_iter(GtkTreeView *treeview,
	void (*tree_iter_func)(GtkTreeModel *model, GtkTreeIter *iter, void *user_data),
	void *user_data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	gboolean next_iter;

	model = gtk_tree_view_get_model(treeview);
	if (!gtk_tree_model_get_iter_first(model, &iter))
		return;

	next_iter = true;
	while (next_iter) {
		tree_iter_func(model, &iter, user_data);
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}
}

static void foreach_channel_iter_of_device(GtkTreeView *treeview, struct _device_list *device,
	void (*tree_iter_func)(GtkTreeModel *model, GtkTreeIter *data, void *user_data),
	void *user_data)
{
	GtkTreeIter iter;
	GtkTreeIter child_iter;
	GtkTreeModel *model;
	gboolean next_iter;
	gboolean next_child_iter;
	char *str_device;

	model = gtk_tree_view_get_model(treeview);
	if (!gtk_tree_model_get_iter_first(model, &iter))
		return;

	next_iter = true;
	while (next_iter) {
		gtk_tree_model_get(model, &iter, ELEMENT_NAME, &str_device, -1);
		if (!strcmp(device->device_name, str_device)) {
			g_free(str_device);
			break;
		}
		g_free(str_device);
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}
	if (!next_iter)
		return;

	gtk_tree_model_iter_children(model, &child_iter, &iter);
	next_child_iter = true;
	while (next_child_iter) {
		tree_iter_func(model, &child_iter, user_data);
		next_child_iter = gtk_tree_model_iter_next(model, &child_iter);
	}
}

 static void channel_duplicate(GtkTreeModel *model, GtkTreeIter *iter, void *user_data)
 {
	 struct iio_channel_info *channel;
	 struct iio_channel_info *duplicate_ch_list = user_data;
	 gboolean enabled;
	 int i;

	 gtk_tree_model_get(model, iter, ELEMENT_REFERENCE, &channel,
		CHANNEL_ACTIVE, &enabled, -1);
	i = channel->index;
	duplicate_ch_list[i].name = channel->name;
	duplicate_ch_list[i].enabled = enabled;

 }

static gboolean check_valid_setup_of_device(OscPlot *plot, struct _device_list *device)
{
	OscPlotPrivate *priv = plot->priv;
	int plot_type;
	int num_enabled;

	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean device_enabled;

	plot_type = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain));

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->channel_list_view));
	get_iter_by_name(GTK_TREE_VIEW(priv->channel_list_view), &iter, device->device_name, NULL);
	gtk_tree_model_get(model, &iter, DEVICE_ACTIVE, &device_enabled, -1);
	if (!device_enabled && plot_type != TIME_PLOT)
		return true;

	num_enabled = enabled_channels_of_device(GTK_TREE_VIEW(priv->channel_list_view), device->device_name);

	/* Basic validation rules */
	if (plot_type == FFT_PLOT) {
		if (num_enabled != 2 && num_enabled != 1) {
			gtk_widget_set_tooltip_text(priv->capture_button,
				"FFT needs 2 channels or less");
			return false;
		}
	} else if (plot_type == XY_PLOT) {
		if (num_enabled != 2) {
			gtk_widget_set_tooltip_text(priv->capture_button,
				"Constellation needs 2 channels");
			return false;
		}
	} else if (plot_type == TIME_PLOT) {
		if (num_enabled == 0) {
			gtk_widget_set_tooltip_text(priv->capture_button,
				"Time Domain needs at least one channel");
			return false;
		}
	}

	/* Additional validation rules provided by the plugin of the device */
	if (num_enabled != 2 || plot_type == TIME_PLOT)
		return true;

	struct iio_channel_info *temp_channels;
	bool valid_comb;
	char *ch_names[2];
	char warning_text[100];

	plugin_setup_validation_fct = find_setup_check_fct_by_devname(device->device_name);
	if (plugin_setup_validation_fct) {
		temp_channels = (struct iio_channel_info *)malloc(sizeof(struct iio_channel_info) * device->num_channels);
		/* Simulate the existing channel list, because channels that are enabled
		in this window are not necessary enabled in the existing channel list. */
		foreach_channel_iter_of_device(GTK_TREE_VIEW(priv->channel_list_view),
			device, *channel_duplicate, temp_channels);
		valid_comb = (*plugin_setup_validation_fct)(temp_channels, device->num_channels, ch_names);
		free(temp_channels);
		if (!valid_comb) {
			snprintf(warning_text, sizeof(warning_text),
				"Combination between %s and %s is invalid", ch_names[0], ch_names[1]);
			gtk_widget_set_tooltip_text(priv->capture_button, warning_text);
			return false;
		}
	}

	return true;
}

static gboolean check_valid_setup(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	gboolean is_valid = false;
	int i;

	for (i = 0; i < num_devices; i++) {
		is_valid = check_valid_setup_of_device(plot, &device_list[i]);
		if (gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain)) == TIME_PLOT) {
			if (!is_valid)
				continue;
			else break;
		}
		if (!is_valid)
			goto capture_button_err;
	}

	if (!is_valid)
		goto capture_button_err;

	g_object_set(priv->capture_button, "stock-id", "gtk-media-play", NULL);
	gtk_widget_set_tooltip_text(priv->capture_button, "Capture / Stop");
	if (!priv->capture_button_hid) {
		priv->capture_button_hid = g_signal_connect(priv->capture_button, "toggled",
			G_CALLBACK(capture_button_clicked_cb), plot);
		priv->deactivate_capture_btn_flag = 0;
	}

	return true;

capture_button_err:
	g_object_set(priv->capture_button, "stock-id", "gtk-dialog-warning", NULL);
	if (priv->capture_button_hid) {
		g_signal_handler_disconnect(priv->capture_button, priv->capture_button_hid);
		priv->deactivate_capture_btn_flag = 1;
	}
	priv->capture_button_hid = 0;

	return false;
}

static void update_transform_settings(OscPlot *plot, Transform *transform,
	struct channel_settings *csettings)
{
	OscPlotPrivate *priv = plot->priv;
	int plot_type;

	plot_type = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain));
	if (plot_type == FFT_PLOT) {
		FFT_SETTINGS(transform)->fft_size = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->fft_size_widget)));
		FFT_SETTINGS(transform)->fft_avg = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->fft_avg_widget));
		FFT_SETTINGS(transform)->fft_pwr_off = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->fft_pwr_offset_widget));
		FFT_SETTINGS(transform)->fft_alg_data.cached_fft_size = -1;
		FFT_SETTINGS(transform)->fft_alg_data.cached_num_active_channels = -1;
		if (transform->channel_parent2 != NULL)
			FFT_SETTINGS(transform)->fft_alg_data.num_active_channels = 2;
		else
			FFT_SETTINGS(transform)->fft_alg_data.num_active_channels = 1;
		FFT_SETTINGS(transform)->markers = NULL;
		FFT_SETTINGS(transform)->markers_copy = NULL;
		FFT_SETTINGS(transform)->marker_lock = NULL;
		FFT_SETTINGS(transform)->marker_type = NULL;
	} else if (plot_type == TIME_PLOT) {
		TIME_SETTINGS(transform)->num_samples = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->sample_count_widget));
		TIME_SETTINGS(transform)->apply_inverse_funct = csettings->apply_inverse_funct;
		TIME_SETTINGS(transform)->apply_multiply_funct = csettings->apply_multiply_funct;
		TIME_SETTINGS(transform)->apply_add_funct = csettings->apply_add_funct;
		TIME_SETTINGS(transform)->multiply_value = csettings->multiply_value;
		TIME_SETTINGS(transform)->add_value = csettings->add_value;
	} else if (plot_type == XY_PLOT){
		CONSTELLATION_SETTINGS(transform)->num_samples = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->sample_count_widget));
	}
}

static Transform* add_transform_to_list(OscPlot *plot, struct iio_channel_info *ch0,
	struct iio_channel_info *ch1, int tr_type, struct channel_settings *csettings)
{
	OscPlotPrivate *priv = plot->priv;
	TrList *list = priv->transform_list;
	Transform *transform;
	struct _time_settings *time_settings;
	struct _fft_settings *fft_settings;
	struct _constellation_settings *constellation_settings;
	struct extra_info *ch_info;

	transform = Transform_new(tr_type);
	ch_info = ch0->extra_field;
	transform->channel_parent = ch0;
	transform->graph_color = &color_graph[0];
	ch_info->shadow_of_enabled++;
	priv->current_device = GET_CHANNEL_PARENT(ch0);
	Transform_set_in_data_ref(transform, (gfloat **)&ch_info->data_ref, &priv->current_device->sample_count);
	switch (tr_type) {
	case TIME_TRANSFORM:
		Transform_attach_function(transform, time_transform_function);
		time_settings = (struct _time_settings *)malloc(sizeof(struct _time_settings));
		Transform_attach_settings(transform, time_settings);
		transform->graph_color = &csettings->graph_color;
		priv->active_transform_type = TIME_TRANSFORM;
		break;
	case FFT_TRANSFORM:
		Transform_attach_function(transform, fft_transform_function);
		fft_settings = (struct _fft_settings *)malloc(sizeof(struct _fft_settings));
		Transform_attach_settings(transform, fft_settings);
		priv->active_transform_type = FFT_TRANSFORM;
		break;
	case CONSTELLATION_TRANSFORM:
		transform->channel_parent2 = ch1;
		Transform_attach_function(transform, constellation_transform_function);
		constellation_settings = (struct _constellation_settings *)malloc(sizeof(struct _constellation_settings));
		Transform_attach_settings(transform, constellation_settings);
		ch_info = ch1->extra_field;
		ch_info->shadow_of_enabled++;
		priv->active_transform_type = CONSTELLATION_TRANSFORM;
		break;
	case COMPLEX_FFT_TRANSFORM:
		transform->channel_parent2 = ch1;
		Transform_attach_function(transform, fft_transform_function);
		fft_settings = (struct _fft_settings *)malloc(sizeof(struct _fft_settings));
		Transform_attach_settings(transform, fft_settings);
		ch_info = ch1->extra_field;
		ch_info->shadow_of_enabled++;
		priv->active_transform_type = COMPLEX_FFT_TRANSFORM;
		break;
	default:
		printf("Invalid transform\n");
		return NULL;
	}
	TrList_add_transform(list, transform);
	update_transform_settings(plot, transform, csettings);

	return transform;
}

static void remove_transform_from_list(OscPlot *plot, Transform *tr)
{
	OscPlotPrivate *priv = plot->priv;
	TrList *list = priv->transform_list;
	struct extra_info *ch_info = tr->channel_parent->extra_field;

	ch_info->shadow_of_enabled--;
	if (tr->type_id == CONSTELLATION_TRANSFORM ||
		tr->type_id == COMPLEX_FFT_TRANSFORM) {
		ch_info = tr->channel_parent2->extra_field;
		ch_info->shadow_of_enabled--;
	}
	if (tr->has_the_marker)
		priv->tr_with_marker = NULL;
	TrList_remove_transform(list, tr);
	Transform_destroy(tr);
	if (list->size == 0) {
		priv->active_transform_type = NO_TRANSFORM_TYPE;
		priv->current_device = NULL;
	}
}

static void add_markers(OscPlot *plot, Transform *transform)
{
	OscPlotPrivate *priv = plot->priv;
	GtkDatabox *databox = GTK_DATABOX(plot->priv->databox);
	struct marker_type *markers = priv->markers;
	char buf[10];
	int i;

	for (i = 0; i <= MAX_MARKERS; i++) {
		markers[i].x = 0.0f;
		markers[i].y = -100.0f;
		if (markers[i].graph)
			g_object_unref(markers[i].graph);
		markers[i].graph = gtk_databox_markers_new(1, &markers[i].x, &markers[i].y, &color_marker,
			10, GTK_DATABOX_MARKERS_TRIANGLE);
		gtk_databox_graph_add(databox, markers[i].graph);
		sprintf(buf, "?%i", i);
		gtk_databox_markers_set_label(GTK_DATABOX_MARKERS(markers[i].graph), 0, GTK_DATABOX_MARKERS_TEXT_N, buf, FALSE);
		if (priv->marker_type == MARKER_OFF)
			gtk_databox_graph_set_hide(markers[i].graph, TRUE);
		else
			gtk_databox_graph_set_hide(markers[i].graph, !markers[i].active);
	}
	if (priv->marker_type != MARKER_OFF)
		set_marker_labels(plot, NULL, priv->marker_type);

	transform->has_the_marker = true;
	priv->tr_with_marker = transform;
	FFT_SETTINGS(transform)->markers = priv->markers;
	FFT_SETTINGS(transform)->markers_copy = priv->markers_copy;
	FFT_SETTINGS(transform)->marker_type = &priv->marker_type;
	FFT_SETTINGS(transform)->marker_lock = &priv->g_marker_copy_lock;
}

static unsigned int plot_sample_count_get(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	unsigned int sample_count;

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain)) == FFT_PLOT)
		sample_count = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->fft_size_widget)));
	else
		sample_count = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->sample_count_widget));

	return sample_count;
}

static void collect_parameters_from_plot(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	struct plot_params *prms;
	GSList *list;
	int i;

	for (i = 0; i < num_devices; i++) {
		prms = malloc(sizeof(struct plot_params));
		prms->plot_id = priv->object_id;
		prms->sample_count = plot_sample_count_get(plot);
		list = device_list[i].plots_sample_counts;
		list = g_slist_prepend(list, prms);
		device_list[i].plots_sample_counts = list;
	}
}

static void dispose_parameters_from_plot(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	struct plot_params *prms;
	GSList *list;
	GSList *node;
	GSList *del_link = NULL;
	int i;

	for (i = 0; i < num_devices; i++) {
		list = device_list[i].plots_sample_counts;
		for (node = list; node; node = g_slist_next(node)) {
			prms = node->data;
			if (prms->plot_id == priv->object_id) {
				del_link = node;
				break;
			}
		}
		if (del_link) {
			list = g_slist_delete_link(list, del_link);
			device_list[i].plots_sample_counts = list;
		}
	}
}

static void draw_marker_values(OscPlotPrivate *priv, Transform *tr)
{
	struct _fft_settings *settings = tr->settings;
	struct extra_info *ch_info;
	struct marker_type *markers = settings->markers;
	GtkTextIter iter;
	char text[256];
	int m;

	if (priv->tbuf == NULL) {
			priv->tbuf = gtk_text_buffer_new(NULL);
			gtk_text_view_set_buffer(GTK_TEXT_VIEW(priv->marker_label), priv->tbuf);
	}
	ch_info = tr->channel_parent->extra_field;
	if (MAX_MARKERS && priv->marker_type != MARKER_OFF) {
		for (m = 0; m <= MAX_MARKERS && markers[m].active; m++) {
			sprintf(text, "M%i: %2.2f dBFS @ %2.3f %sHz%c",
					m, markers[m].y, ch_info->device_parent->lo_freq + markers[m].x,
					ch_info->device_parent->adc_scale,
					m != MAX_MARKERS ? '\n' : '\0');

			if (m == 0) {
				gtk_text_buffer_set_text(priv->tbuf, text, -1);
				gtk_text_buffer_get_iter_at_line(priv->tbuf, &iter, 1);
			} else {
				gtk_text_buffer_insert(priv->tbuf, &iter, text, -1);
			}
		}
	} else {
		gtk_text_buffer_set_text(priv->tbuf, "No markers active", 17);
	}
}

static void call_all_transform_functions(OscPlotPrivate *priv)
{
	TrList *tr_list = priv->transform_list;
	Transform *tr;
	int i = 0;

	if (priv->redraw_function <= 0)
		return;

	for (; i < tr_list->size; i++) {
		tr = tr_list->transforms[i];
		Transform_update_output(tr);
		if (tr->has_the_marker)
			draw_marker_values(priv, tr);
	}
}

static int enabled_channels_of_device(GtkTreeView *treeview, const char *dev_name)
{
	GtkTreeIter iter;
	GtkTreeIter child_iter;
	GtkTreeModel *model;
	gboolean next_iter;
	gboolean next_child_iter;
	char *str_device;
	gboolean enabled;
	int num_enabled = 0;

	model = gtk_tree_view_get_model(treeview);
	if (!gtk_tree_model_get_iter_first(model, &iter))
		return 0;

	next_iter = true;
	while (next_iter) {
		gtk_tree_model_iter_children(model, &child_iter, &iter);
		gtk_tree_model_get(model, &iter, ELEMENT_NAME, &str_device, -1);
		if (!strcmp(dev_name, str_device)) {
			next_child_iter = true;
			while (next_child_iter) {
				gtk_tree_model_get(model, &child_iter, CHANNEL_ACTIVE, &enabled, -1);
				if (enabled)
					num_enabled++;
				next_child_iter = gtk_tree_model_iter_next(model, &child_iter);
			}
		}
		next_iter = gtk_tree_model_iter_next(model, &iter);
		g_free(str_device);
	}

	return num_enabled;
}

struct params {
	OscPlot *plot;
	int plot_type;
	int enabled_channels;
	void *ch_pair_ref;
};

static void channels_transform_assignment(GtkTreeModel *model,
	GtkTreeIter *iter, void *user_data)
{
	struct params *prm = user_data;
	struct channel_settings *settings;
	gboolean enabled;
	void *ch_ref;

	gtk_tree_model_get(model, iter, ELEMENT_REFERENCE, &ch_ref, CHANNEL_ACTIVE,
		&enabled, CHANNEL_SETTINGS, &settings, -1);

	switch (prm->plot_type) {
		case TIME_PLOT:
			if (enabled)
				add_transform_to_list(prm->plot, ch_ref, NULL, TIME_TRANSFORM, settings);
			break;
		case FFT_PLOT:
			if (prm->enabled_channels == 1 && enabled) {
				add_transform_to_list(prm->plot, ch_ref, NULL, FFT_TRANSFORM, settings);
			} else if (prm->enabled_channels == 2 && enabled) {
				if (!prm->ch_pair_ref)
					prm->ch_pair_ref = ch_ref;
				else
					add_transform_to_list(prm->plot, prm->ch_pair_ref, ch_ref, COMPLEX_FFT_TRANSFORM, settings);
			}
			break;
		case XY_PLOT:
			if (prm->enabled_channels == 2 && enabled) {
				if (!prm->ch_pair_ref)
					prm->ch_pair_ref = ch_ref;
				else
					add_transform_to_list(prm->plot, prm->ch_pair_ref, ch_ref, CONSTELLATION_TRANSFORM, settings);
			}
			break;
		default:
			break;
	}
}

static void devices_transform_assignment(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview;
	GtkTreeIter iter;
	GtkTreeModel *model;

	struct params prm;
	int i;

	treeview = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(treeview);
	if (!gtk_tree_model_get_iter_first(model, &iter))
		return;

	prm.plot = plot;
	prm.plot_type = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain));
	prm.enabled_channels = 0;
	prm.ch_pair_ref = NULL;
	for (i = 0; i < num_devices; i++){
		prm.enabled_channels = enabled_channels_of_device(treeview, device_list[i].device_name);
		foreach_channel_iter_of_device(GTK_TREE_VIEW(priv->channel_list_view),
			&device_list[i], *channels_transform_assignment, &prm);
	}
}

static void remove_all_transforms(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	while (priv->transform_list->size)
		remove_transform_from_list(plot, priv->transform_list->transforms[0]);
}

static void auto_scale_databox(OscPlotPrivate *priv, GtkDatabox *box)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enable_auto_scale)))
		return;

	/* Auto scale every 10 seconds */
	if ((priv->frame_counter == 0) || (priv->do_a_rescale_flag == 1)) {
		priv->do_a_rescale_flag = 0;
		rescale_databox(priv, box, 0.05);
	}
}

static void fps_counter(OscPlotPrivate *priv)
{
	time_t t;

	priv->frame_counter++;
	t = time(NULL);
	if (t - priv->last_update >= 10) {
		printf("FPS: %d\n", priv->frame_counter / 10);
		priv->frame_counter = 0;
		priv->last_update = t;
	}
}

static gboolean plot_redraw(OscPlotPrivate *priv)
{
	if (!GTK_IS_DATABOX(priv->databox))
		return FALSE;
	auto_scale_databox(priv, GTK_DATABOX(priv->databox));
	gtk_widget_queue_draw(priv->databox);
	fps_counter(priv);
	if (priv->stop_redraw == TRUE)
		priv->redraw_function = 0;

	return !priv->stop_redraw;
}

static void capture_start(OscPlotPrivate *priv)
{
	if (priv->redraw_function) {
		priv->stop_redraw = FALSE;
	} else {
		priv->stop_redraw = FALSE;
		priv->redraw_function = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE, 50, (GSourceFunc) plot_redraw, priv, NULL);
	}
}

static void plot_setup(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	TrList *tr_list = priv->transform_list;
	Transform *transform;
	struct extra_info *ch_info;
	gfloat *transform_x_axis;
	gfloat *transform_y_axis;
	int max_x_axis = 0;
	gfloat max_adc_freq = 0;
	GtkDataboxGraph *graph;
	const char *empty_text = " ";
	int i;

	gtk_databox_graph_remove_all(GTK_DATABOX(priv->databox));

	/* Remove FFT Marker info */
	if (priv->tbuf)
		gtk_text_buffer_set_text(priv->tbuf, empty_text, -1);

	priv->markers_copy = NULL;

	for (i = 0; i < tr_list->size; i++) {
		transform = tr_list->transforms[i];
		Transform_setup(transform);
		transform_x_axis = Transform_get_x_axis_ref(transform);
		transform_y_axis = Transform_get_y_axis_ref(transform);

		if (strcmp(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->plot_type)), "Lines"))
			graph = gtk_databox_points_new(transform->y_axis_size, transform_x_axis, transform_y_axis, transform->graph_color, 3);
		else
			graph = gtk_databox_lines_new(transform->y_axis_size, transform_x_axis, transform_y_axis, transform->graph_color, 1);

		ch_info = transform->channel_parent->extra_field;
		if (transform->x_axis_size > max_x_axis)
			max_x_axis = transform->x_axis_size;
		if (ch_info->device_parent->adc_freq > max_adc_freq)
			max_adc_freq = ch_info->device_parent->adc_freq;

		if (priv->active_transform_type == FFT_TRANSFORM || priv->active_transform_type == COMPLEX_FFT_TRANSFORM)
			add_markers(plot, transform);

		gtk_databox_graph_add(GTK_DATABOX(priv->databox), graph);
	}
	if (!priv->profile_loaded_scale) {
		if (priv->active_transform_type == TIME_TRANSFORM &&
			!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enable_auto_scale)))
			gtk_databox_set_total_limits(GTK_DATABOX(priv->databox), 0.0, max_x_axis,
				(int)(gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->y_axis_max))),
				(int)(gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->y_axis_min))));
		else if (priv->active_transform_type == CONSTELLATION_TRANSFORM &&
			!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enable_auto_scale)))
			gtk_databox_set_total_limits(GTK_DATABOX(priv->databox), -1000.0, 1000.0, 1000, -1000);
	}

	osc_plot_update_rx_lbl(plot, FORCE_UPDATE);
}

static void capture_button_clicked_cb(GtkToggleToolButton *btn, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;
	gboolean button_state;

	button_state = gtk_toggle_tool_button_get_active(btn);

 	if (button_state) {
		gtk_widget_set_tooltip_text(GTK_WIDGET(btn), "Capture / Stop");
		collect_parameters_from_plot(plot);
		remove_all_transforms(plot);
		devices_transform_assignment(plot);

		g_mutex_trylock(&priv->g_marker_copy_lock);

		g_signal_emit(plot, oscplot_signals[CAPTURE_EVENT_SIGNAL], 0, button_state);

		plot_setup(plot);

		add_grid(plot);
		gtk_widget_queue_draw(priv->databox);
		priv->frame_counter = 0;
		capture_start(priv);
	} else {
		priv->stop_redraw = TRUE;
		dispose_parameters_from_plot(plot);

		g_mutex_trylock(&priv->g_marker_copy_lock);
		g_mutex_unlock(&priv->g_marker_copy_lock);

		g_signal_emit(plot, oscplot_signals[CAPTURE_EVENT_SIGNAL], 0, button_state);
	}
}

static void fullscreen_button_clicked_cb(GtkToggleToolButton *btn, gpointer data)
{
	OscPlot *plot = data;

	if (gtk_toggle_tool_button_get_active(btn))
		gtk_window_fullscreen(GTK_WINDOW(plot->priv->window));
	else
		gtk_window_unfullscreen(GTK_WINDOW(plot->priv->window));
}

static void iter_children_sensitivity_update(GtkTreeModel *model, GtkTreeIter *iter, void *data)
{
	GtkTreeIter child;
	gboolean next_iter;
	gboolean state;
	gboolean *forced_state = data;

	gtk_tree_model_get(model, iter, DEVICE_ACTIVE, &state, -1);
	if (!gtk_tree_model_iter_children(model, &child, iter))
		return;

	if (forced_state)
		state = *forced_state;

	next_iter = true;
	while (next_iter) {
		gtk_tree_store_set(GTK_TREE_STORE(model), &child, SENSITIVE, state, -1);
		next_iter = gtk_tree_model_iter_next(model, &child);
	}
}

static void iter_children_plot_type_update(GtkTreeModel *model, GtkTreeIter *iter, void *data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;
	GtkTreeIter child;
	gboolean next_iter;

	if (!gtk_tree_model_iter_children(model, &child, iter))
		return;

	next_iter = true;
	while (next_iter) {
		gtk_tree_store_set(GTK_TREE_STORE(model), &child,
			PLOT_TYPE, gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain)), -1);
		next_iter = gtk_tree_model_iter_next(model, &child);
	}
}

static void iter_children_icon_color_update(GtkTreeModel *model, GtkTreeIter *iter, void *data)
{
	GtkTreeIter child;
	gboolean next_iter;
	GdkPixbuf *icon;
	struct channel_settings *settings;

	if (!gtk_tree_model_iter_children(model, &child, iter))
		return;

	next_iter = true;
	while (next_iter) {
		gtk_tree_model_get(model, &child, CHANNEL_SETTINGS, &settings,
				CHANNEL_COLOR_ICON, &icon, -1);
		channel_color_icon_set_color(icon, &settings->graph_color);

		next_iter = gtk_tree_model_iter_next(model, &child);
	}
}

static void device_toggled(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer plot)
{
	OscPlotPrivate *priv = ((OscPlot *)plot)->priv;
	GtkTreePath* path = gtk_tree_path_new_from_string(pathStr);
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean active;

	model = gtk_tree_view_get_model(treeview);
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, DEVICE_ACTIVE, &active, -1);
	if (active)
		return;
	gtk_tree_store_set(GTK_TREE_STORE(model), &iter, DEVICE_ACTIVE, true, -1);

	/* When one device is enabled, disable the others */
	GtkTreePath *local_path;
	GtkTreeIter local_iter;
	gboolean next_iter;

	gtk_tree_model_get_iter_first(model, &local_iter);
	next_iter = true;
	while (next_iter) {
		local_path = gtk_tree_model_get_path(model, &local_iter);
		if (gtk_tree_path_compare(local_path, path) != 0)
				gtk_tree_store_set(GTK_TREE_STORE(model), &local_iter, DEVICE_ACTIVE, false, -1);
		gtk_tree_path_free(local_path);

		next_iter = gtk_tree_model_iter_next(model, &local_iter);
	}

	foreach_device_iter(treeview, *iter_children_sensitivity_update, NULL);
}

static void channel_toggled(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer plot)
{
	OscPlotPrivate *priv = ((OscPlot *)plot)->priv;
	GtkTreePath* path = gtk_tree_path_new_from_string(pathStr);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean active;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->channel_list_view));
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, CHANNEL_ACTIVE, &active, -1);
	active = !active;
	gtk_tree_store_set(GTK_TREE_STORE(model), &iter, CHANNEL_ACTIVE, active, -1);
	gtk_tree_path_free(path);

	check_valid_setup(plot);
}

static void color_icon_renderer_visibility(GtkTreeViewColumn *col,
		GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	gboolean is_channel;
	gchar plot_type;

	gtk_tree_model_get(model, iter, IS_CHANNEL, &is_channel,
			PLOT_TYPE, &plot_type, -1);

	if (is_channel && plot_type == TIME_PLOT)
		gtk_cell_renderer_set_visible(cell, TRUE);
	else
		gtk_cell_renderer_set_visible(cell, FALSE);
}

static void create_channel_list_view(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer_name;
	GtkCellRenderer *renderer_ch_toggle;
	GtkCellRenderer *renderer_dev_toggle;
	GtkCellRenderer *renderer_ch_color;

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, "Channels");
	gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);

	renderer_name = gtk_cell_renderer_text_new();
	renderer_ch_toggle = gtk_cell_renderer_toggle_new();
	renderer_dev_toggle = gtk_cell_renderer_toggle_new();
	renderer_ch_color = gtk_cell_renderer_pixbuf_new();

	gtk_tree_view_column_pack_end(col, renderer_ch_color, FALSE);
	gtk_tree_view_column_pack_end(col, renderer_name, FALSE);
	gtk_tree_view_column_pack_end(col, renderer_ch_toggle, FALSE);
	gtk_tree_view_column_pack_end(col, renderer_dev_toggle, FALSE);

	gtk_cell_renderer_toggle_set_radio(GTK_CELL_RENDERER_TOGGLE(renderer_dev_toggle), TRUE);

	gtk_tree_view_column_set_attributes(col, renderer_name,
					"text", ELEMENT_NAME,
					"sensitive", SENSITIVE,
					NULL);
	gtk_tree_view_column_set_attributes(col, renderer_ch_toggle,
					"visible", IS_CHANNEL,
					"active", CHANNEL_ACTIVE,
					"sensitive", SENSITIVE,
					NULL);
	gtk_tree_view_column_set_attributes(col, renderer_dev_toggle,
					"visible", DEVICE_SELECTABLE,
					"active", DEVICE_ACTIVE,
					"sensitive", SENSITIVE,
					NULL);
	gtk_tree_view_column_set_attributes(col, renderer_ch_color,
					"pixbuf", CHANNEL_COLOR_ICON,
					"sensitive", SENSITIVE,
					NULL);
	gtk_tree_view_column_set_cell_data_func(col, renderer_ch_color,
					*color_icon_renderer_visibility,
					NULL,
					NULL);

	g_object_set(renderer_ch_color, "follow-state", FALSE, NULL);
	g_signal_connect(G_OBJECT(renderer_ch_toggle), "toggled", G_CALLBACK(channel_toggled), plot);
	g_signal_connect(G_OBJECT(renderer_dev_toggle), "toggled", G_CALLBACK(device_toggled), plot);
}

static void * channel_settings_new(OscPlot *plot)
{
	struct channel_settings *settings;
	GSList *list = plot->priv->ch_settings_list;
	static int index;

	if (list == NULL)
		index = 0;

	settings = malloc(sizeof(struct channel_settings));
	list = g_slist_prepend(list, settings);
	plot->priv->ch_settings_list = list;

	settings->graph_color.red = color_graph[index % NUM_GRAPH_COLORS].red;
	settings->graph_color.green = color_graph[index % NUM_GRAPH_COLORS].green;
	settings->graph_color.blue = color_graph[index % NUM_GRAPH_COLORS].blue;
	settings->apply_inverse_funct = false;
	settings->apply_multiply_funct = false;
	settings->apply_add_funct = false;
	settings->multiply_value = 0.0;
	settings->add_value = 0.0;
	index++;

	return settings;
}

static GdkPixbuf * channel_color_icon_new(OscPlot *plot)
{
	DIR *d;

	/* Check the local icons folder first */
	d = opendir("./icons");
	if (!d) {
		return gtk_image_get_pixbuf(GTK_IMAGE(gtk_image_new_from_file(OSC_GLADE_FILE_PATH"ch_color_icon.png")));

	} else {
		return gtk_image_get_pixbuf(GTK_IMAGE(gtk_image_new_from_file("icons/ch_color_icon.png")));
	}
	closedir(d);



}

static void channel_color_icon_set_color(GdkPixbuf *pb, GdkColor *color)
{
	guchar *pixel;
	int rowstride;
	int ht;
	int i, j;
	const char border = 2;

	pixel = gdk_pixbuf_get_pixels(pb);
	ht = gdk_pixbuf_get_height(pb);
	rowstride = gdk_pixbuf_get_rowstride(pb);

	for (i = border; i < ht - border; i++)
		for (j = border * 4; j < rowstride - border * 4; j += 4) {
			pixel[i * rowstride + j + 0] = color->red / 255;
			pixel[i * rowstride + j + 1] = color->green / 255;
			pixel[i * rowstride + j + 2] = color->blue / 255;
			pixel[i * rowstride + j + 3] = 255;
		}
}

static void device_list_treeview_init(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeIter iter;
	GtkTreeIter child;
	GtkTreeStore *treestore;
	GdkPixbuf *new_icon;
	GdkColor *icon_color;
	struct channel_settings *new_settings;
	int i, j;

	treestore = GTK_TREE_STORE(gtk_tree_view_get_model(treeview));
	for (i = 0; i < num_devices; i++) {
		gtk_tree_store_append(treestore, &iter, NULL);
		gtk_tree_store_set(treestore, &iter, ELEMENT_NAME, device_list[i].device_name,
			IS_DEVICE, TRUE, DEVICE_ACTIVE, !i, ELEMENT_REFERENCE, &device_list[i], SENSITIVE, true, -1);
		for (j = 0; j < device_list[i].num_channels; j++) {
			if (strcmp("in_timestamp", device_list[i].channel_list[j].name) != 0) {
				new_settings = channel_settings_new(plot);
				new_icon = channel_color_icon_new(plot);
				icon_color = &new_settings->graph_color;
				channel_color_icon_set_color(new_icon, icon_color);
				gtk_tree_store_append(treestore, &child, &iter);
				gtk_tree_store_set(treestore, &child,
					ELEMENT_NAME, device_list[i].channel_list[j].name,
					IS_CHANNEL, TRUE,
					CHANNEL_ACTIVE, FALSE,
					ELEMENT_REFERENCE, &device_list[i].channel_list[j],
					CHANNEL_SETTINGS, new_settings,
					CHANNEL_COLOR_ICON, new_icon,
					SENSITIVE, true,
					PLOT_TYPE, TIME_PLOT,
					-1);
			}
		}
	}
	create_channel_list_view(plot);
}

static void saveas_device_changed_cb(GtkComboBoxText *box, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	struct _device_list *device;
	struct iio_channel_info *channel;
	GtkWidget *parent;
	GtkWidget *ch_checkbtn;
	gchar *active_device;
	int i, d;

	parent = gtk_widget_get_parent(priv->saveas_channels_list);
	gtk_widget_destroy(priv->saveas_channels_list);
	priv->saveas_channels_list = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(parent), priv->saveas_channels_list, FALSE, TRUE, 0);

	active_device = gtk_combo_box_text_get_active_text(box);
	d = device_find_by_name(active_device);
	g_free(active_device);
	if (d < 0)
		return;
	device = &device_list[d];

	for (i = device->num_channels - 1; i >= 0; i--) {
		channel = &device->channel_list[i];
		ch_checkbtn = gtk_check_button_new_with_label(channel->name);
		gtk_box_pack_end(GTK_BOX(priv->saveas_channels_list), ch_checkbtn, FALSE, TRUE, 0);
	}
	gtk_widget_show_all(priv->saveas_channels_list);
}

static void saveas_channels_list_fill(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkWidget *ch_window;
	GtkWidget *vbox;
	int dev;

	ch_window = priv->viewport_saveas_channels;
	vbox = gtk_vbox_new(FALSE, 10);
	gtk_container_add(GTK_CONTAINER(ch_window), vbox);
	priv->device_combobox = gtk_combo_box_text_new();
	gtk_box_pack_start(GTK_BOX(vbox), priv->device_combobox, FALSE, TRUE, 0);
	priv->saveas_channels_list = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_end(GTK_BOX(vbox), priv->saveas_channels_list, FALSE, TRUE, 0);

	for (dev = 0; dev < num_devices; dev++)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(priv->device_combobox),
			device_list[dev].device_name);
	if (!num_devices)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(priv->device_combobox),
			"No Devices Available");
	g_signal_connect(priv->device_combobox, "changed",
		G_CALLBACK(saveas_device_changed_cb), (gpointer)plot);
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->device_combobox), 0);
	gtk_widget_set_size_request(priv->viewport_saveas_channels, -1, 150);
	gtk_widget_show_all(vbox);
}

static gboolean capture_button_icon_transform(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer data)
{
	if (((OscPlot *)data)->priv->deactivate_capture_btn_flag == 1)
		return FALSE;

	if (g_value_get_boolean(source_value))
		g_value_set_static_string(target_value, "gtk-stop");
	else
		g_value_set_static_string(target_value, "gtk-media-play");

	return TRUE;
}

static gboolean fullscreen_button_icon_transform(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer user_data)
{
	if (g_value_get_boolean(source_value))
		g_value_set_static_string(target_value, "gtk-leave-fullscreen");
	else
		g_value_set_static_string(target_value, "gtk-fullscreen");

	return TRUE;
}

/*
 * Fill in an array, of about num times
 */
static void fill_axis(gfloat *buf, gfloat start, gfloat inc, int num)
{
	int i;
	gfloat val = start;

	for (i = 0; i < num; i++) {
		buf[i] = val;
		val += inc;
	}

}

static void add_grid(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	/*
	This would be a better general solution, but it doesn't really work well
	gfloat left, right, top, bottom;
	int x, y;

	gtk_databox_get_total_limits(GTK_DATABOX(databox), &left, &right, &top, &bottom);
	y = fill_axis(gridy, top, bottom, 20);
	x = fill_axis(gridx, left, right, 20);
	grid = gtk_databox_grid_array_new (y, x, gridy, gridx, &color_grid, 1);
	*/

	if (priv->active_transform_type == FFT_TRANSFORM) {
		fill_axis(priv->gridx, 0, 10, 15);
		fill_axis(priv->gridy, 10, -10, 15);
		priv->grid = gtk_databox_grid_array_new (15, 15, priv->gridy, priv->gridx, &color_grid, 1);
	}else if (priv->active_transform_type == COMPLEX_FFT_TRANSFORM) {
		fill_axis(priv->gridx, -30, 10, 15);
		fill_axis(priv->gridy, 10, -10, 15);
		priv->grid = gtk_databox_grid_array_new (15, 15, priv->gridy, priv->gridx, &color_grid, 1);
	}
	 else if (priv->active_transform_type == CONSTELLATION_TRANSFORM) {
		fill_axis(priv->gridx, -80000, 10000, 18);
		fill_axis(priv->gridy, -80000, 10000, 18);
		priv->grid = gtk_databox_grid_array_new (18, 18, priv->gridy, priv->gridx, &color_grid, 1);
	} else if (priv->active_transform_type == TIME_TRANSFORM) {
		fill_axis(priv->gridx, 0, 100, 5);
		fill_axis(priv->gridy, -80000, 10000, 18);
		priv->grid = gtk_databox_grid_array_new (18, 5, priv->gridy, priv->gridx, &color_grid, 1);
	} else if (priv->active_transform_type == NO_TRANSFORM_TYPE) {
		gfloat left, right, top, bottom;

		gtk_databox_get_total_limits(GTK_DATABOX(priv->databox), &left, &right, &top, &bottom);
		fill_axis(priv->gridy, top, bottom, 20);
		fill_axis(priv->gridx, left, right, 20);
		priv->grid = gtk_databox_grid_array_new (18, 5, priv->gridy, priv->gridx, &color_grid, 1);
	}

	gtk_databox_graph_add(GTK_DATABOX(priv->databox), priv->grid);
	gtk_databox_graph_set_hide(priv->grid, !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->show_grid)));
}

static void show_grid_toggled(GtkToggleButton *btn, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;

	if (priv->grid) {
		gtk_databox_graph_set_hide(priv->grid, !gtk_toggle_button_get_active(btn));
		gtk_widget_queue_draw(priv->databox);
	}
}

static void rescale_databox(OscPlotPrivate *priv, GtkDatabox *box, gfloat border)
{
	bool fixed_aspect = (priv->active_transform_type == CONSTELLATION_TRANSFORM) ? TRUE : FALSE;

	if (fixed_aspect) {
		gfloat min_x;
		gfloat max_x;
		gfloat min_y;
		gfloat max_y;
		gfloat width;

		gint extrema_success = gtk_databox_calculate_extrema(box,
				&min_x, &max_x, &min_y, &max_y);
		if (extrema_success)
			return;
		if (min_x > min_y)
			min_x = min_y;
		if (max_x < max_y)
			max_x = max_y;

		width = max_x - min_x;
		if (width == 0)
			width = max_x;

		min_x -= border * width;
		max_x += border * width;

		gtk_databox_set_total_limits(box, min_x, max_x, max_x, min_x);

	} else {
		gtk_databox_auto_rescale(box, border);
	}
}

static void zoom_fit(GtkButton *btn, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;

	rescale_databox(priv, GTK_DATABOX(priv->databox), 0.05);
}

static void zoom_in(GtkButton *btn, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;
	bool fixed_aspect = (priv->active_transform_type == CONSTELLATION_TRANSFORM) ? TRUE : FALSE;
	gfloat left, right, top, bottom;
	gfloat width, height;

	gtk_databox_get_visible_limits(GTK_DATABOX(priv->databox), &left, &right, &top, &bottom);
	width = right - left;
	height = bottom - top;
	left += width * 0.25;
	right -= width * 0.25;
	top += height * 0.25;
	bottom -= height * 0.25;

	if (fixed_aspect) {
		gfloat diff;
		width *= 0.5;
		height *= -0.5;
		if (height > width) {
			diff = width - height;
			left -= diff * 0.5;
			right += diff * 0.5;
		} else {
			diff = height - width;
			bottom += diff * 0.5;
			top -= diff * 0.5;
		}
	}

	gtk_databox_set_visible_limits(GTK_DATABOX(priv->databox), left, right, top, bottom);
}

static void zoom_out(GtkButton *btn, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;
	bool fixed_aspect = (priv->active_transform_type == CONSTELLATION_TRANSFORM) ? TRUE : FALSE;
	gfloat left, right, top, bottom;
	gfloat t_left, t_right, t_top, t_bottom;
	gfloat width, height;

	gtk_databox_get_visible_limits(GTK_DATABOX(priv->databox), &left, &right, &top, &bottom);
	width = right - left;
	height = bottom - top;
	left -= width * 0.25;
	right += width * 0.25;
	top -= height * 0.25;
	bottom += height * 0.25;

	gtk_databox_get_total_limits(GTK_DATABOX(priv->databox), &t_left, &t_right, &t_top, &t_bottom);
	if (left < right) {
		if (left < t_left)
			left = t_left;
		if (right > t_right)
			right = t_right;
	} else {
		if (left > t_left)
			left = t_left;
		if (right < t_right)
			right = t_right;
	}

	if (top < bottom) {
		if (top < t_top)
			top = t_top;
		if (bottom > t_bottom)
			bottom = t_bottom;
	} else {
		if (top > t_top)
			top = t_top;
		if (bottom < t_bottom)
			bottom = t_bottom;
	}

	if (fixed_aspect) {
		gfloat diff;
		width = right - left;
		height = top - bottom;
		if (height < width) {
			diff = width - height;
			bottom -= diff * 0.5;
			top += diff * 0.5;
			if (top < t_top) {
				bottom += t_top - top;
				top = t_top;
			}
			if (bottom > t_bottom) {
				top -= bottom - t_bottom;
				bottom = t_bottom;
			}
		} else {
			diff = height - width;
			left -= diff * 0.5;
			right += diff * 0.5;
			if (left < t_left) {
				right += t_left - left;
				left = t_left;
			}
			if (right > t_right) {
				left -= right - t_right;
				right = t_right;
			}
		}
		width = right - left;
		height = top - bottom;
	}

	gtk_databox_set_visible_limits(GTK_DATABOX(priv->databox), left, right, top, bottom);
}

static void transform_csv_print(OscPlotPrivate *priv, FILE *fp, Transform *tr)
{
	gfloat *tr_data;
	gfloat *tr_x_axis;
	int i;

	if (tr->type_id == TIME_TRANSFORM)
		fprintf(fp, "X Axis(Sample Index)    Y Axis(%s)\n", tr->channel_parent->name);
	else if (tr->type_id == FFT_TRANSFORM)
		fprintf(fp, "X Axis(Frequency)    Y Axis(FFT - %s)\n", tr->channel_parent->name);
	else if (tr->type_id == COMPLEX_FFT_TRANSFORM)
		fprintf(fp, "X Axis(Frequency)    Y Axis(Complex FFT - %s, %s)\n", tr->channel_parent->name, tr->channel_parent2->name);
	else if (tr->type_id == CONSTELLATION_TRANSFORM)
		fprintf(fp, "X Axis(%s)    Y Axis(%s)\n", tr->channel_parent2->name, tr->channel_parent->name);

	tr_x_axis = Transform_get_x_axis_ref(tr);
	tr_data = Transform_get_y_axis_ref(tr);

	if (tr_x_axis == NULL || tr_data == NULL) {
		fprintf(fp, "No data\n");
		return;
	}

	for (i = 0; i < tr->x_axis_size; i++) {
		fprintf(fp, "%g, %g,\n", tr_x_axis[i], tr_data[i]);
	}
	fprintf(fp, "\n");
}

static void plot_destroyed (GtkWidget *object, OscPlot *plot)
{
	plot->priv->stop_redraw = TRUE;
	dispose_parameters_from_plot(plot);
	remove_all_transforms(plot);
	g_slist_free_full(plot->priv->ch_settings_list, *free);
	g_mutex_trylock(&plot->priv->g_marker_copy_lock);
	g_mutex_unlock(&plot->priv->g_marker_copy_lock);

	if (plot->priv->redraw_function) {
		g_signal_emit(plot, oscplot_signals[CAPTURE_EVENT_SIGNAL], 0, FALSE);
	}
	g_signal_emit(plot, oscplot_signals[DESTROY_EVENT_SIGNAL], 0);
}

static void copy_channel_state_to_selection_channel(GtkTreeModel *model,
		GtkTreeIter *iter, void *user_data)
{
	OscPlot *plot = user_data;
	OscPlotPrivate *priv = plot->priv;
	struct iio_channel_info *channel;
	gboolean active_state;
	GList *ch_checkbtns = NULL;
	GList *node;
	GtkToggleButton *btn;
	int index = 0;

	gtk_tree_model_get(model, iter, ELEMENT_REFERENCE, &channel, CHANNEL_ACTIVE, &active_state, -1);

	/* Get user channel selection from GUI widgets */
	ch_checkbtns = gtk_container_get_children(GTK_CONTAINER(priv->saveas_channels_list));
	for (node = ch_checkbtns; node; node = g_list_next(node)) {
		btn = (GtkToggleButton *)node->data;
		if (channel->index == index) {
			gtk_toggle_button_set_active(btn, active_state);
			break;
		}
		index++;
	}
	g_list_free(ch_checkbtns);

}

static void channel_selection_set_default(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	gchar *device_name;
	int d;

	device_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->device_combobox));
	d = device_find_by_name(device_name);
	g_free(device_name);
	foreach_channel_iter_of_device(GTK_TREE_VIEW(priv->channel_list_view), &device_list[d],
		*copy_channel_state_to_selection_channel, plot);
}

static int * get_user_saveas_channel_selection(OscPlot *plot, struct _device_list *device)
{
	OscPlotPrivate *priv = plot->priv;
	GList *ch_checkbtns = NULL;
	GList *node;
	GtkToggleButton *btn;
	int *mask;
	int i = 0;

	/* Create masks for all channels */
	mask = malloc(sizeof(int) * device->num_channels);

	/* Get user channel selection from GUI widgets */
	ch_checkbtns = gtk_container_get_children(GTK_CONTAINER(priv->saveas_channels_list));
	for (node = ch_checkbtns; node; node = g_list_next(node)) {
		btn = (GtkToggleButton *)node->data;
		mask[i++] = !gtk_toggle_button_get_active(btn);
	}
	g_list_free(ch_checkbtns);

	return mask;
}

#define SAVE_AS_RAW_DATA  16
#define SAVE_AS_PLOT_DATA 17
#define SAVE_AS_PNG_IMAGE 18

static void cb_saveas_chooser(GtkToolButton *toolbutton, OscPlot *data)
{
	OscPlotPrivate *priv = data->priv;

	gtk_widget_show(priv->saveas_type_dialog);
}

static void saveas_dialog_show(OscPlot *plot, gint saveas_type)
{
	OscPlotPrivate *priv = plot->priv;

	gtk_file_chooser_set_action(GTK_FILE_CHOOSER (priv->saveas_dialog), GTK_FILE_CHOOSER_ACTION_SAVE);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(priv->saveas_dialog), TRUE);

	if (!priv->saveas_filename) {
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (priv->saveas_dialog), getenv("HOME"));
		gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (priv->saveas_dialog));
	} else {
		gtk_file_chooser_set_filename(GTK_FILE_CHOOSER (priv->saveas_dialog), priv->saveas_filename);
		g_free(priv->saveas_filename);
		priv->saveas_filename = NULL;
	}

	GtkWidget *save_csv = GTK_WIDGET(gtk_builder_get_object(priv->builder, "save_csv"));
	GtkWidget *save_vsa = GTK_WIDGET(gtk_builder_get_object(priv->builder, "save_vsa"));
	GtkWidget *save_mat = GTK_WIDGET(gtk_builder_get_object(priv->builder, "save_mat"));
	GtkWidget *save_png = GTK_WIDGET(gtk_builder_get_object(priv->builder, "save_png"));

	gtk_widget_show(save_csv);
	gtk_widget_show(save_vsa);
	gtk_widget_show(save_mat);
	gtk_widget_show(save_png);
	gtk_widget_hide(priv->viewport_saveas_channels);
	gtk_widget_hide(priv->saveas_select_channel_message);
	switch(saveas_type) {
		case SAVE_AS_RAW_DATA:
			gtk_widget_hide(save_png);
			gtk_widget_show(priv->viewport_saveas_channels);
			gtk_widget_show(priv->saveas_select_channel_message);
			break;
		case SAVE_AS_PLOT_DATA:
			gtk_widget_hide(save_vsa);
			gtk_widget_hide(save_mat);
			gtk_widget_hide(save_png);
			break;
		case SAVE_AS_PNG_IMAGE:
			gtk_widget_hide(save_csv);
			gtk_widget_hide(save_vsa);
			gtk_widget_hide(save_mat);
			break;
		default:
			break;
	};

	priv->active_saveas_type = saveas_type;
	channel_selection_set_default(plot);
	gtk_widget_show(priv->saveas_dialog);
}

void cb_saveas_chooser_response(GtkDialog *dialog, gint response_id, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	if (response_id == SAVE_AS_RAW_DATA ||
		response_id == SAVE_AS_PLOT_DATA ||
		response_id == SAVE_AS_PNG_IMAGE)
		saveas_dialog_show(plot, response_id);

	gtk_widget_hide(priv->saveas_type_dialog);
}

static void save_as(OscPlot *plot, const char *filename, int type)
{
	OscPlotPrivate *priv = plot->priv;
	FILE *fp;
	mat_t *mat;
	matvar_t *matvar;
	struct _device_list *device;
	struct iio_channel_info *channel;
	char tmp[100];
	int dims[2] = {-1, 1};
	double freq;
	GdkPixbuf *pixbuf;
	GError *err=NULL;
	GdkColormap *cmap;
	gint width, height;
	gboolean ret = true;
	char *name;
	gchar *active_device;
	int *save_channels_mask;
	int i, j, d;

	name = malloc(strlen(filename) + 5);
	switch(type) {
		/* Response Codes encoded in glade file */
		case GTK_RESPONSE_DELETE_EVENT:
		case GTK_RESPONSE_CANCEL:
			break;
		case SAVE_VSA:
			/* Save as Agilent VSA formatted file */
			if (!strncasecmp(&filename[strlen(filename)-4], ".txt", 4))
					strcpy(name, filename);
				else
					sprintf(name, "%s.txt", filename);
			fp = fopen(name, "w");
			if (!fp)
				break;

			active_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->device_combobox));
			d = device_find_by_name(active_device);
			g_free(active_device);
			if (d < 0)
				break;
			device = &device_list[d];

			/* Find which channel need to be saved */
			save_channels_mask = get_user_saveas_channel_selection(plot, device);

			/* Make a VSA file header */
			fprintf(fp, "InputZoom\tTRUE\n");
			fprintf(fp, "InputCenter\t0\n");
			fprintf(fp, "InputRange\t1\n");
			fprintf(fp, "InputRefImped\t50\n");
			fprintf(fp, "XStart\t0\n");
			if (!strcmp(device->adc_scale, "M"))
				freq = device->adc_freq * 1000000;
			else if (!strcmp(device->adc_scale, "k"))
				freq = device->adc_freq * 1000;
			else {
				printf("error in writing\n");
				break;
			}
			fprintf(fp, "XDelta\t%-.17f\n", 1.0/freq);
			fprintf(fp, "XDomain\t2\n");
			fprintf(fp, "XUnit\tSec\n");
			fprintf(fp, "YUnit\tV\n");
			fprintf(fp, "FreqValidMax\t%e\n", freq / 2);
			fprintf(fp, "FreqValidMin\t-%e\n", freq / 2);
			fprintf(fp, "Y\n");

			/* Start writing the samples */
			for (i = 0; i < device->sample_count; i++) {
				for (j = 0; j < device->num_channels; j++) {
					channel = &device->channel_list[j];
					if (!channel->enabled || save_channels_mask[j] == 1)
						continue;
					fprintf(fp, "%g\t", CHANNEL_EXTRA_FIELD(channel)->data_ref[i]);
				}
				fprintf(fp, "\n");
			}
			fprintf(fp, "\n");
			fclose(fp);
			free(save_channels_mask);

			break;
		case SAVE_CSV:
			/* save comma separated values (csv) */
			if (!strncasecmp(&filename[strlen(filename)-4], ".csv", 4))
					strcpy(name, filename);
				else
					sprintf(name, "%s.csv", filename);
			fp = fopen(name, "w");
			if (!fp)
				break;
			if (priv->active_saveas_type == SAVE_AS_RAW_DATA) {
				active_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->device_combobox));
				d = device_find_by_name(active_device);
				g_free(active_device);
				if (d < 0)
				break;
				device = &device_list[d];

				/* Find which channel need to be saved */
				save_channels_mask = get_user_saveas_channel_selection(plot, device);

				for (i = 0; i < device->sample_count; i++) {
					for (j = 0; j < device->num_channels; j++) {
						channel = &device->channel_list[j];
						if (!channel->enabled || save_channels_mask[j] == 1)
							continue;
						fprintf(fp, "%g, ", CHANNEL_EXTRA_FIELD(channel)->data_ref[i]);
					}
					fprintf(fp, "\n");
				}
				fprintf(fp, "\n");
				free(save_channels_mask);
			} else {
				for (i = 0; i < priv->transform_list->size; i++) {
						transform_csv_print(priv, fp, priv->transform_list->transforms[i]);
				}
			}
			fprintf(fp, "\n");
			fclose(fp);
			break;

		case SAVE_PNG:
			/* save png */
			if (!strncasecmp(&filename[strlen(filename)-4], ".png", 4))
					strcpy(name, filename);
				else
					sprintf(name, "%s.png", filename);
			cmap = gdk_window_get_colormap(
					GDK_DRAWABLE(gtk_widget_get_window(priv->capture_graph)));
			gdk_drawable_get_size(GDK_DRAWABLE(gtk_widget_get_window(priv->capture_graph)),
					&width, &height);
			pixbuf = gdk_pixbuf_get_from_drawable(NULL,
					GDK_DRAWABLE(gtk_widget_get_window(priv->capture_graph)),
					cmap, 0, 0, 0, 0, width, height);
			if (pixbuf)
				ret = gdk_pixbuf_save(pixbuf, name, "png", &err, NULL);
			if (!pixbuf || !ret)
				printf("error creating %s\n", filename);
			break;

		case SAVE_MAT:
			/* Matlab file
			 * http://na-wiki.csc.kth.se/mediawiki/index.php/MatIO
			 */
			 if (!strncasecmp(&filename[strlen(filename)-4], ".mat", 4))
					strcpy(name, filename);
				else
					sprintf(name, "%s.mat", filename);
			mat = Mat_Open(name, MAT_ACC_RDWR);
			if (!mat)
				break;

			active_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->device_combobox));
			d = device_find_by_name(active_device);
			g_free(active_device);
			if (d < 0)
			break;
			device = &device_list[d];

			/* Find which channel need to be saved */
			save_channels_mask = get_user_saveas_channel_selection(plot, device);

			dims[0] = device->sample_count;
			for (i = 0; i < device->num_channels; i++) {
				channel = &device->channel_list[i];
				if (!channel->enabled || save_channels_mask[i] == 1)
					continue;
				sprintf(tmp, "%s:%s", device->device_name, channel->name);
				matvar = Mat_VarCreate(tmp, MAT_C_SINGLE, MAT_T_SINGLE, 2, dims,
					CHANNEL_EXTRA_FIELD(channel)->data_ref, 0);
				if (!matvar)
					printf("error creating matvar on channel %s\n", tmp);
				else {
					Mat_VarWrite(mat, matvar, 0);
					Mat_VarFree(matvar);
				}
			}
			free(save_channels_mask);

			Mat_Close(mat);
			break;

		default:
			printf("SaveAs response: %i\n", type);
	}
}

void cb_saveas_response(GtkDialog *dialog, gint response_id, OscPlot *plot)
{
	/* Save as Dialog */
	OscPlotPrivate *priv = plot->priv;

	priv->saveas_filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (priv->saveas_dialog));
	if (priv->saveas_filename == NULL)
		goto hide_dialog;
	save_as(plot, priv->saveas_filename, response_id);

hide_dialog:
	gtk_widget_hide(priv->saveas_dialog);
}

static void enable_auto_scale_cb(GtkToggleButton *button, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enable_auto_scale))) {
		priv->do_a_rescale_flag = 1;
		gtk_widget_set_sensitive(plot->priv->y_axis_max, FALSE);
		gtk_widget_set_sensitive(plot->priv->y_axis_min, FALSE);
	} else
	{
		gtk_widget_set_sensitive(plot->priv->y_axis_max, TRUE);
		gtk_widget_set_sensitive(plot->priv->y_axis_min, TRUE);
	}
}

static void max_y_axis_cb(GtkSpinButton *btn, OscPlot *plot)
{
	GtkDatabox *box;
	gfloat min_x;
	gfloat max_x;
	gfloat min_y;
	gfloat max_y;

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(plot->priv->enable_auto_scale)))
		return;
	box = GTK_DATABOX(plot->priv->databox);
	gtk_databox_get_total_limits(box, &min_x, &max_x, &max_y, &min_y);
	max_y = gtk_spin_button_get_value(btn);
	min_y = gtk_spin_button_get_value(GTK_SPIN_BUTTON(plot->priv->y_axis_min));
	gtk_databox_set_total_limits(box, min_x, max_x, max_y, min_y);
}

static void min_y_axis_cb(GtkSpinButton *btn, OscPlot *plot)
{
	GtkDatabox *box;
	gfloat min_x;
	gfloat max_x;
	gfloat min_y;
	gfloat max_y;

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(plot->priv->enable_auto_scale)))
		return;
	box = GTK_DATABOX(plot->priv->databox);
	gtk_databox_get_total_limits(box, &min_x, &max_x, &max_y, &min_y);
	min_y = gtk_spin_button_get_value(btn);
	max_y = gtk_spin_button_get_value(GTK_SPIN_BUTTON(plot->priv->y_axis_max));
	gtk_databox_set_total_limits(box, min_x, max_x, max_y, min_y);
}

static gboolean get_iter_by_name(GtkTreeView *tree, GtkTreeIter *iter, char *dev_name, char *ch_name)
{
	GtkTreeModel *model;
	GtkTreeIter dev_iter;
	GtkTreeIter ch_iter;
	gboolean next_dev_iter;
	gboolean next_ch_iter;
	char *device;
	char *channel;

	model = gtk_tree_view_get_model(tree);
	if (!gtk_tree_model_get_iter_first(model, &dev_iter))
		return FALSE;
	if (dev_name == NULL)
		return FALSE;

	next_dev_iter = true;
	while (next_dev_iter) {
		gtk_tree_model_iter_children(model, &ch_iter, &dev_iter);
		gtk_tree_model_get(model, &dev_iter, ELEMENT_NAME, &device, -1);
		if (!strcmp(dev_name, device)) {
			g_free(device);
			if (ch_name == NULL) {
				*iter = dev_iter;
				return TRUE;
			}
			next_ch_iter = true;
			while (next_ch_iter) {
				gtk_tree_model_get(model, &ch_iter, ELEMENT_NAME, &channel, -1);
				if (!strcmp(ch_name, channel)) {
					g_free(channel);
					*iter = ch_iter;
					return TRUE;
				}
				next_ch_iter = gtk_tree_model_iter_next(model, &ch_iter);
			}
		}
		next_dev_iter = gtk_tree_model_iter_next(model, &dev_iter);
	}

	return FALSE;
}

static void plot_profile_save(OscPlot *plot, char *filename)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *tree = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeModel *model;
	GtkTreeIter dev_iter;
	GtkTreeIter ch_iter;
	gboolean next_dev_iter;
	gboolean next_ch_iter;
	struct _device_list *dev;
	struct iio_channel_info *ch;
	gboolean expanded;
	gboolean device_active;
	gboolean ch_enabled;
	struct channel_settings *csettings;
	FILE *fp;

	model = gtk_tree_view_get_model(tree);

	fp = fopen(filename, "a");
	if (!fp) {
		fprintf(stderr, "Failed to open %s : %s\n", filename, strerror(errno));
		return;
	}
	fprintf(fp, "\n[%s%d]\n", CAPTURE_CONF, priv->object_id);

	int tmp_int;
	float tmp_float;
	gchar *tmp_string;

	fprintf(fp, "domain=");
	tmp_int = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain));
	if (tmp_int == FFT_PLOT)
		fprintf(fp, "fft\n");
	else if (tmp_int == XY_PLOT)
		fprintf(fp, "constellation\n");
	else if (tmp_int == TIME_PLOT)
		fprintf(fp, "time\n");

	tmp_int = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->sample_count_widget));
	fprintf(fp, "sample_count=%d\n", tmp_int);

	tmp_int = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->fft_size_widget)));
	fprintf(fp, "fft_size=%d\n", tmp_int);

	tmp_int = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->fft_avg_widget));
	fprintf(fp, "fft_avg=%d\n", tmp_int);

	tmp_float = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->fft_pwr_offset_widget));
	fprintf(fp, "fft_pwr_offset=%f\n", tmp_float);

	tmp_string = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->plot_type));
	fprintf(fp, "graph_type=%s\n", tmp_string);
	g_free(tmp_string);

	tmp_int = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->show_grid));
	fprintf(fp, "show_grid=%d\n", tmp_int);

	tmp_int = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enable_auto_scale));
	fprintf(fp, "enable_auto_scale=%d\n", tmp_int);

	tmp_float = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->y_axis_max));
	fprintf(fp, "user_y_axis_max=%f\n", tmp_float);

	tmp_float = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->y_axis_min));
	fprintf(fp, "user_y_axis_min=%f\n", tmp_float);

	gfloat left, right, top, bottom;
	gtk_databox_get_visible_limits(GTK_DATABOX(priv->databox), &left, &right, &top, &bottom);
	fprintf(fp, "x_axis_min=%f\n", left);
	fprintf(fp, "x_axis_max=%f\n", right);
	fprintf(fp, "y_axis_min=%f\n", bottom);
	fprintf(fp, "y_axis_max=%f\n", top);

	next_dev_iter = gtk_tree_model_get_iter_first(model, &dev_iter);
	while (next_dev_iter) {
		gtk_tree_model_get(model, &dev_iter, ELEMENT_REFERENCE, &dev,
			DEVICE_ACTIVE, &device_active, -1);
		expanded = gtk_tree_view_row_expanded(tree, gtk_tree_model_get_path(model, &dev_iter));
		fprintf(fp, "%s.expanded=%d\n", dev->device_name, (expanded) ? 1 : 0);
		fprintf(fp, "%s.active=%d\n", dev->device_name, (device_active) ? 1 : 0);
		next_ch_iter = gtk_tree_model_iter_children(model, &ch_iter, &dev_iter);
		while (next_ch_iter) {
			gtk_tree_model_get(model, &ch_iter, ELEMENT_REFERENCE, &ch,
				CHANNEL_ACTIVE, &ch_enabled, CHANNEL_SETTINGS, &csettings, -1);
			fprintf(fp, "%s.%s.enabled=%d\n", dev->device_name, ch->name, (ch_enabled) ? 1 : 0);
			fprintf(fp, "%s.%s.color_red=%d\n", dev->device_name, ch->name, csettings->graph_color.red);
			fprintf(fp, "%s.%s.color_green=%d\n", dev->device_name, ch->name, csettings->graph_color.green);
			fprintf(fp, "%s.%s.color_blue=%d\n", dev->device_name, ch->name, csettings->graph_color.blue);
			fprintf(fp, "%s.%s.math_apply_inverse_funct=%d\n", dev->device_name, ch->name, csettings->apply_inverse_funct);
			fprintf(fp, "%s.%s.math_apply_multiply_funct=%d\n", dev->device_name, ch->name, csettings->apply_multiply_funct);
			fprintf(fp, "%s.%s.math_apply_add_funct=%d\n", dev->device_name, ch->name, csettings->apply_add_funct);
			fprintf(fp, "%s.%s.math_multiply_value=%f\n", dev->device_name, ch->name, csettings->multiply_value);
			fprintf(fp, "%s.%s.math_add_value=%f\n", dev->device_name, ch->name, csettings->add_value);
			next_ch_iter = gtk_tree_model_iter_next(model, &ch_iter);
		}
		next_dev_iter = gtk_tree_model_iter_next(model, &dev_iter);
	}

	if (priv->marker_type == MARKER_OFF)
		fprintf(fp, "marker_type = %s\n", OFF_MRK);
	else if (priv->marker_type == MARKER_PEAK)
		fprintf(fp, "marker_type = %s\n", PEAK_MRK);
	else if (priv->marker_type == MARKER_FIXED)
		fprintf(fp, "marker_type = %s\n", FIX_MRK);
	else if (priv->marker_type == MARKER_ONE_TONE)
		fprintf(fp, "marker_type = %s\n", SINGLE_MRK);
	else if (priv->marker_type == MARKER_TWO_TONE)
		fprintf(fp, "marker_type = %s\n", DUAL_MRK);
	else if (priv->marker_type == MARKER_IMAGE)
		fprintf(fp, "marker_type = %s\n", IMAGE_MRK);

	for (tmp_int = 0; tmp_int <= MAX_MARKERS; tmp_int++) {
		if (priv->markers[tmp_int].active)
			fprintf(fp, "marker.%i = %i\n", tmp_int, priv->markers[tmp_int].bin);
	}

	fprintf(fp, "capture_started=%d\n", (priv->redraw_function) ? 1 : 0);
	fclose(fp);
}

static int comboboxtext_set_active_by_string(GtkComboBox *combo_box, const char *name)
{
	GtkTreeModel *model = gtk_combo_box_get_model(combo_box);
	GtkTreeIter iter;
	gboolean has_iter;
	char *item;

	has_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (has_iter) {
		gtk_tree_model_get(model, &iter, 0, &item, -1);
		if (strcmp(name, item) == 0) {
			g_free(item);
			gtk_combo_box_set_active_iter(combo_box, &iter);
			return 1;
		}
		has_iter = gtk_tree_model_iter_next(model, &iter);
	}

	return 0;
}

#define MATCH(s1, s2) strcmp(s1, s2) == 0
#define MATCH_N(s1, s2, n) strncmp(s1, s2, n) == 0
#define MATCH_SECT(s) strcmp(section, s) == 0
#define MATCH_NAME(n) strcmp(name, n) == 0
#define PLOT_ATTRIBUTE 0
#define DEVICE 1
#define CHANNEL 2

static int device_find_by_name(const char *name)
{
	int i;

	for (i = 0; i < num_devices; i++)
		if (strcmp(device_list[i].device_name, name) == 0)
			return i;

	return -1;
}

static int channel_find_by_name(int device_index, const char *name)
{
	int i;

	for (i = 0; i < device_list[device_index].num_channels; i++)
		if (strcmp(device_list[device_index].channel_list[i].name, name) == 0)
			return i;

	return -1;
}

static int count_char_in_string(char c, const char *s)
{
	int i;

	for (i = 0; s[i];)
		if (s[i] == c)
			i++;
		else
			s++;

	return i;
}

static int cfg_read_handler(void *user, const char* section, const char* name, const char* value)
{
	OscPlot *plot = (OscPlot *)user;
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *tree = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeStore *store = GTK_TREE_STORE(gtk_tree_view_get_model(tree));
	GtkTreeIter dev_iter, ch_iter;
	int dev, ch;
	char *dev_name, *ch_name;
	char *dev_property, *ch_property;
	gboolean expanded;
	gboolean device_active;
	gboolean enabled;
	int elem_type;
	gchar **elems = NULL, **min_max = NULL;
	gfloat max_f, min_f;
	struct channel_settings *csettings;
	int ret = 1, i;
	FILE *fd;

	elem_type = count_char_in_string('.', name);
	switch(elem_type) {
		case PLOT_ATTRIBUTE:
			if (MATCH_NAME("capture_started")) {
				treeview_expand_update(plot);
				treeview_icon_color_update(plot);
				max_y_axis_cb(GTK_SPIN_BUTTON(plot->priv->y_axis_max), plot);
				min_y_axis_cb(GTK_SPIN_BUTTON(plot->priv->y_axis_min), plot);
				if (priv->read_scale_params == 4) {
					gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->y_axis_min), priv->plot_bottom);
					gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->y_axis_max), priv->plot_top);
					gtk_databox_set_total_limits(GTK_DATABOX(priv->databox), priv->plot_left,
						priv->plot_right, priv->plot_top, priv->plot_bottom);
					priv->read_scale_params = 0;
				}
				check_valid_setup(plot);
				priv->profile_loaded_scale = TRUE;
				gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->capture_button), atoi(value));
				priv->profile_loaded_scale = FALSE;
			} else if (MATCH_NAME("domain")) {
				if (!strcmp(value, "time"))
					gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_domain), TIME_PLOT);
				else if (!strcmp(value, "fft"))
					gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_domain), FFT_PLOT);
				else if (!strcmp(value, "constellation"))
					gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_domain), XY_PLOT);
			} else if (MATCH_NAME("sample_count")) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->sample_count_widget), atoi(value));
			} else if (MATCH_NAME("fft_size")) {
				ret = comboboxtext_set_active_by_string(GTK_COMBO_BOX(priv->fft_size_widget), value);
				if (ret == 0)
					goto unhandled;
				else
					ret = 1;
			} else if (MATCH_NAME("fft_avg")) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->fft_avg_widget), atoi(value));
			} else if (MATCH_NAME("fft_pwr_offset")) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->fft_pwr_offset_widget), atof(value));
			} else if (MATCH_NAME("graph_type")) {
				ret = comboboxtext_set_active_by_string(GTK_COMBO_BOX(priv->plot_type), value);
				if (ret == 0)
					goto unhandled;
				else
					ret = 1;
			} else if (MATCH_NAME("show_grid"))
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->show_grid), atoi(value));
			else if (MATCH_NAME("enable_auto_scale"))
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->enable_auto_scale), atoi(value));
			else if (MATCH_NAME("user_y_axis_max"))
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->y_axis_max), atof(value));
			else if (MATCH_NAME("user_y_axis_min"))
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->y_axis_min), atof(value));
			else if (MATCH_NAME("x_axis_min")) {
				priv->plot_left = atof(value);
				priv->read_scale_params++;
			} else if (MATCH_NAME("x_axis_max")) {
				priv->plot_right = atof(value);
				priv->read_scale_params++;
			} else if (MATCH_NAME("y_axis_min")) {
				priv->plot_bottom = atof(value);
					priv->read_scale_params++;
			} else if (MATCH_NAME("y_axis_max")) {
				priv->plot_top = atof(value);
				priv->read_scale_params++;
			} else if (MATCH_NAME("marker_type")) {
				set_marker_labels(plot, (gchar *)value, MARKER_NULL);
				for (i = 0; i <= MAX_MARKERS; i++)
					priv->markers[i].active = FALSE;
			} else if (MATCH_NAME("save_png")) {
				save_as(plot, value, SAVE_PNG);
			} else if (MATCH_NAME("cycle")) {
				i = 0;
				while (gtk_events_pending() && i < atoi(value)) {
					i++;
					gtk_main_iteration();
				}
			} else if (MATCH_NAME("save_markers")) {
				fd = fopen(value, "a");
				if (!fd)
					return 0;

				for (i = 0; i < num_devices; i++) {
					if (!strcmp(device_list[i].device_name, "cf-ad9643-core-lpc") ||
						!strcmp(device_list[i].device_name, "cf-ad9361-lpc"))
						fprintf(fd, "%f", device_list[i].lo_freq);
				}

				for (i = 0; i <= MAX_MARKERS; i++) {
					fprintf(fd, ", %f, %f", priv->markers[i].x, priv->markers[i].y);
				}
				fprintf(fd, "\n");
				fclose(fd);
			} else if (MATCH_NAME("fru_connect")) {
				if (value) {
					if (atoi(value) == 1) {
						i = fru_connect();
						if (i == GTK_RESPONSE_OK)
							ret = 1;
						else
							ret = 0;
					} else
						ret = 0;
				}
			} else if (MATCH_NAME("quit")) {
				return 0;
			} else {
				goto unhandled;
			}
			break;
		case DEVICE:
			elems = g_strsplit(name, ".", DEVICE + 1);
			dev_name = elems[0];
			dev_property = elems[1];

			/* Check for markers */
			if (MATCH(elems[0], "marker")) {
				i = atoi(elems[1]);
				priv->markers[i].bin = atoi(value);
				priv->markers[i].active = TRUE;
				break;
			} else if (MATCH(elems[0], "test")) {
				if (MATCH(elems[1], "message")) {
					create_blocking_popup(GTK_MESSAGE_QUESTION, GTK_BUTTONS_CLOSE,
						"Profile status", value);
					break;
				} else {
					goto unhandled;
				}
			}

			dev = device_find_by_name(dev_name);
			if (dev == -1)
				goto unhandled;
			if (MATCH(dev_property, "expanded")) {
				expanded = atoi(value);
				get_iter_by_name(tree, &dev_iter, dev_name, NULL);
				gtk_tree_store_set(store, &dev_iter, EXPANDED, expanded, -1);
			}else if (MATCH(dev_property, "active")) {
				device_active = atoi(value);
				get_iter_by_name(tree, &dev_iter, dev_name, NULL);
				gtk_tree_store_set(store, &dev_iter, DEVICE_ACTIVE, device_active, -1);
			}
			break;
		case CHANNEL:
			elems = g_strsplit(name, ".", CHANNEL + 1);
			dev_name = elems[0];
			ch_name = elems[1];
			ch_property = elems[2];

			if (MATCH(elems[0], "test")) {
				if (MATCH(elems[1], "marker")) {
					min_max = g_strsplit(value, " ", 0);
					min_f = atof(min_max[0]);
					max_f = atof(min_max[1]);
					i = atoi(elems[2]);
					if (priv->markers[i].active &&
						priv->markers[i].y >= min_f &&
						priv->markers[i].y <= max_f) {
						ret = 1;
					} else {
						ret = 0;
						printf("%smarker %i failed : level %f\n",
							priv->markers[i].active ? "" : "In",
							i, priv->markers[i].y);
					}
					g_strfreev(min_max);
				} else {
					goto unhandled;
				}
				break;
			}

			dev = device_find_by_name(dev_name);
			if (dev == -1)
				goto unhandled;
			ch = channel_find_by_name(dev, ch_name);
			if (ch == -1)
				goto unhandled;
			if (MATCH(ch_property, "enabled")) {
				enabled = atoi(value);
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_store_set(store, &ch_iter, CHANNEL_ACTIVE, enabled, -1);
			} else if (MATCH(ch_property, "color_red")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				csettings->graph_color.red = atoi(value);
			} else if (MATCH(ch_property, "color_green")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				csettings->graph_color.green = atoi(value);
			} else if (MATCH(ch_property, "color_blue")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				csettings->graph_color.blue = atoi(value);
			} else if (MATCH(ch_property, "math_apply_inverse_funct")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				csettings->apply_inverse_funct = atoi(value);
			} else if (MATCH(ch_property, "math_apply_multiply_funct")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				csettings->apply_multiply_funct = atoi(value);
			} else if (MATCH(ch_property, "math_apply_add_funct")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				csettings->apply_add_funct = atoi(value);
			} else if (MATCH(ch_property, "math_multiply_value")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				csettings->multiply_value = atof(value);
			} else if (MATCH(ch_property, "math_add_value")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				csettings->add_value = atof(value);
			}
			break;
		default:
unhandled:
			printf("Unhandled tokens in ini file, \n"
				"\tSection %s\n\tAttribute : %s\n\tValue: %s\n",
				section, name, value);
			ret = 0;
			break;
	}

	if (elems != NULL)
		g_strfreev(elems);

	return ret;
}

static void treeview_expand_update(OscPlot *plot)
{
	GtkTreeIter dev_iter;
	GtkTreeView *tree = GTK_TREE_VIEW(plot->priv->channel_list_view);
	GtkTreeModel *model;
	gboolean next_dev_iter;
	gboolean expanded;

	model = gtk_tree_view_get_model(tree);
	next_dev_iter = gtk_tree_model_get_iter_first(model, &dev_iter);
	while (next_dev_iter) {
		gtk_tree_model_get(model, &dev_iter, EXPANDED, &expanded, -1);
		expand_iter(plot, &dev_iter, expanded);
		next_dev_iter = gtk_tree_model_iter_next(model, &dev_iter);
	}
}

static void treeview_icon_color_update(OscPlot *plot)
{
	foreach_device_iter(GTK_TREE_VIEW(plot->priv->channel_list_view),
			*iter_children_icon_color_update, NULL);
}

static inline void marker_set(OscPlot *plot, int i, char *buf, bool force)
{
	OscPlotPrivate *priv = plot->priv;

	if (force)
		priv->markers[i].active = TRUE;

	if (priv->markers[i].graph) {
		gtk_databox_markers_set_label(GTK_DATABOX_MARKERS(priv->markers[i].graph), 0,
			GTK_DATABOX_MARKERS_TEXT_N, buf, FALSE);
		gtk_databox_graph_set_hide(priv->markers[i].graph, !priv->markers[i].active);
	}
}

static void set_marker_labels (OscPlot *plot, gchar *buf, enum marker_types type)
{
	OscPlotPrivate *priv = plot->priv;
	char tmp[128];
	int i;

	if (!MAX_MARKERS)
		return;

	if ((buf && !strcmp(buf, PEAK_MRK)) || type == MARKER_PEAK) {
		priv->marker_type = MARKER_PEAK;
		for (i = 0; i <= MAX_MARKERS; i++) {
			sprintf(tmp, "P%i", i);
			marker_set(plot, i, tmp, FALSE);
		}
		return;
	} else if ((buf && !strcmp(buf, FIX_MRK)) || type == MARKER_FIXED) {
		priv->marker_type = MARKER_FIXED;
		for (i = 0; i <= MAX_MARKERS; i++) {
			sprintf(tmp, "F%i", i);
			marker_set(plot, i, tmp, FALSE);
		}
		return;
	} else if ((buf && !strcmp(buf, SINGLE_MRK)) || type == MARKER_ONE_TONE) {
		priv->marker_type = MARKER_ONE_TONE;
		marker_set(plot, 0, "Fund", TRUE);
		marker_set(plot, 1, "DC", TRUE);
		for (i = 2; i < MAX_MARKERS; i++) {
			sprintf(tmp, "%iH", i);
			marker_set(plot, i, tmp, FALSE);
		}
		return;
	} else if ((buf && !strcmp(buf, DUAL_MRK)) || type == MARKER_TWO_TONE) {
		priv->marker_type = MARKER_TWO_TONE;
		return;
	} else if ((buf && !strcmp(buf, IMAGE_MRK)) || type == MARKER_IMAGE) {
		priv->marker_type = MARKER_IMAGE;
		marker_set(plot, 0, "Fund", TRUE);
		marker_set(plot, 1, "DC", TRUE);
		marker_set(plot, 2, "Image", TRUE);
		for (i = 3; i <= MAX_MARKERS; i++) {
			priv->markers[i].active = FALSE;
			if(priv->markers[i].graph)
				gtk_databox_graph_set_hide(priv->markers[i].graph, TRUE);
		}
		return;
	} else if (buf && !strcmp(buf, OFF_MRK)) {
		priv->marker_type = MARKER_OFF;
		for (i = 0; i <= MAX_MARKERS; i++) {
			if (priv->markers[i].graph)
				gtk_databox_graph_set_hide(priv->markers[i].graph, TRUE);
		}
		return;
	} else if (buf && !strcmp(buf, REMOVE_MRK)) {
		for (i = MAX_MARKERS; i != 0; i--) {
			if (priv->markers[i].active) {
				priv->markers[i].active = FALSE;
				gtk_databox_graph_set_hide(priv->markers[i].graph, TRUE);
				break;
			}
		}
		return;
	} else if (buf && !strcmp(buf, ADD_MRK)) {
		for (i = 0; i <= MAX_MARKERS; i++) {
			if (!priv->markers[i].active) {
				priv->markers[i].active = TRUE;
				gtk_databox_graph_set_hide(priv->markers[i].graph, FALSE);
				break;
			}
		}
		return;
	}

	printf("unhandled event at %s : %s\n", __func__, buf);
}

static void marker_menu (struct string_and_plot *string_data)
{
	set_marker_labels(string_data->plot, string_data->string_obj, MARKER_NULL);
}

static gint moved_fixed(GtkDatabox *box, GdkEventMotion *event, gpointer user_data)
{
	struct int_and_plot *data = user_data;
	OscPlot *plot = data->plot;
	OscPlotPrivate *priv = plot->priv;
	int mark = data->int_obj;
	unsigned int max_size;
	gfloat *X = Transform_get_x_axis_ref(priv->tr_with_marker);

	if (priv->active_transform_type == COMPLEX_FFT_TRANSFORM)
		max_size = priv->tr_with_marker->x_axis_size;
	else
		max_size = priv->tr_with_marker->x_axis_size / 2;

	while((gfloat)X[priv->markers[mark].bin] < gtk_databox_pixel_to_value_x(box, event->x) &&
		priv->markers[mark].bin < max_size)
		priv->markers[mark].bin++;

	while((gfloat)X[priv->markers[mark].bin] > gtk_databox_pixel_to_value_x(box, event->x) &&
		priv->markers[mark].bin > 0)
		priv->markers[mark].bin--;

	return FALSE;
}

static gint marker_button(GtkDatabox *box, GdkEventButton *event, gpointer data)
{
	OscPlot *plot = (OscPlot *)data;
	OscPlotPrivate *priv = plot->priv;
	gfloat x, y, dist;
	GtkWidget *popupmenu, *menuitem;
	gfloat left, right, top, bottom;
	int i, fix = -1;
	bool full = TRUE, empty = TRUE;

	/* FFT? */
	if (priv->active_transform_type != FFT_TRANSFORM &&
		priv->active_transform_type != COMPLEX_FFT_TRANSFORM)
	return FALSE;

	/* Right button */
	if (event->button != 3)
		return FALSE;

	/* things are running? */
	if (!priv->markers[0].graph)
		return FALSE;

	if (event->type == GDK_BUTTON_RELEASE) {
		if (priv->fixed_marker_hid) {
			g_signal_handler_disconnect(box, priv->fixed_marker_hid);
			priv->fixed_marker_hid = 0;
			return TRUE;
		}
		return FALSE;
	}

	x = gtk_databox_pixel_to_value_x(box, event->x);
	y = gtk_databox_pixel_to_value_y(box, event->y);
	gtk_databox_get_total_limits(box, &left, &right, &top, &bottom);

	for (i = 0; i <= MAX_MARKERS; i++) {
		if (priv->marker_type == MARKER_FIXED) {
			/* sqrt of ((delta X / X range)^2 + (delta Y / Y range)^2 ) */
			dist = sqrtf(powf((x - priv->markers[i].x) / (right - left), 2.0) +
					powf((y - priv->markers[i].y) / (bottom - top), 2.0)) * 100;
			if (dist <= 2.0)
				fix = i;
		}
		if (!priv->markers[i].active)
			full = FALSE;
		else if (empty)
			empty = FALSE;
	}

	priv->fix_marker.int_obj = fix;
	priv->fix_marker.plot = plot;
	if (fix != -1) {
		priv->fixed_marker_hid = g_signal_connect(box, "motion_notify_event",
			G_CALLBACK(moved_fixed), (gpointer) &priv->fix_marker);
		return TRUE;
	}

	popupmenu = gtk_menu_new();

	i = 0;
	if (!full && !(priv->marker_type == MARKER_OFF || priv->marker_type == MARKER_IMAGE)) {
		menuitem = gtk_menu_item_new_with_label(ADD_MRK);
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
				GTK_SIGNAL_FUNC(marker_menu), (gpointer) &priv->add_mrk);
		gtk_widget_show(menuitem);
		i++;
	}

	if (!empty && !(priv->marker_type == MARKER_OFF || priv->marker_type == MARKER_IMAGE)) {
		menuitem = gtk_menu_item_new_with_label(REMOVE_MRK);
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
				GTK_SIGNAL_FUNC(marker_menu), (gpointer) &priv->remove_mrk);
		gtk_widget_show(menuitem);
		i++;
	}

	if (!full || !empty) {
		menuitem = gtk_separator_menu_item_new();
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_widget_show(menuitem);
		i++;
	}

	menuitem = gtk_check_menu_item_new_with_label(PEAK_MRK);
	gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
			priv->marker_type == MARKER_PEAK);
	gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
			GTK_SIGNAL_FUNC(marker_menu), (gpointer) &priv->peak_mrk);
	gtk_widget_show(menuitem);
	i++;

	menuitem = gtk_check_menu_item_new_with_label(FIX_MRK);
	gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
			priv->marker_type == MARKER_FIXED);
	gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
			GTK_SIGNAL_FUNC(marker_menu), (gpointer) &priv->fix_mrk);
	gtk_widget_show(menuitem);
	i++;

	menuitem = gtk_check_menu_item_new_with_label(SINGLE_MRK);
	gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
			priv->marker_type == MARKER_ONE_TONE);
	gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
			GTK_SIGNAL_FUNC(marker_menu), (gpointer) &priv->single_mrk);
	gtk_widget_show(menuitem);
	i++;

	/*
	menuitem = gtk_check_menu_item_new_with_label(DUAL_MRK);
	gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
			priv->marker_type == MARKER_TWO_TONE);
	gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
			GTK_SIGNAL_FUNC(marker_menu), (gpointer) &priv->dual_mrk);
	gtk_widget_show(menuitem);
	i++;
	*/

	if (priv->active_transform_type == COMPLEX_FFT_TRANSFORM) {
		menuitem = gtk_check_menu_item_new_with_label(IMAGE_MRK);
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
				priv->marker_type == MARKER_IMAGE);
		gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
		GTK_SIGNAL_FUNC(marker_menu), (gpointer) &priv->image_mrk);
		gtk_widget_show(menuitem);
		i++;
	}

	if (priv->marker_type != MARKER_OFF) {
		menuitem = gtk_check_menu_item_new_with_label(OFF_MRK);
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
				priv->marker_type == MARKER_OFF);
		gtk_signal_connect_object(GTK_OBJECT(menuitem), "activate",
		GTK_SIGNAL_FUNC(marker_menu), (gpointer) &priv->off_mrk);
		gtk_widget_show(menuitem);
		i++;
	}

	gtk_menu_popup(GTK_MENU(popupmenu), NULL, NULL, NULL, NULL,
		event->button, event->time);

	if (priv->marker_type == MARKER_FIXED)
		return TRUE;

	return FALSE;
}

static void enable_tree_device_selection(OscPlot *plot, gboolean enable)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean next_iter;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->channel_list_view));
	if (!gtk_tree_model_get_iter_first(model, &iter))
		return;

	next_iter = true;
	while (next_iter) {
		gtk_tree_store_set(GTK_TREE_STORE(model), &iter, DEVICE_SELECTABLE, enable, -1);
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}
}

static void plot_domain_changed_cb(GtkComboBox *box, OscPlot *plot)
{
	gboolean force_sensitive = true;

	check_valid_setup(plot);

	foreach_device_iter(GTK_TREE_VIEW(plot->priv->channel_list_view),
			*iter_children_plot_type_update, plot);

	if (num_devices < 2)
		return;

	if (gtk_combo_box_get_active(box) != TIME_PLOT) {
		enable_tree_device_selection(plot, true);
		foreach_device_iter(GTK_TREE_VIEW(plot->priv->channel_list_view),
			*iter_children_sensitivity_update, NULL);
	 } else {
		enable_tree_device_selection(plot, false);
		foreach_device_iter(GTK_TREE_VIEW(plot->priv->channel_list_view),
			*iter_children_sensitivity_update, &force_sensitive);
	}
}

static gboolean domain_is_fft(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer user_data)
{
	g_value_set_boolean(target_value, g_value_get_int(source_value) == FFT_PLOT);
	return TRUE;
}

static gboolean domain_is_time(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer user_data)
{
	g_value_set_boolean(target_value, g_value_get_int(source_value) != FFT_PLOT);
	return TRUE;
}


static void fft_avg_value_changed_cb(GtkSpinButton *button, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	int i;

	for (i = 0; i < priv->transform_list->size; i++) {
		FFT_SETTINGS(priv->transform_list->transforms[i])->fft_avg = gtk_spin_button_get_value(button);
	}
}
static void fft_pwr_offset_value_changed_cb(GtkSpinButton *button, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	int i;

	for (i = 0; i < priv->transform_list->size; i++) {
		FFT_SETTINGS(priv->transform_list->transforms[i])->fft_pwr_off = gtk_spin_button_get_value(button);
	}
}

static gboolean tree_get_selected_row_iter(GtkTreeView *treeview, GtkTreeIter *iter)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GList *row = NULL;

	selection = gtk_tree_view_get_selection(treeview);
	model = gtk_tree_view_get_model(treeview);
	row = gtk_tree_selection_get_selected_rows(selection, &model);
	if (!row)
		return false;
	gtk_tree_model_get_iter(model, iter, row->data);

	return true;
}

static void channel_color_settings_cb(GtkMenuItem *menuitem, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	struct channel_settings *settings;
	GtkWidget *color_dialog;
	GtkWidget *colorsel;
	GdkColor *color;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GdkPixbuf *color_icon;
	gboolean selected;
	gint response;

	color_dialog = gtk_color_selection_dialog_new("Channel Graph Color Selection");
	response = gtk_dialog_run(GTK_DIALOG(color_dialog));
	gtk_widget_hide(color_dialog);
	if (response != GTK_RESPONSE_OK)
		return;

	treeview = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(treeview);
	selected = tree_get_selected_row_iter(treeview, &iter);
	if (!selected)
		return;
	gtk_tree_model_get(model, &iter, CHANNEL_SETTINGS, &settings,
			CHANNEL_COLOR_ICON, &color_icon, -1);
	color = &settings->graph_color;

	colorsel = gtk_color_selection_dialog_get_color_selection(GTK_COLOR_SELECTION_DIALOG(color_dialog));
	gtk_color_selection_get_current_color(GTK_COLOR_SELECTION(colorsel), color);

	/* Change icon color */
	channel_color_icon_set_color(color_icon, color);

}
static void channel_math_settings_cb(GtkMenuItem *menuitem, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkBuilder *builder = priv->builder;
	GtkWidget *widget;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean selected;
	int response;
	struct channel_settings *csettings;

	treeview = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(treeview);
	selected = tree_get_selected_row_iter(treeview, &iter);
	if (!selected)
		return;
	gtk_tree_model_get(model, &iter, CHANNEL_SETTINGS, &csettings, -1);

	widget = GTK_WIDGET(gtk_builder_get_object(builder, "btn_inverse_fct"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), csettings->apply_inverse_funct);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "btn_multiply"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), csettings->apply_multiply_funct);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "btn_add"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), csettings->apply_add_funct);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "spinbutton_multiply_value"));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), csettings->multiply_value);
	widget = GTK_WIDGET(gtk_builder_get_object(builder, "spinbutton_add_to_value"));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), csettings->add_value);

	response = gtk_dialog_run(GTK_DIALOG(priv->math_dialog));
	if (response == GTK_RESPONSE_OK) {
		widget = GTK_WIDGET(gtk_builder_get_object(builder, "btn_inverse_fct"));
		csettings->apply_inverse_funct = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		widget = GTK_WIDGET(gtk_builder_get_object(builder, "btn_multiply"));
		csettings->apply_multiply_funct = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		widget = GTK_WIDGET(gtk_builder_get_object(builder, "btn_add"));
		csettings->apply_add_funct = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		widget = GTK_WIDGET(gtk_builder_get_object(builder, "spinbutton_multiply_value"));
		csettings->multiply_value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
		widget = GTK_WIDGET(gtk_builder_get_object(builder, "spinbutton_add_to_value"));
		csettings->add_value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
	}
	gtk_widget_hide(priv->math_dialog);
}

static void right_click_menu_show(OscPlot *plot, GdkEventButton *event)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean is_channel = false;
	gboolean selected;

	treeview = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(treeview);
	selected = tree_get_selected_row_iter(treeview, &iter);
	if (!selected)
		return;
	gtk_tree_model_get(model, &iter, IS_CHANNEL, &is_channel, -1);

	if (is_channel) {
		gtk_menu_popup(GTK_MENU(priv->channel_settings_menu), NULL, NULL,
			NULL, NULL,
			(event != NULL) ? event->button : 0,
			gdk_event_get_time((GdkEvent*)event));
	}
}

static gboolean right_click_on_ch_list_cb(GtkTreeView *treeview, GdkEventButton *event, OscPlot *plot)
{
	/* single click with the right mouse button */
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot->priv->plot_domain)) != TIME_PLOT)
			return false;
		right_click_menu_show(plot, event);
		return true;
	}

	return false;
}

static void create_plot(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	GtkWidget *table;
	GtkWidget *tmp;
	GtkBuilder *builder = NULL;
	GtkTreeSelection *tree_selection;
	GtkDataboxRuler *ruler_y;
	GtkTreeStore *tree_store;
	char buf[50];
	int i;

	/* Get the GUI from a glade file. */
	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file(builder, "./oscplot.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "oscplot.glade", NULL);
	else {
		GtkImage *logo;
		/* We are running locally, so load the local files */
		logo = GTK_IMAGE(gtk_builder_get_object(builder, "ADI_logo"));
		g_object_set(logo, "file","./icons/ADIlogo.png", NULL);
	}

	priv->builder = builder;
	priv->window = GTK_WIDGET(gtk_builder_get_object(builder, "toplevel"));
	priv->capture_graph = GTK_WIDGET(gtk_builder_get_object(builder, "display_capture"));
	priv->capture_button = GTK_WIDGET(gtk_builder_get_object(builder, "capture_button"));
	priv->channel_list_view = GTK_WIDGET(gtk_builder_get_object(builder, "channel_list_view"));
	priv->show_grid = GTK_WIDGET(gtk_builder_get_object(builder, "show_grid"));
	priv->plot_domain = GTK_WIDGET(gtk_builder_get_object(builder, "capture_domain"));
	priv->plot_type = GTK_WIDGET(gtk_builder_get_object(builder, "plot_type"));
	priv->enable_auto_scale = GTK_WIDGET(gtk_builder_get_object(builder, "auto_scale"));
	priv->hor_scale = GTK_WIDGET(gtk_builder_get_object(builder, "hor_scale"));
	priv->marker_label = GTK_WIDGET(gtk_builder_get_object(builder, "marker_info"));
	priv->saveas_button = GTK_WIDGET(gtk_builder_get_object(builder, "save_as"));
	priv->saveas_dialog = GTK_WIDGET(gtk_builder_get_object(builder, "saveas_dialog"));
	priv->saveas_type_dialog = GTK_WIDGET(gtk_builder_get_object(builder, "saveas_type_dialog"));
	priv->fullscreen_button = GTK_WIDGET(gtk_builder_get_object(builder, "fullscreen_toggle"));
	priv->y_axis_max = GTK_WIDGET(gtk_builder_get_object(builder, "spin_Y_max"));
	priv->y_axis_min = GTK_WIDGET(gtk_builder_get_object(builder, "spin_Y_min"));
	priv->viewport_saveas_channels = GTK_WIDGET(gtk_builder_get_object(builder, "saveas_channels_container"));
	priv->saveas_select_channel_message = GTK_WIDGET(gtk_builder_get_object(builder, "hbox_ch_sel_label"));
	priv->sample_count_widget = GTK_WIDGET(gtk_builder_get_object(builder, "sample_count"));
	priv->fft_size_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size"));
	priv->fft_avg_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_avg"));
	priv->fft_pwr_offset_widget = GTK_WIDGET(gtk_builder_get_object(builder, "pwr_offset"));
	priv->math_dialog = GTK_WIDGET(gtk_builder_get_object(builder, "dialog_math_settings"));

	priv->tbuf = NULL;
	priv->ch_settings_list = NULL;

	/* Count every object that is being created */
	object_count++;
	priv->object_id = object_count;

	/* Set a different title for every plot */
	snprintf(buf, sizeof(buf), "ADI IIO multi plot oscilloscope - Capture%d", priv->object_id);
	gtk_window_set_title(GTK_WINDOW(priv->window), buf);

	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_domain), TIME_PLOT);
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_type), 0);

	/* Create a GtkDatabox widget along with scrollbars and rulers */
	gtk_databox_create_box_with_scrollbars_and_rulers(&priv->databox, &table,
		TRUE, TRUE, TRUE, TRUE);
	gtk_box_pack_start(GTK_BOX(priv->capture_graph), table, TRUE, TRUE, 0);
	gtk_widget_modify_bg(priv->databox, GTK_STATE_NORMAL, &color_background);
	gtk_widget_set_size_request(table, 320, 240);
	ruler_y = gtk_databox_get_ruler_y(GTK_DATABOX(priv->databox));

	/* Create a Tree Store that holds information about devices */
	tree_store = gtk_tree_store_new(NUM_COL,
									G_TYPE_STRING,    /* ELEMENT_NAME */
									G_TYPE_BOOLEAN,   /* IS_DEVICE */
									G_TYPE_BOOLEAN,   /* IS_CHANNEL */
									G_TYPE_BOOLEAN,   /* DEVICE_SELECTABLE */
									G_TYPE_BOOLEAN,   /* DEVICE_ACTIVE */
									G_TYPE_BOOLEAN,   /* CHANNEL_ACTIVE */
									G_TYPE_POINTER,   /* ELEMENT_REFERENCE */
									G_TYPE_BOOLEAN,   /* EXPANDED */
									G_TYPE_POINTER,   /* CHANNEL_SETTINGS */
									GDK_TYPE_PIXBUF,  /* CHANNEL_COLOR_ICON */
									G_TYPE_CHAR,      /* PLOT_TYPE */
									G_TYPE_BOOLEAN);  /* SENSITIVE */
	gtk_tree_view_set_model((GtkTreeView *)priv->channel_list_view, (GtkTreeModel *)tree_store);

	/* Create menus */
	priv->channel_settings_menu = gtk_menu_new();
	priv->channel_color_menuitem = gtk_image_menu_item_new_with_label("Color Selection");
	GtkWidget *image;

	image = gtk_image_new_from_stock(GTK_STOCK_SELECT_COLOR, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(priv->channel_color_menuitem), image);
	gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(priv->channel_color_menuitem), true);
	gtk_menu_shell_append(GTK_MENU_SHELL(priv->channel_settings_menu),
		priv->channel_color_menuitem);
	priv->channel_math_menuitem = gtk_image_menu_item_new_with_label("Math Settings");
	image = gtk_image_new_from_stock(GTK_STOCK_PREFERENCES, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(priv->channel_math_menuitem), image);
	gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(priv->channel_math_menuitem), true);
	gtk_menu_shell_append(GTK_MENU_SHELL(priv->channel_settings_menu),
		priv->channel_math_menuitem);
	gtk_widget_show_all(priv->channel_settings_menu);

	/* Create application's treeviews */
	device_list_treeview_init(plot);
	saveas_channels_list_fill(plot);

	/* Connect Signals */
	g_signal_connect(G_OBJECT(priv->window), "destroy", G_CALLBACK(plot_destroyed), plot);

	priv->capture_button_hid =
	g_signal_connect(priv->capture_button, "toggled",
		G_CALLBACK(capture_button_clicked_cb), plot);
	g_signal_connect(priv->plot_domain, "changed",
		G_CALLBACK(plot_domain_changed_cb), plot);
	g_signal_connect(priv->saveas_button, "clicked",
		G_CALLBACK(cb_saveas_chooser), plot);
	g_signal_connect(priv->saveas_type_dialog, "response",
		G_CALLBACK(cb_saveas_chooser_response), plot);
	g_signal_connect(priv->saveas_dialog, "response",
		G_CALLBACK(cb_saveas_response), plot);
	g_signal_connect(priv->saveas_dialog, "delete-event",
		G_CALLBACK(gtk_widget_hide_on_delete), plot);
	g_signal_connect(priv->saveas_type_dialog, "delete-event",
		G_CALLBACK(gtk_widget_hide_on_delete), plot);
	g_signal_connect(priv->fullscreen_button, "toggled",
		G_CALLBACK(fullscreen_button_clicked_cb), plot);
	g_signal_connect(priv->enable_auto_scale, "toggled",
		G_CALLBACK(enable_auto_scale_cb), plot);
	g_signal_connect(priv->y_axis_max, "value-changed",
		G_CALLBACK(max_y_axis_cb), plot);
	g_signal_connect(priv->y_axis_min, "value-changed",
		G_CALLBACK(min_y_axis_cb), plot);
	g_signal_connect(priv->fft_avg_widget, "value-changed",
		G_CALLBACK(fft_avg_value_changed_cb), plot);
	g_signal_connect(priv->fft_pwr_offset_widget, "value-changed",
		G_CALLBACK(fft_pwr_offset_value_changed_cb), plot);

	g_signal_connect(priv->channel_list_view, "button-press-event",
		G_CALLBACK(right_click_on_ch_list_cb), plot);
	g_signal_connect(priv->channel_color_menuitem, "activate",
		G_CALLBACK(channel_color_settings_cb), plot);
	g_signal_connect(priv->channel_math_menuitem, "activate",
		G_CALLBACK(channel_math_settings_cb), plot);

	g_builder_connect_signal(builder, "zoom_in", "clicked",
		G_CALLBACK(zoom_in), plot);
	g_builder_connect_signal(builder, "zoom_out", "clicked",
		G_CALLBACK(zoom_out), plot);
	g_builder_connect_signal(builder, "zoom_fit", "clicked",
		G_CALLBACK(zoom_fit), plot);
	g_signal_connect(priv->show_grid, "toggled",
		G_CALLBACK(show_grid_toggled), plot);

	g_signal_connect(GTK_DATABOX(priv->databox), "button_press_event",
		G_CALLBACK(marker_button), plot);
	g_signal_connect(GTK_DATABOX(priv->databox), "button_release_event",
		G_CALLBACK(marker_button), plot);

	/* Create Bindings */
	g_object_bind_property_full(priv->capture_button, "active", priv->capture_button,
		"stock-id", 0, capture_button_icon_transform, NULL, plot, NULL);
	g_object_bind_property_full(priv->fullscreen_button, "active", priv->fullscreen_button,
		"stock-id", 0, fullscreen_button_icon_transform, NULL, NULL, NULL);
	g_object_bind_property(priv->y_axis_max, "value", ruler_y, "lower", G_BINDING_DEFAULT);
	g_object_bind_property(priv->y_axis_min, "value", ruler_y, "upper", G_BINDING_DEFAULT);

	g_builder_bind_property(builder, "capture_button", "active",
		"channel_list_view", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
		"capture_domain", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
		"fft_size", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
		"plot_type", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
		"sample_count", "sensitive", G_BINDING_INVERT_BOOLEAN);

	/* Bind the plot domain to the sensitivity of the sample count and
	 * FFT size widgets */
	 tmp = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size_label"));
	 g_object_bind_property_full(priv->plot_domain, "active", tmp, "visible",
		0, domain_is_fft, NULL, plot, NULL);
	 g_object_bind_property_full(priv->plot_domain, "active", priv->fft_size_widget, "visible",
		0, domain_is_fft, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "fft_avg_label"));
	 g_object_bind_property_full(priv->plot_domain, "active", tmp, "visible",
		0, domain_is_fft, NULL, NULL, NULL);
	 g_object_bind_property_full(priv->plot_domain, "active", priv->fft_avg_widget, "visible",
		0, domain_is_fft, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "pwr_offset_label"));
	 g_object_bind_property_full(priv->plot_domain, "active", tmp, "visible",
		0, domain_is_fft, NULL, NULL, NULL);
	 g_object_bind_property_full(priv->plot_domain, "active", priv->fft_pwr_offset_widget, "visible",
		0, domain_is_fft, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "sample_count_label"));
	 g_object_bind_property_full(priv->plot_domain, "active", tmp, "visible",
		0, domain_is_time, NULL, NULL, NULL);
	 g_object_bind_property_full(priv->plot_domain, "active", priv->sample_count_widget, "visible",
		0, domain_is_time, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "plot_type_label"));
	 g_object_bind_property_full(priv->plot_domain, "active", tmp, "visible",
		0, domain_is_time, NULL, NULL, NULL);
	 g_object_bind_property_full(priv->plot_domain, "active", priv->plot_type, "visible",
		0, domain_is_time, NULL, NULL, NULL);

	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->fft_size_widget), 2);
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_type), 0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->y_axis_max), 1000);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->y_axis_min), -1000);
	tree_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->channel_list_view));
	gtk_tree_selection_set_mode(tree_selection, GTK_SELECTION_SINGLE);
	add_grid(plot);
	check_valid_setup(plot);
	g_mutex_init(&priv->g_marker_copy_lock);

	if (MAX_MARKERS) {
		priv->marker_type = MARKER_OFF;
		for (i = 0; i < MAX_MARKERS; i++) {
			priv->markers[i].graph = NULL;
			priv->markers[i].active = (i <= 4);
		}
	}
	priv->add_mrk.plot = plot;
	priv->remove_mrk.plot = plot;
	priv->peak_mrk.plot = plot;
	priv->fix_mrk.plot = plot;
	priv->single_mrk.plot = plot;
	priv->dual_mrk.plot = plot;
	priv->image_mrk.plot = plot;
	priv->off_mrk.plot = plot;
	priv->add_mrk.string_obj = ADD_MRK;
	priv->remove_mrk.string_obj = REMOVE_MRK;
	priv->peak_mrk.string_obj = PEAK_MRK;
	priv->fix_mrk.string_obj = FIX_MRK;
	priv->single_mrk.string_obj = SINGLE_MRK;
	priv->dual_mrk.string_obj = DUAL_MRK;
	priv->image_mrk.string_obj = IMAGE_MRK;
	priv->off_mrk.string_obj = OFF_MRK;

	gtk_window_set_modal(GTK_WINDOW(priv->saveas_dialog), FALSE);
	gtk_widget_show(priv->window);
	gtk_widget_show_all(priv->capture_graph);
}
