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

#include "osc.h"
#include "oscplot.h"
#include "config.h"
#include "iio_widget.h"
#include "datatypes.h"

extern void time_transform_function(Transform *tr, gboolean init_transform);
extern void fft_transform_function(Transform *tr, gboolean init_transform);
extern void constellation_transform_function(Transform *tr, gboolean init_transform);

extern struct _device_list *device_list;
extern unsigned num_devices;

static void create_plot (OscPlot *plot);
static void plot_setup(OscPlot *plot);
static void capture_button_clicked_cb (GtkToggleToolButton *btn, gpointer data);
static void add_grid(OscPlot *plot);
static void rescale_databox(OscPlotPrivate *priv, GtkDatabox *box, gfloat border);
static void call_all_transform_functions(OscPlotPrivate *priv);
static void capture_start(OscPlotPrivate *priv);

/* IDs of signals */
enum {
	CAPTURE_EVENT_SIGNAL,
	DESTROY_EVENT_SIGNAL,
	LAST_SIGNAL
};

/* signals will be configured during class init */
static guint oscplot_signals[LAST_SIGNAL] = { 0 };

/* Types of transforms */
enum {
	NO_TRANSFORM_TYPE,
	TIME_TRANSFORM,
	FFT_TRANSFORM,
	CONSTELLATION_TRANSFORM
};

/* Columns of the device treestore */
enum {
	ELEMENT_NAME,
	IS_DEVICE,
	IS_CHANNEL,
	IS_TRANSFORM,
	CHANNEL_ACTIVE,
	TRANSFORM_ACTIVE,
	MARKER_ENABLED,
	ELEMENT_REFERENCE,
	COLOR_REF,
	NUM_COL
};

static GdkColor color_graph[] = {
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
	.blue = 0,
};

/* Helpers */
#define GET_CHANNEL_PARENT(ch) ((struct extra_info *)(((struct iio_channel_info *)(ch))->extra_field))->device_parent

struct _OscPlotPrivate
{
	GtkBuilder *builder;
	
	/* Graphical User Interface */
	GtkWidget *window;
	GtkWidget *time_settings_diag;
	GtkWidget *fft_settings_diag;
	GtkWidget *constellation_settings_diag;
	GtkWidget *databox;
	GtkWidget *capture_graph;
	GtkWidget *capture_button;
	GtkWidget *channel_list_view;
	GtkWidget *show_grid;
	GtkWidget *plot_type;
	GtkWidget *enable_auto_scale;
	GtkWidget *hor_scale;
	GtkWidget *marker_label;
	GtkWidget *saveas_button;
	GtkWidget *saveas_dialog;
	GtkWidget *fullscreen_button;
	GtkWidget *y_axis_max;
	GtkWidget *y_axis_min;
	
	GtkTextBuffer* tbuf;
	
	int frame_counter;
	time_t last_update;
	
	int do_a_rescale_flag;
	
	/* A reference to the device holding the most recent created transform */
	struct _device_list *current_device;
	
	/* List of transforms for this plot */
	TrList *transform_list;
	
	/* Active transform type for this window */
	int active_transform_type;
	Transform *selected_transform_for_setup;
	
	/* Databox data */
	GtkDataboxGraph *grid;
	GtkDataboxGraph *time_graph;
	gfloat gridy[25], gridx[25];
	
	gint redraw_function;
	gint stop_redraw;
	
	GList *selected_rows_paths;
	gint num_selected_rows;
	
	GList *available_graph_colors;
};

G_DEFINE_TYPE(OscPlot, osc_plot, GTK_TYPE_WIDGET)

static void osc_plot_class_init(OscPlotClass *klass)
{
	GObjectClass *gobject_class  = G_OBJECT_CLASS (klass);

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

void osc_plot_data_update (OscPlot *plot)
{	
	call_all_transform_functions(plot->priv);
}

void osc_plot_update_rx_lbl(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	TrList *tr_list = priv->transform_list;
	int i;
	
	if (priv->active_transform_type == FFT_TRANSFORM) {
		gtk_label_set_text(GTK_LABEL(priv->hor_scale), priv->current_device->adc_scale);
		/* In FFT mode we need to scale the x-axis according to the selected sampling freequency */
		for (i = 0; i < tr_list->size; i++)
			Transform_setup(tr_list->transforms[i]);
		gtk_databox_set_total_limits(GTK_DATABOX(priv->databox), 0.0, priv->current_device->adc_freq / 2.0, 0.0, -75.0);
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

static void add_row_child(OscPlot *plot, GtkTreeIter *parent, char *child_name, Transform *tr)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview;
	GtkTreeStore *treestore;
	GtkTreeIter child;
	GdkColor *transform_color;
	GList *first_element;
	int i;

	treeview = (GtkTreeView *)priv->channel_list_view;
	treestore = GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

get_color:
	if (g_list_length(priv->available_graph_colors)) {
		first_element = g_list_first(priv->available_graph_colors);
		transform_color = first_element->data;
		priv->available_graph_colors = g_list_delete_link(priv->available_graph_colors, first_element);
	} else {
		 /* Fill the list again */
		for (i = sizeof(color_graph) / sizeof(color_graph[0]) - 1; i >= 0; i--)
		priv->available_graph_colors = g_list_prepend(priv->available_graph_colors, &color_graph[i]);
		goto get_color;
	}
	tr->graph_color = transform_color;
	gtk_tree_store_append(treestore, &child, parent);
	gtk_tree_store_set(treestore, &child, ELEMENT_NAME, child_name,
		IS_DEVICE, FALSE, IS_CHANNEL, FALSE, IS_TRANSFORM, TRUE,
		TRANSFORM_ACTIVE, 1, ELEMENT_REFERENCE, tr, COLOR_REF, transform_color, -1);
}

static void time_settings_init(struct _time_settings *settings)
{
	if (settings) {
		settings->num_samples = 400;
		settings->apply_inverse_funct = false;
		settings->apply_multiply_funct = false;
		settings->apply_add_funct = false;
		settings->multiply_value = 0.0;
		settings->add_value = 0.0;
	}
}

static void fft_settings_init(struct _fft_settings *settings)
{
	int i;
	
	if (settings) {
		settings->fft_size = 256;
		settings->fft_avg = 1;
		settings->fft_pwr_off = 0.0;
		settings->fft_alg_data.cached_fft_size = -1;
		for (i = 0; i < MAX_MARKERS + 2; i++)
			settings->marker[i] = NULL;
	}
}

static void constellation_settings_init(struct _constellation_settings *settings)
{
	if (settings) {
		settings->num_samples = 400;
	}
}

static Transform* add_transform_to_list(OscPlot *plot, struct _device_list *ch_parent, 
	struct iio_channel_info *ch0, struct iio_channel_info *ch1, char *tr_name)
{
	OscPlotPrivate *priv = plot->priv;
	TrList *list = priv->transform_list;
	Transform *transform;
	struct _time_settings *time_settings;
	struct _fft_settings *fft_settings;
	struct _constellation_settings *constellation_settings;
	struct extra_info *ch_info;
	
	transform = Transform_new();
	ch_info = ch0->extra_field;
	transform->channel_parent = ch0;
	transform->graph_active = TRUE;
	ch_info->shadow_of_enabled++;
	priv->current_device = GET_CHANNEL_PARENT(ch0);
	Transform_set_in_data_ref(transform, (gfloat **)&ch_info->data_ref, &ch_parent->sample_count);
	if (!strcmp(tr_name, "TIME")) {
		Transform_attach_function(transform, time_transform_function);
		time_settings = (struct _time_settings *)malloc(sizeof(struct _time_settings));
		time_settings_init(time_settings);
		Transform_attach_settings(transform, time_settings);
		priv->active_transform_type = TIME_TRANSFORM;
	} else if (!strcmp(tr_name, "FFT")) {
		Transform_attach_function(transform, fft_transform_function);
		fft_settings = (struct _fft_settings *)malloc(sizeof(struct _fft_settings));
		fft_settings_init(fft_settings);
		Transform_attach_settings(transform, fft_settings);
		priv->active_transform_type = FFT_TRANSFORM;
	} else if (!strcmp(tr_name, "CONSTELLATION")) {
		transform->channel_parent2 = ch1;
		Transform_attach_function(transform, constellation_transform_function);
		constellation_settings = (struct _constellation_settings *)malloc(sizeof(struct _constellation_settings));
		constellation_settings_init(constellation_settings);
		Transform_attach_settings(transform, constellation_settings);
		ch_info = ch1->extra_field;
		ch_info->shadow_of_enabled++;
		priv->active_transform_type = CONSTELLATION_TRANSFORM;
	}
	TrList_add_transform(list, transform);
	
	return transform;
}

static void remove_transform_from_list(OscPlot *plot, Transform *tr)
{
	OscPlotPrivate *priv = plot->priv;
	TrList *list = priv->transform_list;
	struct extra_info *ch_info = tr->channel_parent->extra_field;

	ch_info->shadow_of_enabled--;
	if (priv->active_transform_type == CONSTELLATION_TRANSFORM) {
		ch_info = tr->channel_parent2->extra_field;
		ch_info->shadow_of_enabled--;
	}
		
	TrList_remove_transform(list, tr);
	Transform_destroy(tr);
	if (list->size == 0) {
		priv->active_transform_type = NO_TRANSFORM_TYPE;
		priv->current_device = NULL;
	}
}

static void add_transform_to_tree_store(GtkMenuItem* menuitem, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeIter parent_iter;
	Transform *tr;
	char *ch_name;
	char *tr_name;
	char buf[50];
	struct iio_channel_info *channel0 = NULL, *channel1 = NULL;
	struct _device_list *ch_parent0, *ch_parent1;
	GList *path;
	
	tree_view = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(tree_view);
	/* Get the selected channel name */
	path = g_list_first(priv->selected_rows_paths);
	gtk_tree_model_get_iter(model, &iter, path->data);
	gtk_tree_model_get(model, &iter, ELEMENT_NAME, &ch_name, ELEMENT_REFERENCE, &channel0, -1);	
	/* Get the transform name */
	tr_name = (char *)gtk_menu_item_get_label(menuitem);
	snprintf(buf, sizeof(buf), "%s", tr_name);
	/* Get the parent reference of the channel0. */
	gtk_tree_model_iter_parent(model, &parent_iter, &iter);
	gtk_tree_model_get(model, &parent_iter, ELEMENT_REFERENCE, &ch_parent0, -1);	
	
	/* Get the second channel if two channel were selected. */
	if (priv->num_selected_rows == 2) {
		snprintf(buf, sizeof(buf), "%s with %s", tr_name, ch_name);
		g_free(ch_name);
		path = g_list_next(priv->selected_rows_paths);
		gtk_tree_model_get_iter(model, &iter, path->data);
		gtk_tree_model_get(model, &iter, ELEMENT_REFERENCE, &channel1, -1);
		/* Get the parent reference of the channel1. */
		gtk_tree_model_iter_parent(model, &parent_iter, &iter);
		gtk_tree_model_get(model, &parent_iter, ELEMENT_REFERENCE, &ch_parent1, -1);
		/* Don't add a constellation for channels belonging to different devices */
		if (ch_parent0 != ch_parent1)
			return;
	} else {
		g_free(ch_name);
	}
	
	/* Add a new transform to a list of transforms. */
	tr = add_transform_to_list(plot, ch_parent0, channel0, channel1, tr_name);
	/* Add the transfrom in the treeview */
	add_row_child(plot, &iter, buf, tr);
	g_object_set(G_OBJECT(tree_view), "sensitive", TRUE, NULL);
}

static void remove_transform_from_tree_store(GtkMenuItem* menuitem, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv  = plot->priv;
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreeIter iter;
	Transform *tr;
	GList *path;	
	
	tree_view = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(tree_view);
	path = g_list_first(priv->selected_rows_paths);
	gtk_tree_model_get_iter(model, &iter, path->data);
	gtk_tree_model_get(model, &iter, ELEMENT_REFERENCE, &tr, -1);
	gtk_tree_store_remove(GTK_TREE_STORE(model), &iter);
	priv->available_graph_colors = g_list_prepend(priv->available_graph_colors, tr->graph_color);
	if (tr->graph) {
		gtk_databox_graph_remove(GTK_DATABOX(priv->databox), tr->graph);
		gtk_widget_queue_draw(GTK_WIDGET(priv->databox));
	}
	remove_transform_from_list(plot, tr);
}

void set_time_settings_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	OscPlot *plot = user_data;
	OscPlotPrivate *priv = plot->priv;
	Transform *tr = priv->selected_transform_for_setup;
	struct _time_settings *time_settings = tr->settings;
	GtkBuilder *builder = priv->builder;
	GtkWidget *widget;
	
	if (response_id == GTK_RESPONSE_OK) {
		widget = GTK_WIDGET(gtk_builder_get_object(builder, "time_sample_count"));
		time_settings->num_samples = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
		widget = GTK_WIDGET(gtk_builder_get_object(builder, "time_multiply_value"));
		time_settings->multiply_value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
		widget = GTK_WIDGET(gtk_builder_get_object(builder, "time_add_value"));
		time_settings->add_value = gtk_spin_button_get_value(GTK_SPIN_BUTTON(widget));
		widget = GTK_WIDGET(gtk_builder_get_object(builder, "checkbtn_inverse_fct"));
		time_settings->apply_inverse_funct = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		widget = GTK_WIDGET(gtk_builder_get_object(builder, "checkbtn_multiply"));
		time_settings->apply_multiply_funct = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
		widget = GTK_WIDGET(gtk_builder_get_object(builder, "checkbtn_add_to"));
		time_settings->apply_add_funct = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	}
	g_object_set(G_OBJECT(priv->channel_list_view), "sensitive", TRUE, NULL);
}

void set_fft_settings_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	OscPlot *plot = user_data;
	OscPlotPrivate *priv = plot->priv;
	Transform *tr = priv->selected_transform_for_setup;
	struct _fft_settings *fft_settings = tr->settings;
	GtkBuilder *builder = priv->builder;
	
	GtkWidget *fft_size_widget;
	GtkWidget *fft_avg_widget;
	GtkWidget *fft_pwr_offset_widget;
	
	fft_size_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size"));
	fft_avg_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_avg"));
	fft_pwr_offset_widget = GTK_WIDGET(gtk_builder_get_object(builder, "pwr_offset"));
	
	if (response_id == GTK_RESPONSE_OK) {
		fft_settings->fft_size = atoi(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(fft_size_widget)));
		fft_settings->fft_avg = gtk_spin_button_get_value(GTK_SPIN_BUTTON(fft_avg_widget));
		fft_settings->fft_pwr_off = gtk_spin_button_get_value(GTK_SPIN_BUTTON(fft_pwr_offset_widget));
	}
	g_object_set(G_OBJECT(priv->channel_list_view), "sensitive", TRUE, NULL);
}

void set_constellation_settings_cb (GtkDialog *dialog, gint response_id, gpointer user_data)
{
	OscPlot *plot = user_data;
	OscPlotPrivate *priv = plot->priv;
	Transform *tr = priv->selected_transform_for_setup;
	struct _constellation_settings *constellation_settings = tr->settings;
	GtkBuilder *builder = priv->builder;
	GtkWidget *constellation_sample_count_widget;
	
	constellation_sample_count_widget = GTK_WIDGET(gtk_builder_get_object(builder, "constellation_sample_count"));
	if (response_id == GTK_RESPONSE_OK) {
		constellation_settings->num_samples = gtk_spin_button_get_value(GTK_SPIN_BUTTON(constellation_sample_count_widget));
	}
	g_object_set(G_OBJECT(priv->channel_list_view), "sensitive", TRUE, NULL);
}

static void rebuild_fft_size_list(GtkWidget *fft_size_widget, unsigned int sample_count, unsigned int set_value)
{
	unsigned int min_fft_size = 32;
	unsigned int max_fft_size = 16384;
	unsigned int fft_size;
	GtkTreeIter iter;
	GtkTreeIter set_iter;
	GtkListStore *fft_size_list;
	char buf[10];
	
	fft_size_list = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(fft_size_widget)));
	gtk_list_store_clear(fft_size_list);
	if (sample_count < min_fft_size) {
		gtk_list_store_append(fft_size_list, &iter);
		gtk_list_store_set(fft_size_list, &iter, 0, "Sample count too small", -1);
		gtk_combo_box_set_active_iter(GTK_COMBO_BOX(fft_size_widget), &iter);
		return;
	}
	fft_size = min_fft_size;
	set_iter = iter;
	while ((fft_size <= sample_count) && (fft_size <= max_fft_size)) {
		gtk_list_store_prepend(fft_size_list, &iter);
		snprintf(buf, sizeof(buf), "%d", fft_size);
		gtk_list_store_set(fft_size_list, &iter, 0, buf, -1);
		/* Save the iter that holds a value equal to the set_value */
		if (fft_size == set_value)
			set_iter = iter;
		fft_size *= 2;
	}
	gtk_combo_box_set_active_iter(GTK_COMBO_BOX(fft_size_widget), &set_iter);
}

static void default_time_setting(OscPlot *plot, Transform *tr)
{
	OscPlotPrivate *priv = plot->priv;
	struct extra_info *ch_info;
	struct _time_settings *settings;
	GtkAdjustment *adj;
	GtkToggleButton *check_btn;
	GtkBuilder *builder = priv->builder;
	
	ch_info = tr->channel_parent->extra_field;
	settings = (struct _time_settings *)tr->settings;
	adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_time_sample_count"));
	gtk_adjustment_set_upper(adj, (gdouble)ch_info->device_parent->shadow_of_sample_count);
	gtk_adjustment_set_value(adj, (gdouble)settings->num_samples);
	adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_time_multiply_sample"));
	gtk_adjustment_set_value(adj, settings->multiply_value);
	adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_time_add_to_sample"));
	gtk_adjustment_set_value(adj, settings->add_value);
	check_btn = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "checkbtn_inverse_fct"));
	gtk_toggle_button_set_active(check_btn, settings->apply_inverse_funct);
	check_btn = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "checkbtn_multiply"));
	gtk_toggle_button_set_active(check_btn, settings->apply_multiply_funct);
	check_btn = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "checkbtn_add_to"));
	gtk_toggle_button_set_active(check_btn, settings->apply_add_funct);
}

static void default_fft_setting(OscPlot *plot, Transform *tr)
{
	OscPlotPrivate *priv = plot->priv;
	struct extra_info *ch_info;
	struct _fft_settings *settings;
	GtkBuilder *builder = priv->builder;
	GtkWidget *fft_size_widget;
	GtkWidget *fft_avg_widget;
	GtkWidget *fft_pwr_offset_widget;
	
	fft_size_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size"));
	fft_avg_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_avg"));
	fft_pwr_offset_widget = GTK_WIDGET(gtk_builder_get_object(builder, "pwr_offset"));

	ch_info = tr->channel_parent->extra_field;
	settings = (struct _fft_settings *)tr->settings;
	rebuild_fft_size_list(fft_size_widget, ch_info->device_parent->shadow_of_sample_count, settings->fft_size);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(fft_avg_widget), settings->fft_avg);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(fft_pwr_offset_widget), settings->fft_pwr_off);
}

static void default_constellation_setting(OscPlot *plot, Transform *tr)
{
	OscPlotPrivate *priv = plot->priv;
	struct extra_info *ch_info;
	struct _constellation_settings *settings;
	GtkAdjustment *constellation_sample_count_adj;
	GtkBuilder *builder = priv->builder;
	
	ch_info = tr->channel_parent->extra_field;
	settings = (struct _constellation_settings *)tr->settings;
	constellation_sample_count_adj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_constellation_sample_count"));
	gtk_adjustment_set_upper(constellation_sample_count_adj, (gdouble)ch_info->device_parent->shadow_of_sample_count);
	gtk_adjustment_set_value(constellation_sample_count_adj, (gdouble)settings->num_samples);
}

static void show_time_settings(GtkMenuItem* menuitem, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv  = plot->priv;
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreeIter iter;
	Transform *tr;
	GList *path;
	
	tree_view = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(tree_view);
	path = g_list_first(priv->selected_rows_paths);
	gtk_tree_model_get_iter(model, &iter, path->data);
	gtk_tree_model_get(model, &iter, ELEMENT_REFERENCE, &tr, -1);
	priv->selected_transform_for_setup = tr;
	g_object_set(G_OBJECT(tree_view), "sensitive", FALSE, NULL);
	default_time_setting(plot, tr);
	gtk_dialog_run(GTK_DIALOG(priv->time_settings_diag));
	gtk_widget_hide(priv->time_settings_diag);
}

static void show_fft_settings(GtkMenuItem* menuitem, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv  = plot->priv;
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreeIter iter;
	Transform *tr;
	GList *path;
	
	tree_view = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(tree_view);
	path = g_list_first(priv->selected_rows_paths);
	gtk_tree_model_get_iter(model, &iter, path->data);
	gtk_tree_model_get(model, &iter, ELEMENT_REFERENCE, &tr, -1);
	priv->selected_transform_for_setup = tr;
	g_object_set(G_OBJECT(tree_view), "sensitive", FALSE, NULL);
	default_fft_setting(plot, tr);
	gtk_dialog_run(GTK_DIALOG(priv->fft_settings_diag));
	gtk_widget_hide(priv->fft_settings_diag);
}

static void show_constellation_settings(GtkMenuItem* menuitem, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv  = plot->priv;
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreeIter iter;
	Transform *tr;
	GList *path;
	
	tree_view = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(tree_view);
	path = g_list_first(priv->selected_rows_paths);
	gtk_tree_model_get_iter(model, &iter, path->data);
	gtk_tree_model_get(model, &iter, ELEMENT_REFERENCE, &tr, -1);
	priv->selected_transform_for_setup = tr;
	g_object_set(G_OBJECT(tree_view), "sensitive", FALSE, NULL);
	default_constellation_setting(plot, tr);
	gtk_dialog_run(GTK_DIALOG(priv->constellation_settings_diag));
	gtk_widget_hide(priv->constellation_settings_diag);
}

static void clear_marker_flag(GtkTreeView *treeview)
{
	GtkTreeIter dev_iter, ch_iter, tr_iter;
	GtkTreeModel *model;
	gboolean next_dev_iter;
	gboolean next_ch_iter;
	gboolean next_tr_iter;
	Transform *tr;
	
	model = gtk_tree_view_get_model(treeview);
	if (!gtk_tree_model_get_iter_first(model, &dev_iter))
		return;
	
	next_dev_iter = true;
	while (next_dev_iter) {		
		gtk_tree_model_iter_children(model, &ch_iter, &dev_iter);
		next_ch_iter = true;
		while (next_ch_iter) {
			gtk_tree_model_iter_children(model, &tr_iter, &ch_iter);
				if (gtk_tree_model_iter_has_child(model, &ch_iter))
					next_tr_iter = true;
				else
					next_tr_iter = false;
				while (next_tr_iter) {
					gtk_tree_store_set(GTK_TREE_STORE(model), &tr_iter, MARKER_ENABLED, false, -1);
					gtk_tree_model_get(model, &tr_iter, ELEMENT_REFERENCE, &tr, -1);
					tr->has_the_marker = false;
					next_tr_iter = gtk_tree_model_iter_next(model, &tr_iter);
				}
			next_ch_iter = gtk_tree_model_iter_next(model, &ch_iter);
		}
		next_dev_iter = gtk_tree_model_iter_next(model, &dev_iter);
	}
}

static void apply_marker(GtkMenuItem* menuitem, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv  = plot->priv;
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreeIter iter;
	Transform *tr;
	GList *path;
	
	tree_view = GTK_TREE_VIEW(priv->channel_list_view);
	clear_marker_flag(tree_view);
	model = gtk_tree_view_get_model(tree_view);
	path = g_list_first(priv->selected_rows_paths);
	gtk_tree_model_get_iter(model, &iter, path->data);
	gtk_tree_model_get(model, &iter, ELEMENT_REFERENCE, &tr, -1);
	priv->selected_transform_for_setup = tr;
	gtk_tree_store_set(GTK_TREE_STORE(model), &iter, MARKER_ENABLED, true, -1);
	tr->has_the_marker = true;
}

static void show_sample_count_dialog(GtkMenuItem* menuitem, gpointer data)
{
	struct _device_list *dev_list = data;
	GtkBuilder *builder = dev_list->settings_dialog_builder;
	GtkWidget *dialog;
	GtkAdjustment *sample_count;
	
	dialog = GTK_WIDGET(gtk_builder_get_object(builder, "Sample_count_dialog"));
	sample_count = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_sample_count"));
	gtk_adjustment_set_value(sample_count, dev_list->sample_count);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_hide(dialog);
}

void get_pop_menu_position(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data)
{
	// Don't know how to do this...yet
	*x = 3000;
	*y = 140;
	*push_in = TRUE;
}

static void show_right_click_menu(GtkWidget *treeview, GdkEventButton *event, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;
	GtkWidget *menu, *menuitem;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean is_device;
	gboolean is_channel;
	gboolean is_transform;
	gpointer *ref;
	short i;
	static const char *transforms[3] = {"TIME", "FFT", "CONSTELLATION"};
	GList *path;
	
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));
	path = g_list_first(priv->selected_rows_paths);
	gtk_tree_model_get_iter(model, &iter, path->data);
	gtk_tree_model_get(model, &iter, IS_DEVICE, &is_device, IS_CHANNEL,
		&is_channel, IS_TRANSFORM, &is_transform, ELEMENT_REFERENCE, &ref, -1);
	
	/* Right-click menu for devices */
	if ((is_device == TRUE) && (priv->num_selected_rows == 1)) {
		menu = gtk_menu_new();
		menuitem = gtk_menu_item_new_with_label("Sample Count");
		g_signal_connect(menuitem, "activate",
					(GCallback) show_sample_count_dialog, ref);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		menuitem = gtk_menu_item_new_with_label("Input Generator");
		gtk_widget_set_sensitive(GTK_WIDGET(menuitem), trigger_update_current_device( ((struct _device_list *)ref)->device_name ));
		g_signal_connect(menuitem, "activate",
					(GCallback) trigger_dialog_show, NULL);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		goto show_menu;
	}
	
	/* Right-click menu for channels */
	if (is_channel == TRUE) {
		menu = gtk_menu_new();
		if (priv->num_selected_rows == 2) {
			path = g_list_next(priv->selected_rows_paths);
			gtk_tree_model_get_iter(model, &iter, path->data);
			gtk_tree_model_get(model, &iter, IS_CHANNEL, &is_channel, ELEMENT_REFERENCE, &ref, -1);
			if (is_channel == FALSE)
				return;	
			if ((priv->active_transform_type == NO_TRANSFORM_TYPE) ||
				(priv->active_transform_type == CONSTELLATION_TRANSFORM)) {
				menuitem = gtk_menu_item_new_with_label(transforms[2]);
				g_signal_connect(menuitem, "activate",
					(GCallback) add_transform_to_tree_store, data);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
				goto show_menu;
			}
		} else {
			/* Show all transform options when no other transform were added to this plot */
			if (priv->active_transform_type == NO_TRANSFORM_TYPE) {
				for (i = 0; i < 2; i++) {
					menuitem = gtk_menu_item_new_with_label(transforms[i]);
					g_signal_connect(menuitem, "activate",
						(GCallback) add_transform_to_tree_store, data);
					gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
				}
				goto show_menu;
			} else {
				if (priv->active_transform_type == CONSTELLATION_TRANSFORM)
					return;
					
				struct _device_list *device = GET_CHANNEL_PARENT(ref);
				if ((priv->active_transform_type == FFT_TRANSFORM) && (device != priv->current_device))
					return;
					
				menuitem = gtk_menu_item_new_with_label(transforms[priv->active_transform_type - 1]);
				g_signal_connect(menuitem, "activate",
					(GCallback) add_transform_to_tree_store, data);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);				
				goto show_menu;
			}
		}
	}	
	
	/* Right-click menu for transforms */
	if ((is_transform == TRUE) && (priv->num_selected_rows == 1)) {
		menu = gtk_menu_new();
		if (priv->active_transform_type == TIME_TRANSFORM) {
			menuitem = gtk_menu_item_new_with_label("Settings");
			g_signal_connect(menuitem, "activate",
				(GCallback) show_time_settings, data);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		} else if (priv->active_transform_type == FFT_TRANSFORM) {
			menuitem = gtk_menu_item_new_with_label("Settings");
			g_signal_connect(menuitem, "activate",
				(GCallback) show_fft_settings, data);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
			menuitem = gtk_menu_item_new_with_label("Apply Marker");
			g_signal_connect(menuitem, "activate",
				(GCallback) apply_marker, data);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		} else if (priv->active_transform_type == CONSTELLATION_TRANSFORM) {
			menuitem = gtk_menu_item_new_with_label("Settings");
			g_signal_connect(menuitem, "activate",
				(GCallback) show_constellation_settings, data);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		}
		menuitem = gtk_menu_item_new_with_label("Remove");
		g_signal_connect(menuitem, "activate",
			(GCallback) remove_transform_from_tree_store, data);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		goto show_menu;
	}

	return;

show_menu:
	gtk_widget_show_all(menu);
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, 
			(event != NULL) ? NULL : get_pop_menu_position, NULL,
			(event != NULL) ? event->button : 0, 
			gdk_event_get_time((GdkEvent*)event));

}

void highlight_selected_rows (gpointer data, gpointer user_data)
{
	GtkTreePath *path = data;
	GtkTreeSelection *selection = user_data;
	
	gtk_tree_selection_select_path(selection, path);
}

static gboolean right_click_on_ch_list_cb(GtkWidget *treeview, GdkEventButton *event, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;
	
	/* single click with the right mouse button */
    if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3) {
		GtkTreeSelection *selection;
		
		if (priv->redraw_function > 0)
		return TRUE;

		selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
		int rows = gtk_tree_selection_count_selected_rows(selection);
		if ((rows == 1) || (rows == 2)) {
			GtkTreeModel *model;
			
			/* Remove the previous content of the path list */
			if (priv->selected_rows_paths) {
				g_list_foreach(priv->selected_rows_paths, (GFunc)gtk_tree_path_free, NULL);
				g_list_free(priv->selected_rows_paths);
			}
			
			model =  gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));
			priv->selected_rows_paths = gtk_tree_selection_get_selected_rows(selection, &model);
			gtk_tree_selection_unselect_all(selection);
			priv->num_selected_rows = rows;
			g_list_foreach(priv->selected_rows_paths, highlight_selected_rows, selection);
			show_right_click_menu(treeview, event, data);
		}
	
		return TRUE;
	}
	
	return FALSE;
}

static gboolean shift_f10_event_on_ch_list_cb(GtkWidget *treeview, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	int rows;
	
	if (priv->redraw_function > 0)
		return TRUE;
	
	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
	rows = gtk_tree_selection_count_selected_rows(selection);
	
	if ((rows == 1) || (rows == 2)) {			
			/* Remove the previous content of the path list */
			if (priv->selected_rows_paths) {
				g_list_foreach(priv->selected_rows_paths, (GFunc)gtk_tree_path_free, NULL);
				g_list_free(priv->selected_rows_paths);
			}
			
			model =  gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));
			priv->selected_rows_paths = gtk_tree_selection_get_selected_rows(selection, &model);
			gtk_tree_selection_unselect_all(selection);
			priv->num_selected_rows = rows;
			g_list_foreach(priv->selected_rows_paths, highlight_selected_rows, selection);
			show_right_click_menu(treeview, NULL, data);
		}
	
		return TRUE;
}

static void draw_marker_values(OscPlotPrivate *priv, Transform *tr)
{
	struct _fft_settings *settings = tr->settings;
	struct extra_info *ch_info;
	gfloat *markX = settings->markX;
	gfloat *markY = settings->markY;
	GtkTextIter iter;
	char text[256];
	int m;
	
	if (priv->tbuf == NULL) {
			priv->tbuf = gtk_text_buffer_new(NULL);
			gtk_text_view_set_buffer(GTK_TEXT_VIEW(priv->marker_label), priv->tbuf);
	}
	ch_info = tr->channel_parent->extra_field;
	for (m = 0; m <= MAX_MARKERS; m++) {
		sprintf(text, "M%i: %2.2f dB @ %2.2f %s\n",
				m, markY[m], markX[m], ch_info->device_parent->adc_scale);

		if (m == 0) {
			gtk_text_buffer_set_text(priv->tbuf, text, -1);
			gtk_text_buffer_get_iter_at_line(priv->tbuf, &iter, 1);
		} else {
			gtk_text_buffer_insert(priv->tbuf, &iter, text, -1);
		}
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

static gboolean active_channels_check(GtkTreeView *treeview)
{
	GtkTreeIter iter;
	GtkTreeIter child_iter;
	GtkTreeModel *model;
	gboolean next_iter;
	gboolean next_child_iter;
	gboolean no_active_channels = TRUE;
	
	model = gtk_tree_view_get_model(treeview);
	if (!gtk_tree_model_get_iter_first(model, &iter))
		return FALSE;
	
	next_iter = true;
	while (next_iter) {		
		gtk_tree_model_iter_children(model, &child_iter, &iter);
		next_child_iter = true;
		while (next_child_iter) {
			if (gtk_tree_model_iter_has_child(model, &child_iter))
				no_active_channels = FALSE;
			next_child_iter = gtk_tree_model_iter_next(model, &child_iter);
		}
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}
	
	return !no_active_channels;
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
		priv->redraw_function = g_timeout_add(50, (GSourceFunc) plot_redraw, priv);
	}
}

static void add_markers(OscPlot *plot, Transform *transform)
{
	GtkDatabox *databox = GTK_DATABOX(plot->priv->databox);
	struct _fft_settings *settings = transform->settings;
	gfloat *markX = settings->markX;
	gfloat *markY = settings->markY;
	GtkDataboxGraph **marker = (GtkDataboxGraph **)settings->marker;
	char buf[10];
	int m;
	
	for (m = 0; m <= MAX_MARKERS; m++) {
		markX[m] = 0.0f;
		markY[m] = -100.0f;
		marker[m] = gtk_databox_markers_new(1, &markX[m], &markY[m], &color_marker, 10, GTK_DATABOX_MARKERS_TRIANGLE);
		sprintf(buf, "M%i", m);
		gtk_databox_markers_set_label(GTK_DATABOX_MARKERS(marker[m]), 0, GTK_DATABOX_MARKERS_TEXT_N, buf, FALSE);
		gtk_databox_graph_add(databox, marker[m]);
		gtk_databox_graph_set_hide(GTK_DATABOX_GRAPH(settings->marker[m]), !transform->graph_active);
	}
	
}

static void check_transform_settings(Transform *tr, int transform_type)
{
	struct _device_list *device = GET_CHANNEL_PARENT(tr->channel_parent);
	gfloat sample_count  = device->shadow_of_sample_count;
	
	if (transform_type == TIME_TRANSFORM) {
		if (((struct _time_settings *)tr->settings)->num_samples > sample_count)
			((struct _time_settings *)tr->settings)->num_samples = sample_count;
	} else if(transform_type == FFT_TRANSFORM) {
		while (((struct _fft_settings *)tr->settings)->fft_size > sample_count)
			((struct _fft_settings *)tr->settings)->fft_size /= 2;
	} else if(transform_type == CONSTELLATION_TRANSFORM) {
		if (((struct _constellation_settings *)tr->settings)->num_samples > sample_count)
			((struct _constellation_settings *)tr->settings)->num_samples = sample_count;
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
	const char *empty_text = " ";
	int i;

	gtk_databox_graph_remove_all(GTK_DATABOX(priv->databox));
	
	/* Remove FFT Marker info */
	if (priv->tbuf)
		gtk_text_buffer_set_text(priv->tbuf, empty_text, -1);
		
	for (i = 0; i < tr_list->size; i++) {
		transform = tr_list->transforms[i];
		check_transform_settings(transform, priv->active_transform_type);
		Transform_setup(transform);
		transform_x_axis = Transform_get_x_axis_ref(transform);
		transform_y_axis = Transform_get_y_axis_ref(transform);
				
		if (strcmp(gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->plot_type)), "Lines"))
			transform->graph = gtk_databox_points_new(transform->y_axis_size, transform_x_axis, transform_y_axis, transform->graph_color, 3);
		else
			transform->graph = gtk_databox_lines_new(transform->y_axis_size, transform_x_axis, transform_y_axis, transform->graph_color, 1);
		
		ch_info = transform->channel_parent->extra_field;
		if (transform->x_axis_size > max_x_axis)
			max_x_axis = transform->x_axis_size;
		if (ch_info->device_parent->adc_freq > max_adc_freq)
			max_adc_freq = ch_info->device_parent->adc_freq;
			
		if (priv->active_transform_type == FFT_TRANSFORM)
			if (transform->has_the_marker)
				add_markers(plot, transform);
		
		gtk_databox_graph_add(GTK_DATABOX(priv->databox), transform->graph);
		gtk_databox_graph_set_hide(GTK_DATABOX_GRAPH(transform->graph), !transform->graph_active);
	}
	osc_plot_update_rx_lbl(plot);
}

static void capture_button_clicked_cb(GtkToggleToolButton *btn, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;
	gboolean button_state;
	
	button_state = gtk_toggle_tool_button_get_active(btn);
	
 	if (button_state) {
		if (active_channels_check((GtkTreeView *)priv->channel_list_view) > 0)
			g_signal_emit(plot, oscplot_signals[CAPTURE_EVENT_SIGNAL], 0, button_state);
		else
			goto play_err;
			
		plot_setup(plot);
		
		add_grid(plot);
		gtk_widget_queue_draw(priv->databox);
		priv->frame_counter = 0;
		capture_start(priv);
	} else {
		priv->stop_redraw = TRUE;
		g_signal_emit(plot, oscplot_signals[CAPTURE_EVENT_SIGNAL], 0, button_state);
	}
	
	return;
	
play_err:
	gtk_toggle_tool_button_set_active(btn, FALSE);
}

static void fullscreen_button_clicked_cb(GtkToggleToolButton *btn, gpointer data)
{
	OscPlot *plot = data;
	
	if (gtk_toggle_tool_button_get_active(btn))
		gtk_window_fullscreen(GTK_WINDOW(plot->priv->window));
	else
		gtk_window_unfullscreen(GTK_WINDOW(plot->priv->window));
}

static void transform_toggled(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer data)
{
	OscPlotPrivate *priv = data;
	GtkTreePath* path = gtk_tree_path_new_from_string(pathStr);
	GtkTreeModel *model;
	GtkTreeIter iter;
	Transform *tr;
	struct _fft_settings *settings;
	gboolean active;
	int m;
	
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->channel_list_view));
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, TRANSFORM_ACTIVE, &active, ELEMENT_REFERENCE, &tr, -1);
	active = !active;
	gtk_tree_store_set(GTK_TREE_STORE(model), &iter, TRANSFORM_ACTIVE, active, -1);
	tr->graph_active = (active != 0) ? TRUE : FALSE;
	gtk_tree_path_free(path);
	
	if (tr->graph) {
		gtk_databox_graph_set_hide(GTK_DATABOX_GRAPH(tr->graph), !active);
		settings = tr->settings;
		if (tr->has_the_marker)
			for (m = 0; m <= MAX_MARKERS; m++)
				if (settings->marker[m])
					gtk_databox_graph_set_hide(GTK_DATABOX_GRAPH(settings->marker[m]), !active);
		gtk_widget_queue_draw(GTK_WIDGET(priv->databox));
	}
}

static void create_channel_list_view(OscPlotPrivate *priv)
{
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeViewColumn *col;
	GtkCellRenderer *renderer_ch_name;
	GtkCellRenderer *renderer_tr_toggle;
	GtkCellRenderer *renderer_tr_has_marker;
	GtkCellRenderer *renderer_tr_color;
	
	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(col, "Channels");
	gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
	
	renderer_ch_name = gtk_cell_renderer_text_new();
	renderer_tr_toggle = gtk_cell_renderer_toggle_new();
	renderer_tr_has_marker = gtk_cell_renderer_text_new();
	renderer_tr_color = gtk_cell_renderer_pixbuf_new();
	
	gtk_tree_view_column_pack_end(col, renderer_ch_name, FALSE);
	gtk_tree_view_column_pack_end(col, renderer_tr_color, FALSE);
	gtk_tree_view_column_pack_end(col, renderer_tr_has_marker, FALSE);
	gtk_tree_view_column_pack_end(col, renderer_tr_toggle, FALSE);
	
	gtk_tree_view_column_add_attribute(col, renderer_ch_name, "text", ELEMENT_NAME);
	gtk_tree_view_column_add_attribute(col, renderer_tr_toggle, "visible", IS_TRANSFORM);
	gtk_tree_view_column_add_attribute(col, renderer_tr_color, "visible", IS_TRANSFORM);
	gtk_tree_view_column_add_attribute(col, renderer_tr_toggle, "active", TRANSFORM_ACTIVE);
	gtk_tree_view_column_add_attribute(col, renderer_tr_has_marker, "visible", MARKER_ENABLED);
	gtk_tree_view_column_add_attribute(col, renderer_tr_color, "cell-background-gdk", COLOR_REF);
	g_object_set(renderer_tr_has_marker, "text", "M :", NULL);
	
	g_object_set(renderer_tr_color, "width", 15, NULL);
	g_object_set(renderer_tr_color, "mode", GTK_CELL_RENDERER_MODE_EDITABLE, NULL);
	
	g_signal_connect(G_OBJECT(renderer_tr_toggle), "toggled", G_CALLBACK(transform_toggled), priv);	
}

static void fill_channel_list(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeIter iter;
	GtkTreeIter child;
	GtkTreeStore *treestore;
	int i, j;
	
	treestore = GTK_TREE_STORE(gtk_tree_view_get_model(treeview));
	for (i = 0; i < num_devices; i++) {
		gtk_tree_store_append(treestore, &iter, NULL);
		gtk_tree_store_set(treestore, &iter, ELEMENT_NAME,  device_list[i].device_name, IS_DEVICE, TRUE, ELEMENT_REFERENCE, &device_list[i], -1);
		for (j = 0; j < device_list[i].num_channels; j++) {
			if (strcmp("in_timestamp", device_list[i].channel_list[j].name) != 0) {
				gtk_tree_store_append(treestore, &child, &iter);
				gtk_tree_store_set(treestore, &child, ELEMENT_NAME, device_list[i].channel_list[j].name,
					IS_CHANNEL, TRUE, CHANNEL_ACTIVE, FALSE, ELEMENT_REFERENCE, &device_list[i].channel_list[j], -1);
				}
		}
	}	
	create_channel_list_view(priv);
}

static gboolean capture_button_icon_transform(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer user_data)
{
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
	} else if (priv->active_transform_type == CONSTELLATION_TRANSFORM) {
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

static void plot_destroyed (GtkWidget *object, OscPlot *plot)
{
	remove_all_transforms(plot);
	plot->priv->stop_redraw = TRUE;
	if (plot->priv->redraw_function) {
		g_signal_emit(plot, oscplot_signals[CAPTURE_EVENT_SIGNAL], 0, FALSE);
	}
	g_signal_emit(plot, oscplot_signals[DESTROY_EVENT_SIGNAL], 0);
	gtk_widget_destroy(plot->priv->window);
}

#define ENTER_KEY_CODE 0xFF0D

gboolean save_settings_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	if ((event->type == GDK_KEY_RELEASE) && (event->keyval == ENTER_KEY_CODE)) {
		g_signal_emit_by_name(widget, "response", GTK_RESPONSE_OK, 0);
	}
	
	return FALSE;
}

static void cb_saveas(GtkToolButton *toolbutton, OscPlot *data)
{
	OscPlotPrivate *priv = data->priv;

	gtk_widget_show(priv->saveas_dialog);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (priv->saveas_dialog), "~/");
	gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (priv->saveas_dialog));
}

void cb_saveas_response(GtkDialog *dialog, gint response_id, OscPlot *data)
{
	/* Save as Dialog */
	OscPlotPrivate *priv = data->priv;
	char *filename;
		
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (priv->saveas_dialog), "~/");
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(priv->saveas_dialog), true);
	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (priv->saveas_dialog));
	if (filename) {
		switch(response_id) {
			/* Response Codes encoded in glade file */
			case GTK_RESPONSE_CANCEL:
			break;
			case 3:
			case 2:	{
					GdkPixbuf *pixbuf;
					GError *err=NULL;
					GdkColormap *cmap;
					gint width, height;
					gboolean ret = true;

					cmap = gdk_window_get_colormap(
							GDK_DRAWABLE(gtk_widget_get_window(priv->capture_graph)));
					gdk_drawable_get_size(GDK_DRAWABLE(gtk_widget_get_window(priv->capture_graph)),
							&width, &height);
					pixbuf = gdk_pixbuf_get_from_drawable(NULL,
							GDK_DRAWABLE(gtk_widget_get_window(priv->capture_graph)),
							cmap, 0, 0, 0, 0, width, height);

					if (pixbuf)
						ret = gdk_pixbuf_save(pixbuf, filename, "png", &err, NULL);
					if (!pixbuf || !ret)
						printf("error creating %s\n", filename);
				}
				break;
			default:
				printf("response_id : %i\n", response_id);
		}
		g_free(filename);
	}
	gtk_widget_hide(priv->saveas_dialog);
}

static void enable_auto_scale_cb(GtkToggleButton *button, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enable_auto_scale))) {
		gtk_widget_set_sensitive(plot->priv->y_axis_max, FALSE);
		gtk_widget_set_sensitive(plot->priv->y_axis_min, FALSE);
	} else
	{
		gtk_widget_set_sensitive(plot->priv->y_axis_max, TRUE);
		gtk_widget_set_sensitive(plot->priv->y_axis_min, TRUE);
	}
}

static void create_plot(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	
	GtkWidget *table;
	GtkBuilder *builder = NULL;
	GtkTreeSelection *tree_selection;
	GtkWidget *fft_size_widget;
	GtkDataboxRuler *ruler_y;
	GtkTreeStore *tree_store;
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
	priv->time_settings_diag = GTK_WIDGET(gtk_builder_get_object(builder, "dialog_TIME_settings"));
	priv->fft_settings_diag = GTK_WIDGET(gtk_builder_get_object(builder, "dialog_FFT_settings"));
	priv->constellation_settings_diag = GTK_WIDGET(gtk_builder_get_object(builder, "dialog_CONSTELLATION_settings"));
	priv->capture_graph = GTK_WIDGET(gtk_builder_get_object(builder, "display_capture"));
	priv->capture_button = GTK_WIDGET(gtk_builder_get_object(builder, "capture_button"));
	priv->channel_list_view = GTK_WIDGET(gtk_builder_get_object(builder, "channel_list_view"));
	priv->show_grid = GTK_WIDGET(gtk_builder_get_object(builder, "show_grid"));
	priv->plot_type = GTK_WIDGET(gtk_builder_get_object(builder, "plot_type"));
	priv->enable_auto_scale = GTK_WIDGET(gtk_builder_get_object(builder, "auto_scale"));
	priv->hor_scale = GTK_WIDGET(gtk_builder_get_object(builder, "hor_scale"));
	priv->marker_label = GTK_WIDGET(gtk_builder_get_object(builder, "marker_info"));
	priv->saveas_button = GTK_WIDGET(gtk_builder_get_object(builder, "save_as"));
	priv->saveas_dialog = GTK_WIDGET(gtk_builder_get_object(builder, "saveas_dialog"));
	priv->fullscreen_button = GTK_WIDGET(gtk_builder_get_object(builder, "fullscreen_toggle"));
	priv->y_axis_max = GTK_WIDGET(gtk_builder_get_object(builder, "spin_Y_max"));
	priv->y_axis_min = GTK_WIDGET(gtk_builder_get_object(builder, "spin_Y_min"));
	fft_size_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size"));
	priv->tbuf = NULL;
	
	/* Create a GtkDatabox widget along with scrollbars and rulers */
	gtk_databox_create_box_with_scrollbars_and_rulers(&priv->databox, &table,
		TRUE, TRUE, TRUE, TRUE);
	gtk_box_pack_start(GTK_BOX(priv->capture_graph), table, TRUE, TRUE, 0);
	gtk_widget_modify_bg(priv->databox, GTK_STATE_NORMAL, &color_background);
	gtk_widget_set_size_request(table, 320, 240);
	ruler_y = gtk_databox_get_ruler_y(GTK_DATABOX(priv->databox));
	
	/* Create a Tree Store that holds information about devices */
	tree_store = gtk_tree_store_new(NUM_COL,
									G_TYPE_STRING,
									G_TYPE_BOOLEAN,
									G_TYPE_BOOLEAN,
									G_TYPE_BOOLEAN,
									G_TYPE_BOOLEAN,
									G_TYPE_BOOLEAN,
									G_TYPE_BOOLEAN,
									G_TYPE_POINTER,
									GDK_TYPE_COLOR);
	gtk_tree_view_set_model((GtkTreeView *)priv->channel_list_view, (GtkTreeModel *)tree_store);
	fill_channel_list(plot);
	
	/* Fill the color list with the available colors for the graph */
	for (i = sizeof(color_graph) / sizeof(color_graph[0]) - 1; i >= 0; i--)
		priv->available_graph_colors = g_list_prepend(priv->available_graph_colors, &color_graph[i]);
	
	/* Connect Signals */
	g_signal_connect(G_OBJECT(priv->window), "destroy", G_CALLBACK(plot_destroyed), plot);
	
	g_signal_connect(priv->capture_button, "toggled",
		G_CALLBACK(capture_button_clicked_cb), plot);
	g_signal_connect(priv->channel_list_view, "button-press-event",
		G_CALLBACK(right_click_on_ch_list_cb), plot);
    g_signal_connect(priv->channel_list_view, "popup-menu", 
		G_CALLBACK(shift_f10_event_on_ch_list_cb), plot);
	g_signal_connect(priv->time_settings_diag, "response",
		G_CALLBACK(set_time_settings_cb), plot);
	g_signal_connect(priv->fft_settings_diag, "response",
		G_CALLBACK(set_fft_settings_cb), plot);
	g_signal_connect(priv->constellation_settings_diag, "response",
		G_CALLBACK(set_constellation_settings_cb), plot);
	g_signal_connect(priv->saveas_button, "clicked",
		G_CALLBACK(cb_saveas), plot);
	g_signal_connect(priv->saveas_dialog, "response", 
		G_CALLBACK(cb_saveas_response), plot);
	g_signal_connect(priv->saveas_dialog, "delete-event",
		G_CALLBACK(gtk_widget_hide_on_delete), plot);
	g_signal_connect(priv->fullscreen_button, "toggled",
		G_CALLBACK(fullscreen_button_clicked_cb), plot);
	g_signal_connect(priv->enable_auto_scale, "toggled",
		G_CALLBACK(enable_auto_scale_cb), plot);
	g_signal_connect(priv->time_settings_diag, "key_release_event",
		G_CALLBACK(save_settings_cb), NULL);
	g_signal_connect(priv->fft_settings_diag, "key_release_event",
		G_CALLBACK(save_settings_cb), NULL);
	g_signal_connect(priv->constellation_settings_diag, "key_release_event",
		G_CALLBACK(save_settings_cb), NULL);
	
	g_builder_connect_signal(builder, "zoom_in", "clicked",
		G_CALLBACK(zoom_in), plot);
	g_builder_connect_signal(builder, "zoom_out", "clicked",
		G_CALLBACK(zoom_out), plot);
	g_builder_connect_signal(builder, "zoom_fit", "clicked",
		G_CALLBACK(zoom_fit), plot);
	g_signal_connect(priv->show_grid, "toggled",
		G_CALLBACK(show_grid_toggled), plot);
	
	/* Create Bindings */
	g_object_bind_property_full(priv->capture_button, "active", priv->capture_button,
		"stock-id", 0, capture_button_icon_transform, NULL, NULL, NULL);
	g_object_bind_property_full(priv->fullscreen_button, "active", priv->fullscreen_button,
		"stock-id", 0, fullscreen_button_icon_transform, NULL, NULL, NULL);
	g_object_bind_property(ruler_y, "lower", priv->y_axis_max, "value", G_BINDING_BIDIRECTIONAL);
	g_object_bind_property(ruler_y, "upper", priv->y_axis_min, "value", G_BINDING_BIDIRECTIONAL);

	gtk_combo_box_set_active(GTK_COMBO_BOX(fft_size_widget), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_type), 0);
	tree_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->channel_list_view));
	gtk_tree_selection_set_mode(tree_selection, GTK_SELECTION_MULTIPLE);
	add_grid(plot);
	
	gtk_window_set_modal(GTK_WINDOW(priv->saveas_dialog), FALSE);
	gtk_widget_show(priv->window);
	gtk_widget_show_all(priv->capture_graph);	
}
