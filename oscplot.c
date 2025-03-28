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
#include <stdlib.h>
#include <math.h>
#include <matio.h>
#include <sys/time.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

#include <complex.h>
#include <fftw3.h>
#include <iio.h>

#include "osc.h"
#include "oscplot.h"
#include "config.h"
#include "iio_widget.h"
#include "datatypes.h"
#include "osc_plugin.h"
#include "math_expression_generator.h"
#include "iio_utils.h"

/* add backwards compat for <matio-1.5.0 */
#if MATIO_MAJOR_VERSION == 1 && MATIO_MINOR_VERSION < 5
typedef int mat_dim;
#else
typedef size_t mat_dim;
#endif

/* timersub, macros are _BSD_SOURCE, and aren't included in windows */
#ifndef timersub
#define timersub(a, b, result) \
	do { \
		(result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
		(result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
		if ((result)->tv_usec < 0) { \
			--(result)->tv_sec; \
			(result)->tv_usec += 1000000; \
		} \
	} while (0)
#endif /* timersub */

extern void *find_setup_check_fct_by_devname(const char *dev_name);

static int (*plugin_setup_validation_fct)(struct iio_device *, const char **) = NULL;
static unsigned object_count = 0;

static void create_plot (OscPlot *plot);
static void plot_setup(OscPlot *plot);
static void capture_button_clicked_cb (GtkToggleToolButton *btn, gpointer data);
static void single_shot_clicked_cb (GtkToggleToolButton *btn, gpointer data);
static void update_grid(OscPlot *plot, gfloat min, gfloat max);
static void add_grid(OscPlot *plot);
static void rescale_databox(OscPlotPrivate *priv, GtkDatabox *box, gfloat border);
static bool call_all_transform_functions(OscPlotPrivate *priv);
static void capture_start(OscPlotPrivate *priv);
static void plot_profile_save(OscPlot *plot, char *filename);
static void transform_add_plot_markers(OscPlot *plot, Transform *transform);
static void osc_plot_finalize(GObject *object);
static void osc_plot_dispose(GObject *object);
static void save_as(OscPlot *plot, const char *filename, int type);
static void treeview_expand_update(OscPlot *plot);
static void treeview_icon_color_update(OscPlot *plot);
static int enabled_channels_of_device(GtkTreeView *treeview, const char *name, unsigned *enabled_mask);
static int enabled_channels_count(OscPlot *plot);
static int num_of_channels_of_device(GtkTreeView *treeview, const char *name);
static gboolean get_iter_by_name(GtkTreeView *tree, GtkTreeIter *iter, const char *dev_name, const char *ch_name);
static void set_marker_labels (OscPlot *plot, gchar *buf, enum marker_types type);
static void channel_color_icon_set_color(GdkPixbuf *pb, GdkRGBA *color);
static int comboboxtext_set_active_by_string(GtkComboBox *combo_box, const char *name);
static int comboboxtext_get_active_text_as_int(GtkComboBoxText* combobox);
static gboolean check_valid_setup(OscPlot *plot);
static int device_find_by_name(struct iio_context *ctx, const char *name);
static int channel_find_by_name(struct iio_context *ctx, int device_index, const char *name);
static void device_rx_info_update(OscPlotPrivate *priv);
static gdouble prefix2scale (char adc_scale);
static struct iio_device * transform_get_device_parent(Transform *transform);
static gboolean tree_get_selected_row_iter(GtkTreeView *treeview, GtkTreeIter *iter);
static void set_channel_shadow_of_enabled(gpointer data, gpointer user_data);
static gfloat * plot_channels_get_nth_data_ref(GSList *list, guint n);
static void transform_add_own_markers(OscPlot *plot, Transform *transform);
static void transform_remove_own_markers(Transform *transform);
static bool set_channel_state_in_tree_model(GtkTreeModel *model, GtkTreeIter* chn_iter, gboolean state);

/* IDs of signals */
enum {
	CAPTURE_EVENT_SIGNAL,
	DESTROY_EVENT_SIGNAL,
	NEWPLOT_EVENT_SIGNAL,
	LAST_SIGNAL
};

/* signals will be configured during class init */
static guint oscplot_signals[LAST_SIGNAL] = { 0 };

/* Columns of the device treestore */
enum {
	ELEMENT_NAME,
	IS_DEVICE,
	IS_CHANNEL,
	CHANNEL_TYPE,
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

/* Horizontal Scale Types */
enum {
	HOR_SCALE_SAMPLES,
	HOR_SCALE_TIME,
	HOR_SCALE_NUM_OPTIONS
};

/* Types of channels that can be displayed on a plot */
enum {
	PLOT_IIO_CHANNEL = 0,
	PLOT_MATH_CHANNEL,
	NUM_PLOT_CHANNELS_TYPES
};

#define MATH_CHANNELS_DEVICE "Math"

#define OSC_COLOR(r, g, b, a) { \
	.red = (double) ((r) << 8) / 65535.0, \
	.green = (double) ((g) << 8) / 65535.0, \
	.blue = (double) ((b) << 8) / 65535.0, \
	.alpha = (a) \
}

static GdkRGBA color_graph[] = {
	OSC_COLOR(138, 226, 52, 1.0),
	OSC_COLOR(239, 41, 41, 1.0),
	OSC_COLOR(114, 159, 207, 1.0),
	OSC_COLOR(252, 175, 62, 1.0),
	OSC_COLOR(211, 215, 208, 1.0),
	OSC_COLOR(252, 233, 79, 1.0),
	OSC_COLOR(173, 127, 168, 1.0),
	OSC_COLOR(233, 185, 110, 1.0),

	OSC_COLOR(115, 210, 22, 1.0),
	OSC_COLOR(204, 0, 0, 1.0),
	OSC_COLOR(52, 101, 164, 1.0),
	OSC_COLOR(245, 121, 0, 1.0),
	OSC_COLOR(186, 189, 182, 1.0),
	OSC_COLOR(237, 212, 0, 1.0),
	OSC_COLOR(117, 80, 123, 1.0),
	OSC_COLOR(193, 125, 17, 1.0),
};

#define NUM_GRAPH_COLORS (sizeof(color_graph) / sizeof(color_graph[0]))

static GdkRGBA color_grid = {
	.red = 0.778210117, /* = 51000 / 65535 */
	.green = 0.778210117, /* = 51000 / 65535 */
	.blue = 0,
	.alpha = 1.0
};

static GdkRGBA color_marker = {
	.red = 1.0,
	.green = 0,
	.blue = 1.0,
	.alpha = 1.0
};

typedef struct channel_settings PlotChn;
typedef struct iio_channel_settings PlotIioChn;
typedef struct math_channel_settings PlotMathChn;

struct channel_settings {
	unsigned type;
	char *name;
	char *parent_name;
	struct iio_device *dev;
	struct iio_context *ctx;
	GdkRGBA graph_color;

	gfloat * (*get_data_ref)(PlotChn *);
	void (*assert_used_iio_channels)(PlotChn *, bool);
	void (*destroy)(PlotChn *);
};

struct iio_channel_settings {
	PlotChn base;
	struct iio_channel *iio_chn;
	bool apply_inverse_funct;
	bool apply_multiply_funct;
	bool apply_add_funct;
	double multiply_value;
	double add_value;
};

struct math_channel_settings {
	PlotChn base;
	GSList *iio_channels;
	gfloat  ***iio_channels_data;
	int num_channels;
	char *iio_device_name;
	char *txt_math_expression;
	void (*math_expression)(float ***channels_data, float *out_data, unsigned long long chn_sample_cnt);
	void *math_lib_handler;
	float *data_ref;
};

/* Helpers */
#define TIME_SETTINGS(obj) ((struct _time_settings *)obj->settings)
#define FFT_SETTINGS(obj) ((struct _fft_settings *)obj->settings)
#define CONSTELLATION_SETTINGS(obj) ((struct _constellation_settings *)obj->settings)
#define XCORR_SETTINGS(obj) ((struct _cross_correlation_settings *)obj->settings)
#define FREQ_SPECTRUM_SETTINGS(obj) ((struct _freq_spectrum_settings *)obj->settings)
#define MATH_SETTINGS(obj) ((struct _math_settings *)obj->settings)

#define PLOT_CHN(obj) ((PlotChn *)obj)
#define PLOT_IIO_CHN(obj) ((PlotIioChn *)obj)
#define PLOT_MATH_CHN(obj) ((PlotMathChn *)obj)

struct int_and_plot {
	int int_obj;
	OscPlot *plot;
};

struct string_and_plot {
	char *string_obj;
	OscPlot *plot;
};

struct plot_geometry {
	gint width;
	gint height;
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
	GtkWidget *ss_button;
	GtkWidget *channel_list_view;
	GtkWidget *show_grid;
	GtkWidget *plot_type;
	GtkWidget *plot_domain;
	GtkWidget *enable_auto_scale;
	GtkWidget *hor_scale;
	GtkWidget *hor_units;
	GtkWidget *marker_label;
	GtkWidget *devices_label;
	GtkWidget *phase_label;
	GtkWidget *saveas_button;
	GtkWidget *saveas_dialog;
	GtkWidget *saveas_type_dialog;
	GtkWidget *title_edit_dialog;
	GtkWidget *fullscreen_button;
	GtkWidget *menu_fullscreen;
	GtkWidget *menu_show_options;
	GtkWidget *y_axis_max;
	GtkWidget *y_axis_min;
	GtkWidget *viewport_saveas_channels;
	GtkWidget *saveas_channels_list;
	GtkWidget *saveas_select_channel_message;
	GtkWidget *device_combobox;
	GtkWidget *sample_count_widget;
	unsigned int sample_count;
	GtkWidget *fft_size_widget;
	GtkWidget *fft_win_widget;
	GtkWidget *fft_win_correction;
	GtkWidget *fft_avg_widget;
	GtkWidget *fft_pwr_offset_widget;
	GtkWidget *device_settings_menu;
	GtkWidget *math_settings_menu;
	GtkWidget *device_trigger_menuitem;
	GtkWidget *math_menuitem;
	GtkWidget *plot_trigger_menuitem;
	GtkWidget *channel_settings_menu;
	GtkWidget *math_channel_settings_menu;
	GtkWidget *channel_expression_edit_menuitem;
	GtkWidget *channel_iio_color_menuitem;
	GtkWidget *channel_math_color_menuitem;
	GtkWidget *channel_math_menuitem;
	GtkWidget *channel_remove_menuitem;
	GtkWidget *math_dialog;
	GtkWidget *capture_options_box;
	GtkWidget *saveas_settings_box;
	GtkWidget *save_mat_scale;
	GtkWidget *new_plot_button;
	GtkWidget *cmb_saveas_type;
	GtkWidget *math_expression_dialog;
	GtkWidget *math_expression_textview;
	GtkWidget *math_device_select;
	GtkWidget *math_channel_name_entry;
	GtkWidget *math_expr_error;

	GtkCssProvider *provider;

	GtkTextBuffer* tbuf;
	GtkTextBuffer* devices_buf;
	GtkTextBuffer* phase_buf;
	GtkTextBuffer* math_expression;

	OscPlotPreferences *preferences;

	struct iio_context *ctx;

	unsigned int nb_input_devices;
	unsigned int nb_plot_channels;

	struct plot_geometry size;

	int frame_counter;
	double fps;
	struct timeval last_update;

	int last_hor_unit;

	int do_a_rescale_flag;

	gulong capture_button_hid;
	gint deactivate_capture_btn_flag;

	bool single_shot_mode;

	/* A reference to the device holding the most recent created transform */
	struct iio_device *current_device;

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

	/* Spectrum mode - Parameters */
	unsigned fft_count;
	double start_freq;
	double filter_bw;

	gint line_thickness;

	gint redraw_function;
	gboolean stop_redraw;
	gboolean redraw;

	bool spectrum_data_ready;

	gboolean fullscreen_state;

	bool profile_loaded_scale;

	bool save_as_png;

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

	gint plot_x_pos;
	gint plot_y_pos;

	gfloat plot_left;
	gfloat plot_right;
	gfloat plot_top;
	gfloat plot_bottom;
	int read_scale_params;

	GMutex g_marker_copy_lock;

	void (*quit_callback)(void *user_data);
	void *qcb_user_data;
};

G_DEFINE_TYPE_WITH_PRIVATE(OscPlot, osc_plot, GTK_TYPE_WIDGET)

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
			g_cclosure_marshal_VOID__BOOLEAN,
			G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	oscplot_signals[DESTROY_EVENT_SIGNAL] = g_signal_new("osc-destroy-event",
			G_TYPE_FROM_CLASS (klass),
			G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			G_STRUCT_OFFSET (OscPlotClass, destroy_event),
			NULL,
			NULL,
			g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	oscplot_signals[NEWPLOT_EVENT_SIGNAL] = g_signal_new("osc-newplot-event",
			G_TYPE_FROM_CLASS (klass),
			G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			G_STRUCT_OFFSET (OscPlotClass, newplot_event),
			NULL,
			NULL,
			g_cclosure_marshal_VOID__POINTER,
			G_TYPE_NONE, 1, G_TYPE_POINTER);
}

static void osc_plot_init(OscPlot *plot)
{
	plot->priv = osc_plot_get_instance_private (plot);
}

GtkWidget *osc_plot_new(struct iio_context *ctx)
{
	GtkWidget *plot;

	plot = GTK_WIDGET(g_object_new(OSC_PLOT_TYPE, NULL));
	OSC_PLOT(plot)->priv->ctx = ctx;
	create_plot(OSC_PLOT(plot));

	return plot;
}

GtkWidget *osc_plot_new_with_pref(struct iio_context *ctx, OscPlotPreferences *pref)
{
	GtkWidget *plot;

	plot = GTK_WIDGET(g_object_new(OSC_PLOT_TYPE, NULL));
	OSC_PLOT(plot)->priv->ctx = ctx;
	OSC_PLOT(plot)->priv->preferences = pref;
	create_plot(OSC_PLOT(plot));

	return plot;
}

void osc_plot_destroy (OscPlot *plot)
{
	g_object_unref(plot->priv->provider);
	gtk_widget_destroy(plot->priv->window);
	gtk_widget_destroy(GTK_WIDGET(plot));
}

void osc_plot_reset_numbering (void)
{
	object_count = 0;
}

void osc_plot_set_visible (OscPlot *plot, bool visible)
{
	gtk_widget_set_visible(plot->priv->window, visible);
}

struct iio_buffer * osc_plot_get_buffer(OscPlot *plot)
{
	struct extra_dev_info *dev_info;

	dev_info = iio_device_get_data(plot->priv->current_device);
	return dev_info->buffer;
}

void osc_plot_data_update (OscPlot *plot)
{
	if (call_all_transform_functions(plot->priv))
		plot->priv->redraw = TRUE;

	if (plot->priv->single_shot_mode) {
		plot->priv->single_shot_mode = false;
		gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(plot->priv->capture_button), false);
	}
}

static bool is_frequency_transform(OscPlotPrivate *priv)
{
	return priv->active_transform_type == FFT_TRANSFORM ||
	       priv->active_transform_type == COMPLEX_FFT_TRANSFORM ||
	       priv->active_transform_type == FREQ_SPECTRUM_TRANSFORM;
}

void osc_plot_update_rx_lbl(OscPlot *plot, bool initial_update)
{
	OscPlotPrivate *priv = plot->priv;
	TrList *tr_list = priv->transform_list;
	struct extra_dev_info *dev_info = NULL;
	char buf[20];
	double corr;
	int i;

	device_rx_info_update(priv);

	/* Skip rescaling graphs, updating labels and others if the redrawing is currently halted. */
	if (priv->redraw_function <= 0 && !initial_update)
		return;

	if (is_frequency_transform(priv)) {
		gfloat top, bottom, left, right;
		gfloat padding;

		/* In FFT mode we need to scale the x-axis according to the selected sampling frequency */
		for (i = 0; i < tr_list->size; i++) {
			if(!initial_update)
				Transform_setup(tr_list->transforms[i]);
			gtk_databox_graph_set_hide(tr_list->transforms[i]->graph, TRUE);
		}

		dev_info = iio_device_get_data(transform_get_device_parent(tr_list->transforms[i - 1]));
		sprintf(buf, "%cHz", dev_info->adc_scale);
		gtk_label_set_text(GTK_LABEL(priv->hor_scale), buf);

		if (priv->active_transform_type == COMPLEX_FFT_TRANSFORM)
			corr = dev_info->adc_freq / 2.0;
		else
			corr = 0;
		if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enable_auto_scale)) && !initial_update)
			return;
		if (priv->profile_loaded_scale)
			return;

		update_grid(plot, -corr, dev_info->adc_freq / 2.0);
		padding = (dev_info->adc_freq / 2.0 + corr) * 0.05;
		gtk_databox_get_total_limits(GTK_DATABOX(priv->databox), &left, &right,
				&top, &bottom);
		gtk_databox_set_total_limits(GTK_DATABOX(priv->databox),
				-corr - padding, dev_info->adc_freq / 2.0 + padding,
				top, bottom);
	} else {
		switch (gtk_combo_box_get_active(GTK_COMBO_BOX(priv->hor_units))) {
		case 0:
			gtk_label_set_text(GTK_LABEL(priv->hor_scale), "Samples");
			break;
		case 1:
			gtk_label_set_text(GTK_LABEL(priv->hor_scale), "µs");
			break;
		}
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
		priv->fps = 0.0;
		gettimeofday(&(priv->last_update), NULL);
		capture_start(priv);
	}
}

bool osc_plot_running_state (OscPlot *plot)
{
	return !!plot->priv->redraw_function;
}

void osc_plot_draw_start (OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	gtk_toggle_tool_button_set_active((GtkToggleToolButton *)priv->capture_button, TRUE);
}

void osc_plot_draw_stop (OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	gtk_toggle_tool_button_set_active((GtkToggleToolButton *)priv->capture_button, FALSE);
}

void osc_plot_save_to_ini (OscPlot *plot, char *filename)
{
	plot_profile_save(plot, filename);
}

void osc_plot_save_as (OscPlot *plot, char *filename, int type)
{
	save_as(plot, filename, type);
}

const char * osc_plot_get_active_device (OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean next_iter;
	gboolean active;
	struct iio_device *dev;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->channel_list_view));
	next_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (next_iter) {
		gtk_tree_model_get(model, &iter, ELEMENT_REFERENCE, &dev, DEVICE_ACTIVE, &active, -1);
		if (active)
			return get_iio_device_label_or_name(dev);
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
	set_marker_labels(plot, NULL, mtype);
}

void * osc_plot_get_markers_copy(OscPlot *plot)
{
	return plot->priv->markers_copy;
}

void osc_plot_set_markers_copy (OscPlot *plot, void *value)
{
	plot->priv->markers_copy = value;
}

void osc_plot_set_domain (OscPlot *plot, int domain)
{
	OscPlotPrivate *priv = plot->priv;

	if (gtk_toggle_tool_button_get_active((GtkToggleToolButton *)priv->capture_button))
		return;

	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_domain), domain);
}

int osc_plot_get_plot_domain (OscPlot *plot)
{
	return gtk_combo_box_get_active(GTK_COMBO_BOX(plot->priv->plot_domain));
}

GMutex * osc_plot_get_marker_lock (OscPlot *plot)
{
	return &plot->priv->g_marker_copy_lock;
}

bool osc_plot_set_sample_count (OscPlot *plot, gdouble count)
{
	OscPlotPrivate *priv = plot->priv;
	int ret;

	if (gtk_toggle_tool_button_get_active((GtkToggleToolButton *)priv->capture_button))
		return false;

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain)) == FFT_PLOT ||
			gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain)) == SPECTRUM_PLOT) {
		char s_count[32];
		snprintf(s_count, sizeof(s_count), "%d", (int)count);
		ret = comboboxtext_set_active_by_string(GTK_COMBO_BOX(priv->fft_size_widget), s_count);
		priv->sample_count = (int)count;
	} else {
		switch (gtk_combo_box_get_active(GTK_COMBO_BOX(priv->hor_units))) {
		case 0:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->sample_count_widget), count);
			priv->sample_count = (int)count;
			break;
		case 1:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->sample_count_widget), count);
			break;
		}
		ret = 1;
	}

	return (ret) ? true : false;
}

double osc_plot_get_sample_count (OscPlot *plot) {

	OscPlotPrivate *priv = plot->priv;
	int count;

	if (gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain)) == FFT_PLOT ||
			gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain)) == SPECTRUM_PLOT)
		count = comboboxtext_get_active_text_as_int(GTK_COMBO_BOX_TEXT(priv->fft_size_widget));
	else
		count = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->sample_count_widget));

	return count;
}

void osc_plot_set_channel_state(OscPlot *plot, const char *dev, unsigned int channel, bool state)
{
	OscPlotPrivate *priv;
	struct iio_context *ctx;
	struct iio_device *iio_dev;
	struct iio_channel *iio_ch;

	if (!plot || !dev)
		return;

	priv = plot->priv;
	if (!priv)
		return;

	ctx = priv->ctx;
	if (!ctx)
		return;

	if (gtk_toggle_tool_button_get_active((GtkToggleToolButton *)priv->capture_button))
		return;

	iio_dev = iio_context_find_device(ctx, dev);
	if (!iio_dev || !is_input_device(iio_dev))
		return;

	if (channel >= iio_device_get_channels_count(iio_dev))
		return;

	iio_ch = iio_device_get_channel(iio_dev, channel);

	if (!iio_ch)
		return;

	GtkTreeView *tree = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeModel *model = gtk_tree_view_get_model(tree);
	GtkTreeIter ch_iter;
	const char *ch;

	ch = iio_channel_get_id(iio_ch);
	get_iter_by_name(tree, &ch_iter, dev, ch);

	set_channel_state_in_tree_model(model, &ch_iter, state);
	check_valid_setup(plot);
}

void osc_plot_xcorr_revert (OscPlot *plot, int revert)
{
	TrList *tr_list = plot->priv->transform_list;
	Transform *transform;
	int i;

	for (i = 0; i < tr_list->size; i++) {
		transform = tr_list->transforms[i];
		XCORR_SETTINGS(transform)->revert_xcorr = revert;
	}
}

void osc_plot_set_quit_callback(OscPlot *plot,
	void (*qcallback)(void *user_data), void *user_data)
{
	g_return_if_fail(plot);
	g_return_if_fail(qcallback);

	plot->priv->quit_callback = qcallback;
	plot->priv->qcb_user_data = user_data;
}

int osc_plot_get_id(OscPlot *plot)
{
	return plot->priv->object_id;
}

void osc_plot_set_id(OscPlot *plot, int id)
{
	plot->priv->object_id = id;
}

void osc_plot_spect_mode(OscPlot *plot, bool enable)
{
	OscPlotPrivate *priv = plot->priv;
	GtkComboBox *cbox = GTK_COMBO_BOX(priv->plot_domain);

	g_return_if_fail(plot);

	if (enable) {
		if (!comboboxtext_set_active_by_string(cbox, "Spectrum Mode"))
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cbox),
				"Spectrum Mode");
		gtk_widget_hide(priv->capture_button);
		gtk_widget_hide(priv->ss_button);
	} else {
		GtkTreeIter iter;
		GtkTreeModel *model;

		model = gtk_combo_box_get_model(cbox);
		if (gtk_tree_model_get_iter_from_string(model, &iter,
							"Spectrum Mode")) {
			gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
		}
		gtk_widget_show(priv->capture_button);
		gtk_widget_show(priv->ss_button);
	}
}

void osc_plot_spect_set_start_f(OscPlot *plot, double freq_mhz)
{
	g_return_if_fail(plot);

	plot->priv->start_freq = freq_mhz;
}

void osc_plot_spect_set_len(OscPlot *plot, unsigned fft_count)
{
	g_return_if_fail(plot);

	plot->priv->fft_count = fft_count;
}

void osc_plot_spect_set_filter_bw(OscPlot *plot, double bw)
{
	g_return_if_fail(plot);

	plot->priv->filter_bw = bw;
}

static void osc_plot_dispose(GObject *object)
{
	G_OBJECT_CLASS(osc_plot_parent_class)->dispose(object);
}

static void osc_plot_finalize(GObject *object)
{
	G_OBJECT_CLASS(osc_plot_parent_class)->finalize(object);
}

/* Ref:
 *    A Family of Cosine-Sum Windows for High-Resolution Measurements
 *    Hans-Helge Albrecht
 *    Physikalisch-Technische Bendesanstalt
 *   Acoustics, Speech, and Signal Processing, 2001. Proceedings. (ICASSP '01).
 *   2001 IEEE International Conference on   (Volume:5 )
 *   pgs. 3081-3084
 *
 * While this doesn't use any of his code - I did find the coeffients that were nicely
 * typed in by Joe Henning as part of his MATLAB Window Utilities
 * (https://www.mathworks.com/matlabcentral/fileexchange/46092-window-utilities)
 *
 */
static double window_function(gchar *win, int j, int n)
{
	/* Strings need to match what is in glade */
	if (!g_strcmp0(win, "Hanning")) {
		double a = 2.0 * M_PI / (n - 1);
		return 0.5 * (1.0 - cos(a * j));
	} else if (!g_strcmp0(win, "Boxcar")) {
		return 1.0;
	} else if (!g_strcmp0(win, "Triangular")) {
		double a = fabs(j - (n - 1)/ 2.0) / ((n - 1.0) / 2.0);
		return 1.0 - a;
	} else if (!g_strcmp0(win, "Welch")) {
		double a = (j - (n - 1.0) / 2.0) / ((n - 1.0) / 2.0);
		return 1.0 - (a * a);
	} else if (!g_strcmp0(win, "Cosine")) {
		double a = M_PI * j / (n - 1);
		return sin(a);
	} else if (!g_strcmp0(win, "Hamming")) {
		double a0 = 0.5383553946707251, a1 = .4616446053292749;
		return a0 - a1 * cos(j * 2.0 * M_PI / (n - 1));
	} else if (!g_strcmp0(win, "Exact Blackman")) {
		/* https://ieeexplore.ieee.org/document/940309 */
		double a0 = 7938.0/18608.0, a1 = 9240.0/18608.0, a2 = 1430.0/18608.0;
		double a = j * 2.0 * M_PI / (n - 1);
		return a0 - a1 * cos(a) + a2 * cos(2.0 * a);
	} else if (!g_strcmp0(win, "3 Term Cosine")) {
		double a0 = 4.243800934609435e-1, a1 = 4.973406350967378e-1, a2 = 7.827927144231873e-2;
		double a = j * 2.0 * M_PI / (n - 1);
		return a0 - a1 * cos(a) + a2 * cos(2.0 * a);
	} else if (!g_strcmp0(win, "4 Term Cosine")) {
		double a0 = 3.635819267707608e-1, a1 = 4.891774371450171e-1, a2 = 1.365995139786921e-1,
		       a3 = 1.064112210553003e-2;
		double a = j * 2.0 * M_PI / (n - 1);
		return a0 - a1 * cos(a) + a2 * cos(2.0 * a) - a3 * cos(3.0 * a);
	} else if (!g_strcmp0(win, "5 Term Cosine")) {
		double a0 = 3.232153788877343e-1, a1 = 4.714921439576260e-1, a2 = 1.755341299601972e-1,
		       a3 = 2.849699010614994e-2, a4 = 1.261357088292677e-3;
		double a = j * 2.0 * M_PI / (n - 1);
		return a0 - a1 * cos(a) + a2 * cos(2.0 * a) - a3 * cos(3.0 * a) + a4 * cos(4.0 * a);
	} else if (!g_strcmp0(win, "6 Term Cosine")) {
		double a0 = 2.935578950102797e-1, a1 = 4.519357723474506e-1, a2 = 2.014164714263962e-1,
		       a3 = 4.792610922105837e-2, a4 = 5.026196426859393e-3, a5 = 1.375555679558877e-4;
		double a = j * 2.0 * M_PI / (n - 1);
		return a0 - a1 * cos(1.0 * a) + a2 * cos(2.0 * a) - a3 * cos(3.0 * a) + a4 * cos(4.0 * a) -
			    a5 * cos(5.0 * a);
	} else if (!g_strcmp0(win, "7 Term Cosine")) {
		double a0 = 2.712203605850388e-1, a1 = 4.334446123274422e-1, a2 = 2.180041228929303e-1,
		       a3 = 6.578534329560609e-2, a4 = 1.076186730534183e-2, a5 = 7.700127105808265e-4,
		       a6 = 1.368088305992921e-5;
		double a = j * 2.0 * M_PI / (n - 1);
		return a0 - a1 * cos(1.0 * a) + a2 * cos(2.0 * a) - a3 * cos(3.0 * a) + a4 * cos(4.0 * a) -
			    a5 * cos(5.0 * a) + a6 * cos(6.0 * a);
	} else if (!g_strcmp0(win, "Blackman-Harris")) {
		double a0 = 3.58750287312166e-1, a1 = 4.88290107472600e-1, a2 = 1.41279712970519e-1,
		       a3 = 1.16798922447150e-2;
		double a = j * 2.0 * M_PI / (n - 1);
		return a0 - a1 * cos(a) + a2 * cos(2.0 * a) - a3 * cos(3.0 * a);
	} else if (!g_strcmp0(win, "Flat Top")) {
		double a0 = 2.1557895e-1, a1 = 4.1663158e-1, a2 = 2.77263158e-1,
		       a3 = 8.3578947e-2, a4 = 6.947368e-3;
		double a = j * 2.0 * M_PI / (n - 1);
		return a0 - a1 * cos(a) + a2 * cos(2.0 * a) - a3 * cos(3.0 * a) + a4 * cos(4.0 * a);
	}

	printf("unknown window function\n");
	return 0;
}

/* This equalized power, so full scale is always 0dBFS */
static double window_function_offset(gchar *win)
{
	/* Strings need to match what is in glade */
	if (!g_strcmp0(win, "Hanning")) {
		return 1.77;
	} else if (!g_strcmp0(win, "Boxcar")) {
		return -4.25;
	} else if (!g_strcmp0(win, "Triangular")) {
		return 1.77;
	} else if (!g_strcmp0(win, "Welch")) {
		return -0.73;
	} else if (!g_strcmp0(win, "Cosine")) {
		return -0.33;
	} else if (!g_strcmp0(win, "Hamming")) {
		return 1.13;
	} else if (!g_strcmp0(win, "Exact Blackman")) {
		return 3.15;
	} else if (!g_strcmp0(win, "3 Term Cosine")) {
		return 3.19;
	} else if (!g_strcmp0(win, "4 Term Cosine")) {
		return 4.54;
	} else if (!g_strcmp0(win, "5 Term Cosine")) {
		return 5.56;
	} else if (!g_strcmp0(win, "6 Term Cosine")) {
		return 6.39;
	} else if (!g_strcmp0(win, "7 Term Cosine")) {
		return 7.08;
	} else if (!g_strcmp0(win, "Blackman-Harris")) {
		return 4.65;
	} else if (!g_strcmp0(win, "Flat Top")) {
		return 9.08;
	}
	printf("missed\n");
	return 0;
}

static void do_fft(Transform *tr)
{
	struct _fft_settings *settings = tr->settings;
	struct _fft_alg_data *fft = &settings->fft_alg_data;
	struct marker_type *markers = settings->markers;
	enum marker_types marker_type = MARKER_OFF;
	gfloat *in_data = settings->real_source;
	gfloat *in_data_c;
	gfloat *out_data = tr->y_axis;
	gfloat *X = tr->x_axis;
	int fft_size = settings->fft_size;
	int i, j, k;
	int cnt;
	gfloat mag;
	double avg, pwr_offset;
	int maxX[MAX_MARKERS + 1];
	gfloat maxY[MAX_MARKERS + 1];

	if (settings->marker_type)
		marker_type = *((enum marker_types *)settings->marker_type);

	if ((fft->cached_fft_size == -1) || (fft->cached_fft_size != fft_size) ||
		(fft->cached_num_active_channels != fft->num_active_channels)) {

		if (fft->cached_fft_size != -1) {
			fftw_destroy_plan(fft->plan_forward);
			fftw_free(fft->win);
			fftw_free(fft->out);
			if (fft->in != NULL)
				fftw_free(fft->in);
			if (fft->in_c != NULL)
				fftw_free(fft->in_c);
			fft->in_c = NULL;
			fft->in = NULL;
		}

		fft->win = fftw_malloc(sizeof(double) * fft_size);
		if (fft->num_active_channels == 2) {
			fft->m = fft_size;
			fft->in_c = fftw_malloc(sizeof(fftw_complex) * fft_size);
			fft->in = NULL;
			fft->out = fftw_malloc(sizeof(fftw_complex) * (fft->m + 1));
			fft->plan_forward = fftw_plan_dft_1d(fft_size, fft->in_c, fft->out, FFTW_FORWARD, FFTW_ESTIMATE);
		} else {
			fft->m = fft_size / 2;
			fft->out = fftw_malloc(sizeof(fftw_complex) * (fft->m + 1));
			fft->in_c = NULL;
			fft->in = fftw_malloc(sizeof(double) * fft_size);
			fft->plan_forward = fftw_plan_dft_r2c_1d(fft_size, fft->in, fft->out, FFTW_ESTIMATE);
		}

		for (i = 0; i < fft_size; i ++)
			fft->win[i] = window_function(settings->fft_win, i, fft_size);

		fft->cached_fft_size = fft_size;
		fft->cached_num_active_channels = fft->num_active_channels;
	}

	if (fft->num_active_channels == 2) {
		in_data_c = settings->imag_source;
		for (cnt = 0, i = 0; cnt < fft_size; cnt++) {
			/* normalization and scaling see fft_corr */
			fft->in_c[cnt] = in_data[i] * fft->win[cnt] + I * in_data_c[i] * fft->win[cnt];
			i++;
		}
	} else {
		for (cnt = 0, i = 0; i < fft_size; i++) {
			/* normalization and scaling see fft_corr */
			fft->in[cnt] = in_data[i] * fft->win[cnt];
			cnt++;
		}
	}

	fftw_execute(fft->plan_forward);
	avg = (double)settings->fft_avg;
	if (avg && avg != 128 )
		avg = 1.0f / avg;
	if(settings->window_correction)
	        pwr_offset = settings->fft_pwr_off + window_function_offset(settings->fft_win);
	else
	        pwr_offset = settings->fft_pwr_off;

	for (j = 0; j <= MAX_MARKERS; j++) {
		maxX[j] = 0;
		maxY[j] = -200.0f;
	}

	for (i = 0; i < fft->m; ++i) {
		if (fft->num_active_channels == 2) {
			if (i < (fft->m / 2))
				j = i + (fft->m / 2);
			else
				j = i - (fft->m / 2);
		} else {
				j = i;
		}

		if (creal(fft->out[j]) == 0 && cimag(fft->out[j]) == 0)
			fft->out[j] = FLT_MIN + I * FLT_MIN;

		mag = 10 * log10((creal(fft->out[j]) * creal(fft->out[j]) +
				cimag(fft->out[j]) * cimag(fft->out[j])) / ((unsigned long long)fft->m * fft->m)) +
			fft->fft_corr + pwr_offset;
		/* it's better for performance to have separate loops,
		 * rather than do these tests inside the loop, but it makes
		 * the code harder to understand... Oh well...
		 ***/
		if (out_data[i] == FLT_MAX) {
			/* Don't average the first iteration */
			 out_data[i] = mag;
		} else if (!avg) {
			/* keep peaks */
			if (out_data[i] <= mag)
				out_data[i] = mag;
		} else if (avg == 128) {
			/* keep min */
			if (out_data[i] >= mag)
				out_data[i] = mag;
		} else {
			/* do an average */
			out_data[i] = ((1 - avg) * out_data[i]) + (avg * mag);
		}
		if (!settings->markers || i < 2)
			continue;
		if (MAX_MARKERS && (marker_type == MARKER_PEAK ||
				marker_type == MARKER_ONE_TONE ||
				marker_type == MARKER_IMAGE)) {
			if (i <= 2) {
				maxX[0] = 0;
				maxY[0] = out_data[0];
			} else {
				for (j = 0; j <= MAX_MARKERS && markers[j].active; j++) {
					if  ((out_data[i - 1] > maxY[j]) &&
						((!((out_data[i - 2] > out_data[i - 1]) &&
						 (out_data[i - 1] > out_data[i]))) &&
						 (!((out_data[i - 2] < out_data[i - 1]) &&
						 (out_data[i - 1] < out_data[i]))))) {
						if (marker_type == MARKER_PEAK) {
							for (k = MAX_MARKERS; k > j; k--) {
								maxY[k] = maxY[k - 1];
								maxX[k] = maxX[k - 1];
							}
						}
						maxY[j] = out_data[i - 1];
						maxX[j] = i - 1;
						break;
					}
				}
			}
		}
	}

	if (!settings->markers)
		return;

	int m = fft->m;

	if ((marker_type == MARKER_ONE_TONE || marker_type == MARKER_IMAGE) &&
		((fft->num_active_channels == 1 && maxX[0] == 0) ||
		(fft->num_active_channels == 2 && maxX[0] == m/2))) {
		unsigned int max_tmp;

		max_tmp = maxX[1];
		maxX[1] = maxX[0];
		maxX[0] = max_tmp;
	}

	if (MAX_MARKERS && marker_type != MARKER_OFF) {
		for (j = 0; j <= MAX_MARKERS && markers[j].active; j++) {
			if (marker_type == MARKER_PEAK) {
				markers[j].x = (gfloat)X[maxX[j]];
				markers[j].y = (gfloat)out_data[maxX[j]];
				markers[j].bin = maxX[j];
			} else if (marker_type == MARKER_FIXED) {
				markers[j].x = (gfloat)X[markers[j].bin];
				markers[j].y = (gfloat)out_data[markers[j].bin];
			} else if (marker_type == MARKER_ONE_TONE) {
				/* assume peak is the tone */
				if (j == 0) {
					markers[j].bin = maxX[j];
					i = 1;
				} else if (j == 1) {
					/* keep DC */
					if (tr->type_id == COMPLEX_FFT_TRANSFORM)
						markers[j].bin = m / 2;
					else
						markers[j].bin = 0;
				} else {
					/* where should the spurs be? */
					i++;
					if (tr->type_id == COMPLEX_FFT_TRANSFORM) {
						markers[j].bin = markers[0].bin * i;
						if (i % 2 == 0)
							markers[j].bin += m / 2;
						markers[j].bin %= m;
					} else {
						markers[j].bin = (markers[0].bin * i) % (2 * m);
						/* Mirror the even Nyquist zones */
						if (markers[j].bin > m)
							markers[j].bin = 2 * m - markers[j].bin;
					}
				}
				/* make sure we don't need to nudge things one way or the other */
				k = markers[j].bin;
				while (out_data[k] < out_data[k + 1]) {
					k++;
				}

				while (markers[j].bin != 0 &&
						out_data[markers[j].bin] < out_data[markers[j].bin - 1]) {
					markers[j].bin--;
				}

				if (out_data[k] > out_data[markers[j].bin])
					markers[j].bin = k;

				markers[j].x = (gfloat)X[markers[j].bin];
				markers[j].y = (gfloat)out_data[markers[j].bin];
			} else if (marker_type == MARKER_IMAGE) {
				/* keep DC, fundamental, and image
				 * num_active_channels always needs to be 2 for images */
				if (j == 0) {
					/* Fundamental */
					markers[j].bin = maxX[j];
				} else if (j == 1) {
					/* DC */
					markers[j].bin = m / 2;
				} else if (j == 2) {
					/* Image */
					markers[j].bin = m / 2 - (markers[0].bin - m/2);
				} else
					continue;
				markers[j].x = (gfloat)X[markers[j].bin];
				markers[j].y = (gfloat)out_data[markers[j].bin];

			}
			if (fft->num_active_channels == 2) {
				markers[j].vector = I * settings->imag_source[markers[j].bin] +
					in_data[markers[j].bin];
			} else {
				markers[j].vector = 0 + I * 0;
			}
		}
		if (settings->markers_copy && *settings->markers_copy) {
			memcpy(*settings->markers_copy, settings->markers,
				sizeof(struct marker_type) * MAX_MARKERS);
			*settings->markers_copy = NULL;
			g_mutex_unlock(settings->marker_lock);
		}
	}
}

static void do_fft_for_spectrum(Transform *tr)
{
	struct _freq_spectrum_settings *settings = tr->settings;
	struct _fft_alg_data *fft = &settings->ffts_alg_data[settings->fft_index];
	struct marker_type *markers = settings->markers;
	enum marker_types marker_type = MARKER_OFF;
	int fft_clip_size = settings->fft_upper_clipping_limit -
				settings->fft_lower_clipping_limit;
	gfloat *in_data = settings->real_source;
	gfloat *in_data_c = settings->imag_source;
	gfloat *out_data = tr->y_axis + (settings->fft_index * fft_clip_size);
	int fft_size = settings->fft_size;
	int i, j, k, m;
	int cnt;
	gfloat mag;
	double avg, pwr_offset;
	unsigned int *maxX = settings->maxXaxis;
	gfloat *maxY = settings->maxYaxis;

	if (settings->marker_type)
		marker_type = *((enum marker_types *)settings->marker_type);

	if ((fft->cached_fft_size == -1) || (fft->cached_fft_size != fft_size) ||
		(fft->cached_num_active_channels != fft->num_active_channels)) {

		if (fft->cached_fft_size != -1) {
			fftw_destroy_plan(fft->plan_forward);
			fftw_free(fft->win);
			fftw_free(fft->out);
			if (fft->in != NULL)
				fftw_free(fft->in);
			if (fft->in_c != NULL)
				fftw_free(fft->in_c);
			fft->in_c = NULL;
			fft->in = NULL;
		}

		fft->win = fftw_malloc(sizeof(double) * fft_size);
		fft->m = fft_size;
		fft->in_c = fftw_malloc(sizeof(fftw_complex) * fft_size);
		fft->in = NULL;
		fft->out = fftw_malloc(sizeof(fftw_complex) * (fft->m + 1));
		fft->plan_forward = fftw_plan_dft_1d(fft_size, fft->in_c, fft->out, FFTW_FORWARD, FFTW_ESTIMATE);

		for (i = 0; i < fft_size; i ++)
			fft->win[i] = window_function(settings->fft_win, i, fft_size);

		fft->cached_fft_size = fft_size;
		fft->cached_num_active_channels = fft->num_active_channels;
	}

	for (cnt = 0, i = 0; cnt < fft_size; cnt++) {
		/* normalization and scaling see fft_corr */
		fft->in_c[cnt] = in_data[i] * fft->win[cnt] + I * in_data_c[i] * fft->win[cnt];
		i++;
	}

	fftw_execute(fft->plan_forward);
	avg = (double)settings->fft_avg;
	if (avg && avg != 128 )
		avg = 1.0f / avg;

	if(settings->window_correction)
	         pwr_offset = settings->fft_pwr_off + window_function_offset(settings->fft_win);
	else
                 pwr_offset = settings->fft_pwr_off;

	for (i = 0, k = 0; i < fft->m; ++i) {
		if ((unsigned)i < settings->fft_lower_clipping_limit || (unsigned)i >= settings->fft_upper_clipping_limit)
			continue;
		if (i < (fft->m / 2))
			j = i + (fft->m / 2);
		else
			j = i - (fft->m / 2);

		if (creal(fft->out[j]) == 0 && cimag(fft->out[j]) == 0)
			fft->out[j] = FLT_MIN + I * FLT_MIN;

		mag = 10 * log10((creal(fft->out[j]) * creal(fft->out[j]) +
				cimag(fft->out[j]) * cimag(fft->out[j])) / ((unsigned long long)fft->m * fft->m)) +
			settings->fft_corr + pwr_offset;
		/* it's better for performance to have separate loops,
		 * rather than do these tests inside the loop, but it makes
		 * the code harder to understand... Oh well...
		 ***/
		if (out_data[k] == FLT_MAX) {
			/* Don't average the first iteration */
			 out_data[k] = mag;
		} else if (!avg) {
			/* keep peaks */
			if (out_data[k] <= mag)
				out_data[k] = mag;
		} else if (avg == 128) {
			/* keep min */
			if (out_data[k] >= mag)
				out_data[k] = mag;
		} else {
			/* do an average */
			out_data[k] = ((1 - avg) * out_data[k]) + (avg * mag);
		}

		if (MAX_MARKERS && marker_type == MARKER_PEAK) {
			if (settings->fft_index == 0 && k <= 2) {
				maxX[0] = 0;
				maxY[0] = out_data[0];
			} else {
				for (j = 0; j <= MAX_MARKERS && markers[j].active; j++) {
					if  ((*(out_data + k - 1) > maxY[j]) &&
						((!((*(out_data + k - 2) > *(out_data + k - 1)) &&
						 (*(out_data + k - 1) > *(out_data + k)))) &&
						 (!((*(out_data + k - 2) < *(out_data + k - 1)) &&
						 (*(out_data + k - 1) < *(out_data + k)))))) {

						for (m = MAX_MARKERS; m > j; m--) {
							maxY[m] = maxY[m - 1];
							maxX[m] = maxX[m - 1];
						}
						maxY[j] = *(out_data + k - 1);
						maxX[j] = k + (settings->fft_index * fft_clip_size) - 1;
						break;
					}
				}
			}
		}

		k++;
	}
}

/* sections of the xcorr function are borrowed (under the GPL) from
 * http://blog.dmaggot.org/2010/06/cross-correlation-using-fftw3/
 * which is copyright 2010 David E. Narváez
 */
static void xcorr(fftw_complex *signala, fftw_complex *signalb, fftw_complex *result, int N, double avg)
{
	fftw_complex * signala_ext = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * (2 * N - 1));
	fftw_complex * signalb_ext = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * (2 * N - 1));
	fftw_complex * outa = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * (2 * N - 1));
	fftw_complex * outb = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * (2 * N - 1));
	fftw_complex * out = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * (2 * N - 1));
	fftw_complex scale;
	fftw_complex *cross;

	int i;
	double peak_a = 0.0, peak_b = 0.0;

	if (!signala_ext || !signalb_ext || !outa || !outb || !out)
		return;

	if (avg > 1)
		cross = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * (2 * N));
	else
		cross = result;

	fftw_plan pa = fftw_plan_dft_1d(2 * N - 1, signala_ext, outa, FFTW_FORWARD, FFTW_ESTIMATE);
	fftw_plan pb = fftw_plan_dft_1d(2 * N - 1, signalb_ext, outb, FFTW_FORWARD, FFTW_ESTIMATE);
	fftw_plan px = fftw_plan_dft_1d(2 * N - 1, out, cross, FFTW_BACKWARD, FFTW_ESTIMATE);

	//zeropadding
	memset(signala_ext, 0, sizeof(fftw_complex) * (N - 1));
	memcpy(signala_ext + (N - 1), signala, sizeof(fftw_complex) * N);
	memcpy(signalb_ext, signalb, sizeof(fftw_complex) * N);
	memset(signalb_ext + N, 0, sizeof(fftw_complex) * (N - 1));

	/* find the peaks of the time domain, for normalization */
	for (i = 0; i < N; i++) {
		if (peak_a < cabs(signala[i]))
			peak_a = cabs(signala[i]);

		if (peak_b < cabs(signalb[i]))
			peak_b = cabs(signalb[i]);
	}

	/* Move the two signals into the fourier domain */
	fftw_execute(pa);
	fftw_execute(pb);

	/* Compute the dot product, and scale them */
	scale = (2 * N -1) * peak_a * peak_b * 2;
	for (i = 0; i < 2 * N - 1; i++)
		out[i] = outa[i] * conj(outb[i]) / scale;

	/* Inverse FFT on the dot product */
	fftw_execute(px);

	fftw_destroy_plan(pa);
	fftw_destroy_plan(pb);
	fftw_destroy_plan(px);

	fftw_free(signala_ext);
	fftw_free(signalb_ext);
	fftw_free(out);
	fftw_free(outa);
	fftw_free(outb);

	if(avg > 1) {
		if (result[0] == FLT_MAX) {
			for (i = 0; i < 2 * N -1; i++)
				result[i] = cross[i];
		} else {
			for (i = 0; i < 2 * N -1; i++)
				result[i] = (result[i] * (avg - 1) + cross[i]) / avg;
		}
		fftw_free(cross);
	}

	fftw_cleanup();

	return;
}

bool time_transform_function(Transform *tr, gboolean init_transform)
{
	struct _time_settings *settings = tr->settings;
	unsigned axis_length = settings->num_samples;
	gfloat *in_data;
	unsigned int i;

	if (init_transform) {

		/* Set the sources of the transfrom */
		settings->data_source = plot_channels_get_nth_data_ref(tr->plot_channels, 0);

		/* Initialize axis */
		Transform_resize_x_axis(tr, axis_length);
		for (i = 0; i < axis_length; i++) {
			if (settings->max_x_axis && settings->max_x_axis != 0)
				tr->x_axis[i] = (gfloat)(i * settings->max_x_axis)/axis_length;
			else
				tr->x_axis[i] = i;
		}
		tr->y_axis_size = axis_length;

		if (settings->apply_inverse_funct ||
				settings->apply_multiply_funct ||
				settings->apply_add_funct) {
			Transform_resize_y_axis(tr, tr->y_axis_size);
		} else {
			tr->y_axis = settings->data_source;
		}

		return true;
	}

	if (tr->plot_channels_type == PLOT_MATH_CHANNEL) {
		PlotMathChn *m = tr->plot_channels->data;
		m->math_expression(m->iio_channels_data,
			m->data_ref, settings->num_samples);
	} else if (tr->plot_channels_type == PLOT_IIO_CHANNEL) {
		if (!settings->apply_inverse_funct &&
				!settings->apply_multiply_funct &&
				!settings->apply_add_funct)
			return true;

		in_data = plot_channels_get_nth_data_ref(tr->plot_channels, 0);
		if (!in_data)
			return false;

		for (i = 0; i < tr->y_axis_size; i++) {
			if (settings->apply_inverse_funct) {
				if (in_data[i] != 0)
					tr->y_axis[i] = 1 / in_data[i];
				else
					tr->y_axis[i] = 65535;
			} else {
				tr->y_axis[i] = in_data[i];
			}
			if (settings->apply_multiply_funct)
				tr->y_axis[i] *= settings->multiply_value;
			if (settings->apply_add_funct)
				tr->y_axis[i] += settings->add_value;
		}
	}

	return true;
}

bool cross_correlation_transform_function(Transform *tr, gboolean init_transform)
{
	struct _cross_correlation_settings *settings = tr->settings;
	unsigned axis_length = settings->num_samples;
	gfloat *i_0, *q_0;
	gfloat *i_1, *q_1;
	unsigned int i;

	if (init_transform) {
		/* Set the sources of the transfrom */
		settings->i0_source = plot_channels_get_nth_data_ref(tr->plot_channels, 0);
		settings->q0_source = plot_channels_get_nth_data_ref(tr->plot_channels, 1);
		settings->i1_source = plot_channels_get_nth_data_ref(tr->plot_channels, 2);
		settings->q1_source = plot_channels_get_nth_data_ref(tr->plot_channels, 3);

		/* Initialize axis */
		if (settings->signal_a) {
			fftw_free(settings->signal_a);
			settings->signal_a = NULL;
		}
		if (settings->signal_b) {
			fftw_free(settings->signal_b);
			settings->signal_b = NULL;
		}
		if (settings->xcorr_data) {
			fftw_free(settings->xcorr_data);
			settings->xcorr_data = NULL;
		}
		settings->signal_a = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * axis_length);
		settings->signal_b = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * axis_length);
		settings->xcorr_data = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * axis_length * 2);
		settings->xcorr_data[0] = FLT_MAX;

		Transform_resize_x_axis(tr, 2 * axis_length);
		Transform_resize_y_axis(tr, 2 * axis_length);
		for (i = 0; i < 2 * axis_length - 1; i++) {
			tr->x_axis[i] = ((i - (gfloat)axis_length + 1) * settings->max_x_axis)/(gfloat)axis_length;
			tr->y_axis[i] = 0;
		}
		tr->y_axis_size = 2 * axis_length - 1;

		return true;
	}

	GSList *node;

	if (tr->plot_channels_type == PLOT_MATH_CHANNEL)
		for (node = tr->plot_channels; node; node = g_slist_next(node)) {
			PlotMathChn *m = node->data;
			m->math_expression(m->iio_channels_data,
				m->data_ref, settings->num_samples);
		}

	i_0 = settings->i0_source;
	q_0 = settings->q0_source;
	i_1 = settings->i1_source;
	q_1 = settings->q1_source;

	for (i = 0; i < axis_length; i++) {
		settings->signal_a[i] = q_0[i] + I * i_0[i];
		settings->signal_b[i] = q_1[i] + I * i_1[i];
	}

	if (settings->revert_xcorr)
		xcorr(settings->signal_b, settings->signal_a, settings->xcorr_data, axis_length, (double)settings->avg);
	else
		xcorr(settings->signal_a, settings->signal_b, settings->xcorr_data, axis_length, (double)settings->avg);

	gfloat *out_data = tr->y_axis;
	gfloat *X = tr->x_axis;
	struct marker_type *markers = settings->markers;
	enum marker_types marker_type = MARKER_OFF;
	unsigned int maxX[MAX_MARKERS + 1];
	gfloat maxY[MAX_MARKERS + 1];
	int j, k;

	if (settings->marker_type)
		marker_type = *((enum marker_types *)settings->marker_type);

	for (j = 0; j <= MAX_MARKERS; j++) {
		maxX[j] = 0;
		maxY[j] = -200.0f;
	}

	/* find the peaks */
	for (i = 0; i < 2 * axis_length - 1; i++) {
		tr->y_axis[i] =  2 * creal(settings->xcorr_data[i]) / (gfloat)axis_length;
		if (!settings->markers)
			continue;

		if (MAX_MARKERS && marker_type == MARKER_PEAK) {
			if (i <= 2) {
				maxX[0] = 0;
				maxY[0] = out_data[0];
			} else {
				for (j = 0; j <= MAX_MARKERS && markers[j].active; j++) {
					if  ((fabs(out_data[i - 1]) > maxY[j]) &&
						((!((out_data[i - 2] > out_data[i - 1]) &&
						 (out_data[i - 1] > out_data[i]))) &&
						 (!((out_data[i - 2] < out_data[i - 1]) &&
						 (out_data[i - 1] < out_data[i]))))) {
						if (marker_type == MARKER_PEAK) {
							for (k = MAX_MARKERS; k > j; k--) {
								maxY[k] = maxY[k - 1];
								maxX[k] = maxX[k - 1];
							}
						}
						maxY[j] = fabs(out_data[i - 1]);
						maxX[j] = i - 1;
						break;
					}
				}
			}
		}
	}

	if (!settings->markers)
		return true;

	/* now we know where the peaks are, we estimate the actual peaks,
	 * by quadratic interpolation of existing spectral peaks, which is explained:
	 * https://ccrma.stanford.edu/~jos/sasp/Quadratic_Interpolation_Spectral_Peaks.html
	 * written by Julius Orion Smith III.
	 */
	if (MAX_MARKERS && marker_type != MARKER_OFF) {
		for (j = 0; j <= MAX_MARKERS && markers[j].active; j++)
			if (marker_type == MARKER_PEAK) {
				/* If we don't have the alpha or the gamma peaks, we can't continue */
				if (maxX[j] < 1 || maxX[j] > 2 * axis_length - 1) {
					markers[j].x = 0;
					markers[j].y = 0;
					continue;
				}
				/* sync'ed with the pictures in the url above:
				 * alpha = (gfloat)out_data[maxX[j] - 1];
				 * gamma = (gfloat)out_data[maxX[j] + 1];
				 * beta  = (gfloat)out_data[maxX[j]];
				 */
				markers[j].x = (gfloat)((out_data[maxX[j] - 1] - out_data[maxX[j] + 1]) /
						(2 * (out_data[maxX[j] - 1] - 2 * out_data[maxX[j]] +
						out_data[maxX[j] + 1])));
				markers[j].y = (gfloat)(out_data[maxX[j]] - (out_data[maxX[j] - 1] - out_data[maxX[j] + 1]) *
						markers[j].x / 4);
				markers[j].x += (gfloat)X[maxX[j]];
				markers[j].bin = maxX[j];
			}
		if (settings->markers_copy && *settings->markers_copy) {
			memcpy(*settings->markers_copy, settings->markers,
				sizeof(struct marker_type) * MAX_MARKERS);
			*settings->markers_copy = NULL;
			g_mutex_unlock(settings->marker_lock);
		}
	}

	return true;
}

bool freq_spectrum_transform_function(Transform *tr, gboolean init_transform)
{
	struct iio_channel *chn;
	struct _freq_spectrum_settings *settings = tr->settings;
	unsigned i, j, k, axis_length, fft_size, bits_used;
	int ret;
	double sampling_freq;
	bool complete_transform = false;

	if (init_transform) {
		fft_size = settings->fft_size;
		chn = PLOT_IIO_CHN(tr->plot_channels->data)->iio_chn;
		ret = iio_channel_attr_read_double(chn, "sampling_frequency",
				&sampling_freq);
		if (ret < 0)
			return false;
		sampling_freq /= 1000000; /* Hz to MHz*/

		bits_used = iio_channel_get_data_format(chn)->bits;

		if (!bits_used)
			return false;

		/* Compute FFT normalization and scaling offset */
		settings->fft_corr = 20 * log10(2.0 / (1ULL << (bits_used - 1)));

		settings->fft_lower_clipping_limit = (fft_size / 2) - (settings->filter_bandwidth * fft_size) / (2 * sampling_freq);
		settings->fft_upper_clipping_limit = (fft_size / 2) + (settings->filter_bandwidth * fft_size) / (2 * sampling_freq);

		settings->real_source = plot_channels_get_nth_data_ref(tr->plot_channels, 0);
		settings->imag_source = plot_channels_get_nth_data_ref(tr->plot_channels, 1);

		axis_length = (settings->fft_upper_clipping_limit - settings->fft_lower_clipping_limit) * settings->fft_count;
		Transform_resize_x_axis(tr, axis_length);
		Transform_resize_y_axis(tr, axis_length);

		for (i = 0, k = 0; i < settings->fft_count; i++) {
			for (j = 0; j < fft_size; j++) {
				if (j >= settings->fft_lower_clipping_limit && j < settings->fft_upper_clipping_limit) {
					tr->x_axis[k] = (j * sampling_freq / settings->fft_size - sampling_freq / 2) + settings->freq_sweep_start + (settings->filter_bandwidth) * i;
					tr->y_axis[k] = FLT_MAX;
					k++;
				}
			}
		}

		for (i = 0; i <= MAX_MARKERS; i++) {
			settings->maxXaxis[i] = 0;
			settings->maxYaxis[i] = -200.0f;
		}

		return true;
	}

	do_fft_for_spectrum(tr);
	settings->fft_index++;

	if (settings->fft_index == settings->fft_count) {
		settings->fft_index = 0;
		complete_transform = true;

		if (MAX_MARKERS && *settings->marker_type != MARKER_OFF) {
			for (j = 0; j <= MAX_MARKERS && settings->markers[j].active; j++)
				if (*settings->marker_type == MARKER_PEAK) {
					settings->markers[j].x = (gfloat)tr->x_axis[settings->maxXaxis[j]];
					settings->markers[j].y = (gfloat)tr->y_axis[settings->maxXaxis[j]];
					settings->markers[j].bin = settings->maxXaxis[j];
				}
			if (*settings->markers_copy) {
				memcpy(*settings->markers_copy, settings->markers,
					sizeof(struct marker_type) * MAX_MARKERS);
				*settings->markers_copy = NULL;
				g_mutex_unlock(settings->marker_lock);
			}
		}

		for (i = 0; i <= MAX_MARKERS; i++) {
			settings->maxXaxis[i] = 0;
			settings->maxYaxis[i] = -200.0f;
		}
	}

	return complete_transform;
}

bool fft_transform_function(Transform *tr, gboolean init_transform)
{
	struct iio_device *dev;
	struct extra_dev_info *dev_info;
	struct _fft_settings *settings = tr->settings;
	unsigned num_samples;
	int axis_length;
	unsigned int bits_used;
	double corr;
	int i;

	if (init_transform) {
		/* Set the sources of the transfrom */
		settings->real_source = plot_channels_get_nth_data_ref(tr->plot_channels, 0);
		if (g_slist_length(tr->plot_channels) > 1)
			settings->imag_source = plot_channels_get_nth_data_ref(tr->plot_channels, 1);

		/* Initialize axis */
		dev = transform_get_device_parent(tr);
		if (!dev)
			return false;
		dev_info = iio_device_get_data(dev);
		num_samples = dev_info->sample_count;
		if (dev_info->channel_trigger_enabled)
			num_samples /= 2;

		PlotChn *chn = (PlotChn *)tr->plot_channels->data;
		struct iio_channel *iio_chn = NULL;

		bits_used = 0;
		if (chn->type == PLOT_IIO_CHANNEL) {
			iio_chn = PLOT_IIO_CHN(chn)->iio_chn;
		} else if (PLOT_CHN(chn)->type == PLOT_MATH_CHANNEL) {
			if (PLOT_MATH_CHN(chn)->iio_channels) {
				iio_chn = (struct iio_channel *)
						PLOT_MATH_CHN(chn)->iio_channels->data;
			}
		}
		if (iio_chn)
			bits_used = iio_channel_get_data_format(iio_chn)->bits;

		if (!bits_used)
			return false;
		axis_length = settings->fft_size * settings->fft_alg_data.num_active_channels / 2;
		Transform_resize_x_axis(tr, axis_length);
		Transform_resize_y_axis(tr, axis_length);
		tr->y_axis_size = axis_length;
		if (settings->fft_alg_data.num_active_channels == 2)
			corr = dev_info->adc_freq / 2.0;
		else
			corr = 0;
		for (i = 0; i < axis_length; i++) {
			tr->x_axis[i] = i * dev_info->adc_freq / num_samples - corr;
			tr->y_axis[i] = FLT_MAX;
		}

		/* Compute FFT normalization and scaling offset */
		settings->fft_alg_data.fft_corr = 20 * log10(2.0 / (1ULL << (bits_used - 1)));

		/* Make sure that previous positions of markers are not out of bonds */
		if (settings->markers)
			for (i = 0; i <= MAX_MARKERS; i++)
				if (settings->markers[i].bin >= axis_length)
					settings->markers[i].bin = 0;

		return true;
	}

	GSList *node;

	if (tr->plot_channels_type == PLOT_MATH_CHANNEL)
		for (node = tr->plot_channels; node; node = g_slist_next(node)) {
			PlotMathChn *m = node->data;
			m->math_expression(m->iio_channels_data,
				m->data_ref, settings->fft_size);
		}
	do_fft(tr);

	return true;
}

bool constellation_transform_function(Transform *tr, gboolean init_transform)
{
	struct _constellation_settings *settings = tr->settings;
	unsigned axis_length = settings->num_samples;

	if (init_transform) {
		/* Set the sources of the transfrom */
		settings->x_source = plot_channels_get_nth_data_ref(tr->plot_channels, 0);
		settings->y_source = plot_channels_get_nth_data_ref(tr->plot_channels, 1);

		/* Initialize axis */
		tr->x_axis_size = axis_length;
		tr->y_axis_size = axis_length;
		tr->x_axis = settings->x_source;
		tr->y_axis = settings->y_source;

		return true;
	}

	GSList *node;

	if (tr->plot_channels_type == PLOT_MATH_CHANNEL)
		for (node = tr->plot_channels; node; node = g_slist_next(node)) {
			PlotMathChn *m = node->data;
			m->math_expression(m->iio_channels_data,
				m->data_ref, settings->num_samples);
		}

	return true;
}


/* Plot iio channel definitions */

static gfloat* plot_iio_channel_get_data_ref(PlotChn *obj);
static void plot_iio_channel_assert_channels(PlotChn *obj, bool assert);
static void plot_iio_channel_destroy(PlotChn *obj);

static PlotIioChn * plot_iio_channel_new(struct iio_context *ctx, struct iio_device *dev)
{
	PlotIioChn *obj;

	obj = calloc(1, sizeof(PlotIioChn));
	if (!obj) {
		fprintf(stderr, "Error in %s: %s", __func__, strerror(errno));
		return NULL;
	}

	obj->base.type = PLOT_IIO_CHANNEL;
	obj->base.dev = dev;
	obj->base.ctx = ctx;
	obj->base.get_data_ref = *plot_iio_channel_get_data_ref;
	obj->base.assert_used_iio_channels = *plot_iio_channel_assert_channels;
	obj->base.destroy = *plot_iio_channel_destroy;

	return obj;
}

static gfloat* plot_iio_channel_get_data_ref(PlotChn *obj)
{
	PlotIioChn *this = (PlotIioChn *)obj;
	gfloat *ref = NULL;

	if (this && this->iio_chn) {
		struct extra_info *ch_info;

		ch_info = iio_channel_get_data(this->iio_chn);
		if (ch_info)
			ref = ch_info->data_ref;
	}

	return ref;
}

static void plot_iio_channel_assert_channels(PlotChn *obj, bool assert)
{
	PlotIioChn *this = (PlotIioChn *)obj;

	if (this && this->iio_chn)
		set_channel_shadow_of_enabled(this->iio_chn,
				(gpointer)assert);
}

static void plot_iio_channel_destroy(PlotChn *obj)
{
	PlotIioChn *this = (PlotIioChn *)obj;

	if (!this)
		return;

	if (this->base.name)
		g_free(this->base.name);

	if (this->base.parent_name)
		g_free(this->base.parent_name);

	free(this);
}

/* Plot math channel definitions */

static gfloat * plot_math_channel_get_data_ref(PlotChn *obj);
static void plot_math_channel_assert_channels(PlotChn *obj, bool assert);
static void plot_math_channel_destroy(PlotChn *obj);

static PlotMathChn * plot_math_channel_new(struct iio_context *ctx)
{
	PlotMathChn *obj;

	obj = calloc(1, sizeof(PlotMathChn));
	if (!obj) {
		fprintf(stderr, "Error in %s: %s", __func__, strerror(errno));
		return NULL;
	}

	obj->base.type = PLOT_MATH_CHANNEL;
	obj->base.ctx = ctx;
	obj->base.get_data_ref = *plot_math_channel_get_data_ref;
	obj->base.assert_used_iio_channels = *plot_math_channel_assert_channels;
	obj->base.destroy = *plot_math_channel_destroy;

	return obj;
}

static gfloat * plot_math_channel_get_data_ref(PlotChn *obj)
{
	PlotMathChn *this = (PlotMathChn *)obj;
	gfloat *ref = NULL;

	if (this)
		ref = this->data_ref;

	return ref;
}

static void plot_math_channel_assert_channels(PlotChn *obj, bool assert)
{
	PlotMathChn *this = (PlotMathChn *)obj;

	if (this && this->iio_channels)
		g_slist_foreach(this->iio_channels,
				set_channel_shadow_of_enabled,
				(gpointer)assert);
}

static void plot_math_channel_destroy(PlotChn *obj)
{
	PlotMathChn *this = (PlotMathChn *)obj;

	if (!this)
		return;

	if (this->base.name)
		g_free(this->base.name);

	if (this->base.parent_name)
		g_free(this->base.parent_name);

	if (this->iio_channels)
		g_slist_free(this->iio_channels);

	if (this->iio_channels_data)
		free(this->iio_channels_data);

	if (this->iio_device_name)
		g_free(this->iio_device_name);

	if (this->txt_math_expression)
		g_free(this->txt_math_expression);

	math_expression_close_lib_handler(this->math_lib_handler);

	free(this);
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

static void foreach_channel_iter_of_device(GtkTreeView *treeview, const char *name,
	void (*tree_iter_func)(GtkTreeModel *model, GtkTreeIter *data, void *user_data),
	void *user_data)
{
	GtkTreeIter iter;
	GtkTreeIter child_iter;
	GtkTreeModel *model;
	gboolean next_iter = true;
	gboolean next_child_iter;
	char *str_device;

	model = gtk_tree_view_get_model(treeview);
	if (!gtk_tree_model_get_iter_first(model, &iter))
		return;

	do {
		gtk_tree_model_get(model, &iter, ELEMENT_NAME, &str_device, -1);
		if (!strcmp(name, str_device))
			break;
		next_iter = gtk_tree_model_iter_next(model, &iter);
	} while (next_iter);
	if (!next_iter)
		return;

	next_child_iter = gtk_tree_model_iter_children(model, &child_iter, &iter);
	while (next_child_iter) {
		tree_iter_func(model, &child_iter, user_data);
		next_child_iter = gtk_tree_model_iter_next(model, &child_iter);
	}
}

static void set_may_be_enabled_bit(GtkTreeModel *model,
	GtkTreeIter *iter, void *user_data)
{
	gboolean enabled;
	struct iio_channel *chn;
	struct extra_info *info;

	gtk_tree_model_get(model, iter, ELEMENT_REFERENCE, &chn,
			CHANNEL_ACTIVE, &enabled, -1);
	info = iio_channel_get_data(chn);
	info->may_be_enabled = enabled;
}

static gboolean check_valid_setup_of_device(OscPlot *plot, const char *name)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	int plot_type;
	int num_enabled;
	struct iio_device *dev;
	unsigned int nb_channels = num_of_channels_of_device(treeview, name);
	unsigned enabled_channels_mask;

	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean device_enabled;

	plot_type = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain));

	model = gtk_tree_view_get_model(treeview);
	get_iter_by_name(treeview, &iter, name, NULL);
	gtk_tree_model_get(model, &iter,
			ELEMENT_REFERENCE, &dev,
			DEVICE_ACTIVE, &device_enabled, -1);
	if (!device_enabled && plot_type != TIME_PLOT)
		return true;

	num_enabled = enabled_channels_of_device(treeview, name, &enabled_channels_mask);

	/* Basic validation rules */
	if (plot_type == FFT_PLOT) {
		if (num_enabled != 4 && num_enabled != 2 && num_enabled != 1) {
			gtk_widget_set_tooltip_text(priv->capture_button,
				"FFT needs 4 or 2 or less channels");
			return false;
		}
	} else if (plot_type == XY_PLOT) {
		if (num_enabled != 2) {
			gtk_widget_set_tooltip_text(priv->capture_button,
				"Constellation requires only 2 channels");
			return false;
		}
	} else if (plot_type == TIME_PLOT) {
		if (enabled_channels_count(plot) == 0) {
			gtk_widget_set_tooltip_text(priv->capture_button,
				"Time Domain needs at least one channel");
			return false;
		} else if (dev && !dma_valid_selection(name, enabled_channels_mask | global_enabled_channels_mask(dev), nb_channels)) {
			gtk_widget_set_tooltip_text(priv->capture_button,
				"Channel selection not supported");
			return false;
		}
	} else if (plot_type == XCORR_PLOT) {
		if (enabled_channels_count(plot) != 4) {
			gtk_widget_set_tooltip_text(priv->capture_button,
				"Correlation requires 4 channels");
			return false;
		}
	}

	/* No additional checking is needed for non iio devices */
	if (!dev)
		return TRUE;

	char warning_text[100];

	/* Check if devices that need a trigger have one and it's configured */
	const struct iio_device *trigger;
	int ret;

	ret = iio_device_get_trigger(dev, &trigger);
	if (ret == 0 && trigger == NULL && num_enabled > 0) {
		snprintf(warning_text, sizeof(warning_text),
				"Device %s needs an impulse generator", name);
		gtk_widget_set_tooltip_text(priv->capture_button, warning_text);
		return false;
	}
	/* Additional validation rules provided by the plugin of the device */
	if (num_enabled != 2 || plot_type == TIME_PLOT)
		return true;

	bool valid_comb;
	const char *ch_names[2];

	plugin_setup_validation_fct = find_setup_check_fct_by_devname(name);
	if (plugin_setup_validation_fct) {
		foreach_channel_iter_of_device(GTK_TREE_VIEW(priv->channel_list_view),
			name, *set_may_be_enabled_bit, NULL);
		valid_comb = (*plugin_setup_validation_fct)(dev, ch_names);
		if (!valid_comb) {
			snprintf(warning_text, sizeof(warning_text),
				"Combination between %s and %s is invalid", ch_names[0], ch_names[1]);
			gtk_widget_set_tooltip_text(priv->capture_button, warning_text);
			return false;
		}
	}

	if (num_enabled && plot_type == FFT_PLOT && !gtk_toggle_tool_button_get_active((GtkToggleToolButton *)priv->capture_button)) {
		GtkListStore *liststore;
		int i, j, k = 0, m = 0;
		char buf[256];

		j = comboboxtext_get_active_text_as_int(GTK_COMBO_BOX_TEXT(priv->fft_size_widget));
		liststore = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(priv->fft_size_widget)));
		gtk_list_store_clear(liststore);

		i = 4194304;
		/* make sure we don't exceed DMA, 2^22 bytes (not samples) */
		while (i >= 64) {
			if (i * num_enabled * 2 <= 4194304) {
				sprintf(buf, "%i", i);
				gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(priv->fft_size_widget), buf);

				if (i == j)
					m = k;
				k++;
			}
			i = i / 2;
		}
		gtk_combo_box_set_active(GTK_COMBO_BOX(priv->fft_size_widget), m);
	}

	return true;
}

static gboolean check_valid_setup_of_all_devices(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean next_iter;
	gchar *dev_name;
	gboolean valid;

	model = gtk_tree_view_get_model(treeview);
	next_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (next_iter) {
		gtk_tree_model_get(model, &iter, ELEMENT_NAME, &dev_name, -1);
		valid = check_valid_setup_of_device(plot, dev_name);
		g_free(dev_name);
		if (!valid)
			return false;
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}

	return true;
}

static gboolean check_valid_setup(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	if (!check_valid_setup_of_all_devices(plot))
		goto capture_button_err;

	if (gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(priv->capture_button)))
		g_object_set(priv->capture_button, "stock-id", "gtk-stop", NULL);
	else
		g_object_set(priv->capture_button, "stock-id", "gtk-media-play", NULL);
	gtk_widget_set_tooltip_text(priv->capture_button, "Capture / Stop");

	g_object_set(priv->ss_button, "stock-id", "gtk-media-next", NULL);
	gtk_widget_set_tooltip_text(priv->ss_button, "Single Shot Capture");

	if (!priv->capture_button_hid) {
		priv->capture_button_hid = g_signal_connect(priv->capture_button, "toggled",
			G_CALLBACK(capture_button_clicked_cb), plot);
		priv->deactivate_capture_btn_flag = 0;
	}

	return true;

capture_button_err:
	g_object_set(priv->capture_button, "stock-id", "gtk-dialog-warning", NULL);
	g_object_set(priv->ss_button, "stock-id", "gtk-dialog-warning", NULL);
	if (priv->capture_button_hid) {
		g_signal_handler_disconnect(priv->capture_button, priv->capture_button_hid);
		priv->deactivate_capture_btn_flag = 1;
	}
	priv->capture_button_hid = 0;

	return false;
}

static bool plot_channel_check_name_exists(OscPlot *plot, const char *name,
		PlotChn *struct_to_skip)
{
	OscPlotPrivate *priv = plot->priv;
	GSList *node;
	PlotChn *settings;
	bool name_exists;

	name_exists = false;
	for (node = priv->ch_settings_list; node; node = g_slist_next(node)) {
		settings = node->data;
		if (settings == struct_to_skip)
			continue;
		if (!settings->name) {
			fprintf(stderr, "Error in %s: Channel name is null", __func__);
			continue;
		}
		if (!strcmp(name, settings->name)) {
			name_exists = true;
			break;
		}
	}

	return name_exists;
}

static int plot_get_sample_count_of_device(OscPlot *plot, const char *device)
{
	OscPlotPrivate *priv;
	struct iio_context *ctx;
	struct iio_device *iio_dev;
	struct extra_dev_info *dev_info;
	gdouble freq;
	int count = -1;

	if (!plot || !device)
		return count;

	priv = plot->priv;
	if (!priv)
		return count;

	ctx = priv->ctx;
	if (!ctx)
		return count;

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(priv->hor_units))) {
	case 0:
		count = (int)osc_plot_get_sample_count(plot);
		break;
	case 1:
		iio_dev = iio_context_find_device(ctx, device);
		if (!iio_dev)
			break;

		dev_info = iio_device_get_data(iio_dev);
		if (!dev_info)
			break;

		freq = dev_info->adc_freq * prefix2scale(dev_info->adc_scale);
		count = (int)round((gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->sample_count_widget)) *
				freq) / pow(10.0, 6));
		break;
	}

	return count;
}

static int plot_get_sample_count_for_transform(OscPlot *plot, Transform *transform)
{
	OscPlotPrivate *priv = plot->priv;
	struct iio_device *iio_dev = transform_get_device_parent(transform);

	if (!iio_dev)
		iio_dev = priv->current_device;

	return plot_get_sample_count_of_device(plot, get_iio_device_label_or_name(iio_dev));
}

static void notebook_info_set_page_visibility(GtkNotebook *nb, int page, bool visbl)
{
	GtkWidget *wpage = gtk_notebook_get_nth_page(nb, page);
	gtk_widget_set_visible(wpage, visbl);
}

static struct iio_device * transform_get_device_parent(Transform *transform)
{
	if (!transform || !transform->plot_channels || !transform->plot_channels->data)
		return NULL;

	return ((PlotChn *)transform->plot_channels->data)->dev;
}

static void update_transform_settings(OscPlot *plot, Transform *transform)
{
	OscPlotPrivate *priv = plot->priv;
	unsigned i;
	int plot_type;

	plot_type = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain));
	if (plot_type == FFT_PLOT) {
		FFT_SETTINGS(transform)->fft_size = comboboxtext_get_active_text_as_int(GTK_COMBO_BOX_TEXT(priv->fft_size_widget));
		FFT_SETTINGS(transform)->fft_win = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->fft_win_widget));
		FFT_SETTINGS(transform)->window_correction = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->fft_win_correction));
		FFT_SETTINGS(transform)->fft_avg = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->fft_avg_widget));
		FFT_SETTINGS(transform)->fft_pwr_off = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->fft_pwr_offset_widget));
		FFT_SETTINGS(transform)->fft_alg_data.cached_fft_size = -1;
		FFT_SETTINGS(transform)->fft_alg_data.cached_num_active_channels = -1;
		FFT_SETTINGS(transform)->fft_alg_data.num_active_channels = g_slist_length(transform->plot_channels);
		FFT_SETTINGS(transform)->markers = NULL;
		FFT_SETTINGS(transform)->markers_copy = NULL;
		FFT_SETTINGS(transform)->marker_lock = NULL;
		FFT_SETTINGS(transform)->marker_type = NULL;
	} else if (plot_type == TIME_PLOT) {
		int dev_samples = plot_get_sample_count_for_transform(plot, transform);
		if (dev_samples < 0)
			return;

		TIME_SETTINGS(transform)->num_samples = dev_samples;
		if (PLOT_CHN(transform->plot_channels->data)->type == PLOT_IIO_CHANNEL) {
			PlotIioChn *set;

			set = (PlotIioChn *)transform->plot_channels->data;
			TIME_SETTINGS(transform)->apply_inverse_funct = set->apply_inverse_funct;
			TIME_SETTINGS(transform)->apply_multiply_funct = set->apply_multiply_funct;
			TIME_SETTINGS(transform)->apply_add_funct = set->apply_add_funct;
			TIME_SETTINGS(transform)->multiply_value = set->multiply_value;
			TIME_SETTINGS(transform)->add_value = set->add_value;
			TIME_SETTINGS(transform)->max_x_axis = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->sample_count_widget));
		}
	} else if (plot_type == XY_PLOT){
		CONSTELLATION_SETTINGS(transform)->num_samples = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->sample_count_widget));
	} else if (plot_type == XCORR_PLOT){
		int dev_samples = plot_get_sample_count_for_transform(plot, transform);
		if (dev_samples < 0)
			return;

		XCORR_SETTINGS(transform)->num_samples = dev_samples;
		XCORR_SETTINGS(transform)->avg = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->fft_avg_widget));
		XCORR_SETTINGS(transform)->revert_xcorr = 0;
		XCORR_SETTINGS(transform)->signal_a = NULL;
		XCORR_SETTINGS(transform)->signal_b = NULL;
		XCORR_SETTINGS(transform)->xcorr_data = NULL;
		XCORR_SETTINGS(transform)->markers = NULL;
		XCORR_SETTINGS(transform)->markers_copy = NULL;
		XCORR_SETTINGS(transform)->marker_lock = NULL;
		XCORR_SETTINGS(transform)->marker_type = NULL;
		XCORR_SETTINGS(transform)->max_x_axis = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->sample_count_widget));
	} else if (plot_type == SPECTRUM_PLOT) {
		FREQ_SPECTRUM_SETTINGS(transform)->ffts_alg_data = calloc(priv->fft_count, sizeof(struct _fft_alg_data));
		FREQ_SPECTRUM_SETTINGS(transform)->fft_count = priv->fft_count;
		FREQ_SPECTRUM_SETTINGS(transform)->freq_sweep_start = priv->start_freq + priv->filter_bw / 2;
		FREQ_SPECTRUM_SETTINGS(transform)->filter_bandwidth = priv->filter_bw;
		FREQ_SPECTRUM_SETTINGS(transform)->fft_size = comboboxtext_get_active_text_as_int(GTK_COMBO_BOX_TEXT(priv->fft_size_widget));
		FREQ_SPECTRUM_SETTINGS(transform)->fft_win = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->fft_win_widget));
		FREQ_SPECTRUM_SETTINGS(transform)->window_correction = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->fft_win_correction));
		FREQ_SPECTRUM_SETTINGS(transform)->fft_avg = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->fft_avg_widget));
		FREQ_SPECTRUM_SETTINGS(transform)->fft_pwr_off = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->fft_pwr_offset_widget));
		FREQ_SPECTRUM_SETTINGS(transform)->maxXaxis = malloc(sizeof(unsigned int) * (MAX_MARKERS + 1));
		FREQ_SPECTRUM_SETTINGS(transform)->maxYaxis = malloc(sizeof(unsigned int) * (MAX_MARKERS + 1));
		for (i = 0; i < priv->fft_count; i++) {
			FREQ_SPECTRUM_SETTINGS(transform)->ffts_alg_data[i].cached_fft_size = -1;
			FREQ_SPECTRUM_SETTINGS(transform)->ffts_alg_data[i].cached_num_active_channels = -1;
			FREQ_SPECTRUM_SETTINGS(transform)->ffts_alg_data[i].num_active_channels = g_slist_length(transform->plot_channels);
		}
	}
}

static Transform* add_transform_to_list(OscPlot *plot, int tr_type, GSList *channels)
{
	OscPlotPrivate *priv = plot->priv;
	Transform *transform;
	struct _time_settings *time_settings;
	struct _fft_settings *fft_settings;
	struct _constellation_settings *constellation_settings;
	struct _cross_correlation_settings *xcross_settings;
	struct _freq_spectrum_settings *freq_spectrum_settings;
	GSList *node;

	bool window_correction = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(plot->priv->fft_win_correction));

	transform = Transform_new(tr_type);
	transform->graph_color = &color_graph[priv->transform_list->size];
	transform->plot_channels = g_slist_copy(channels);
	transform->plot_channels_type = PLOT_CHN(channels->data)->type;


	/* Enable iio channels used by the transform */
	for (node = channels; node; node = g_slist_next(node)) {
		PlotChn *plot_ch;

		plot_ch = node->data;
		if (plot_ch)
			plot_ch->assert_used_iio_channels(plot_ch, true);
	}

	switch (tr_type) {
	case TIME_TRANSFORM:
		Transform_attach_function(transform, time_transform_function);
		time_settings = (struct _time_settings *)calloc(1, sizeof(struct _time_settings));
		Transform_attach_settings(transform, time_settings);
		transform->graph_color = &PLOT_CHN(channels->data)->graph_color;
		break;
	case FFT_TRANSFORM:
		Transform_attach_function(transform, fft_transform_function);
		fft_settings = (struct _fft_settings *)calloc(1, sizeof(struct _fft_settings));
		fft_settings->window_correction = window_correction;
		Transform_attach_settings(transform, fft_settings);
		break;
	case CONSTELLATION_TRANSFORM:
		Transform_attach_function(transform, constellation_transform_function);
		constellation_settings = (struct _constellation_settings *)calloc(1, sizeof(struct _constellation_settings));
		Transform_attach_settings(transform, constellation_settings);
		break;
	case COMPLEX_FFT_TRANSFORM:
		Transform_attach_function(transform, fft_transform_function);
		fft_settings = (struct _fft_settings *)calloc(1, sizeof(struct _fft_settings));
		fft_settings->window_correction = window_correction;
		Transform_attach_settings(transform, fft_settings);
		break;
	case CROSS_CORRELATION_TRANSFORM:
		Transform_attach_function(transform, cross_correlation_transform_function);
		xcross_settings = (struct _cross_correlation_settings *)calloc(1, sizeof(struct _cross_correlation_settings));
		Transform_attach_settings(transform, xcross_settings);
		break;
	case FREQ_SPECTRUM_TRANSFORM:
		Transform_attach_function(transform, freq_spectrum_transform_function);
		freq_spectrum_settings = (struct _freq_spectrum_settings *)calloc(1, sizeof(struct _freq_spectrum_settings));
		Transform_attach_settings(transform, freq_spectrum_settings);
		break;
	default:
		fprintf(stderr, "Invalid transform\n");
		return NULL;
	}
	TrList_add_transform(priv->transform_list, transform);
	update_transform_settings(plot, transform);

	priv->active_transform_type = tr_type;
	priv->current_device = transform_get_device_parent(transform);

	return transform;
}

static gfloat *** iio_channels_get_data(struct iio_context *ctx,
					const char *device_name)
{
	gfloat ***data;
	struct iio_device *iio_dev;
	struct iio_channel *iio_chn;
	int nb_channels;
	struct extra_info *ch_info;
	int i;

	if (!device_name)
		return NULL;

	iio_dev = iio_context_find_device(ctx, device_name);
	if (!iio_dev) {
		fprintf(stderr, "Could not find device %s in %s\n",
				device_name, __func__);
		return NULL;
	}
	nb_channels = iio_device_get_channels_count(iio_dev);
	data = malloc(sizeof(double**) * nb_channels);
	for (i = 0; i < nb_channels; i++) {
		iio_chn = iio_device_get_channel(iio_dev, i);
		ch_info = iio_channel_get_data(iio_chn);
		data[i] = &ch_info->data_ref;
	}

	return data;
}

static void set_channel_shadow_of_enabled(gpointer data, gpointer user_data)
{
	struct iio_channel *chn = data;
	struct extra_info *ch_info;

	if (!chn)
		return;

	ch_info = iio_channel_get_data(chn);
	ch_info->shadow_of_enabled += ((bool)user_data) ? 1 : -1;
}

static void remove_transform_from_list(OscPlot *plot, Transform *tr)
{
	OscPlotPrivate *priv = plot->priv;
	TrList *list = priv->transform_list;

	if (tr->has_the_marker)
		priv->tr_with_marker = NULL;

	transform_remove_own_markers(tr);
	if (tr->type_id == FREQ_SPECTRUM_TRANSFORM) {
		free(FREQ_SPECTRUM_SETTINGS(tr)->ffts_alg_data);
		free(FREQ_SPECTRUM_SETTINGS(tr)->maxXaxis);
		free(FREQ_SPECTRUM_SETTINGS(tr)->maxYaxis);
	}
	TrList_remove_transform(list, tr);
	Transform_destroy(tr);
	if (list->size == 0) {
		priv->active_transform_type = NO_TRANSFORM_TYPE;
	}
}

static void markers_init(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkDatabox *databox = GTK_DATABOX(plot->priv->databox);
	struct marker_type *markers = priv->markers;
	const char *empty_text = " ";
	char buf[10];
	int i;

	/* Clear marker information text box */
	if (priv->tbuf)
		gtk_text_buffer_set_text(priv->tbuf, empty_text, -1);

	priv->markers_copy = NULL;

	/* Don't go any further with the init when in TIME or XY domains*/
	if (priv->active_transform_type == TIME_TRANSFORM ||
			priv->active_transform_type == CONSTELLATION_TRANSFORM)
		return;

	/* Ensure that Marker Image is applied only to Complex FFT Transforms */
	if (priv->active_transform_type == FFT_TRANSFORM && priv->marker_type == MARKER_IMAGE)
		priv->marker_type = MARKER_OFF;

	for (i = 0; i <= MAX_MARKERS; i++) {
		markers[i].x = 0.0f;
		markers[i].y = 0.0f;
		if (markers[i].graph)
			g_object_unref(markers[i].graph);
		markers[i].graph = gtk_databox_markers_new(1, &markers[i].x, &markers[i].y, &color_marker,
			10, GTK_DATABOX_MARKERS_TRIANGLE);
		gtk_databox_graph_add(databox, markers[i].graph);
		gtk_databox_graph_set_hide(markers[i].graph, true);
		sprintf(buf, "?%i", i);
		gtk_databox_markers_set_label(GTK_DATABOX_MARKERS(markers[i].graph),
			0, GTK_DATABOX_MARKERS_TEXT_N, buf, FALSE);
		if (priv->marker_type == MARKER_OFF)
			gtk_databox_graph_set_hide(markers[i].graph, TRUE);
		else
			gtk_databox_graph_set_hide(markers[i].graph, !markers[i].active);
	}
	if (priv->marker_type != MARKER_OFF)
		set_marker_labels(plot, NULL, priv->marker_type);
}

static void transform_add_plot_markers(OscPlot *plot, Transform *transform)
{
	OscPlotPrivate *priv = plot->priv;

	transform->has_the_marker = true;
	priv->tr_with_marker = transform;
	if (priv->active_transform_type == FFT_TRANSFORM ||
		priv->active_transform_type == COMPLEX_FFT_TRANSFORM) {
		FFT_SETTINGS(transform)->markers = priv->markers;
		FFT_SETTINGS(transform)->markers_copy = &priv->markers_copy;
		FFT_SETTINGS(transform)->marker_type = &priv->marker_type;
		FFT_SETTINGS(transform)->marker_lock = &priv->g_marker_copy_lock;
	} else if (priv->active_transform_type == CROSS_CORRELATION_TRANSFORM) {
		XCORR_SETTINGS(transform)->markers = priv->markers;
		XCORR_SETTINGS(transform)->markers_copy = &priv->markers_copy;
		XCORR_SETTINGS(transform)->marker_type = &priv->marker_type;
		XCORR_SETTINGS(transform)->marker_lock = &priv->g_marker_copy_lock;
	} else if (priv->active_transform_type == FREQ_SPECTRUM_TRANSFORM) {
		FREQ_SPECTRUM_SETTINGS(transform)->markers = priv->markers;
		FREQ_SPECTRUM_SETTINGS(transform)->markers_copy = &priv->markers_copy;
		FREQ_SPECTRUM_SETTINGS(transform)->marker_type = &priv->marker_type;
		FREQ_SPECTRUM_SETTINGS(transform)->marker_lock = &priv->g_marker_copy_lock;
	}
}

static void transform_add_own_markers(OscPlot *plot, Transform *transform)
{
	OscPlotPrivate *priv = plot->priv;
	struct marker_type *markers;
	int i;

	markers = calloc(MAX_MARKERS + 2, sizeof(struct marker_type));
	if (!markers) {
		fprintf(stderr,
			"Error: could not alloc memory for markers in %s\n",
			__func__);
		return;
	}

	for (i = 0; i < MAX_MARKERS; i++)
		markers[i].active = (i <= 4);

	if (transform->type_id == FFT_TRANSFORM ||
		transform->type_id == COMPLEX_FFT_TRANSFORM) {
		FFT_SETTINGS(transform)->markers = markers;
		FFT_SETTINGS(transform)->marker_type = FFT_SETTINGS(
					priv->tr_with_marker)->marker_type;
	} else if (transform->type_id == CROSS_CORRELATION_TRANSFORM) {
		XCORR_SETTINGS(transform)->markers = markers;
		XCORR_SETTINGS(transform)->marker_type = XCORR_SETTINGS(
					priv->tr_with_marker)->marker_type;
	}
}

static void transform_remove_own_markers(Transform *transform)
{
	struct marker_type *markers;

	if (transform->has_the_marker)
		return;

	if (transform->type_id == FFT_TRANSFORM ||
		transform->type_id == COMPLEX_FFT_TRANSFORM) {
		markers = FFT_SETTINGS(transform)->markers;
	} else if (transform->type_id == CROSS_CORRELATION_TRANSFORM) {
		markers = XCORR_SETTINGS(transform)->markers;
	} else {
		return;
	}

	if (markers)
		free(markers);
}

static void plot_channels_update(OscPlot *plot)
{
	OscPlotPrivate *priv;
	GSList *node;
	PlotChn *ch;
	PlotMathChn *mch;
	int num_samples;

	g_return_if_fail(plot);

	priv = plot->priv;
	for (node = priv->ch_settings_list;
			node; node = g_slist_next(node)) {
		ch = node->data;
		if (ch->type != PLOT_MATH_CHANNEL)
			continue;

		mch = (PlotMathChn *)ch;
		num_samples = plot_get_sample_count_of_device(plot,
				mch->iio_device_name);
		if (num_samples < 0)
			num_samples = osc_plot_get_sample_count(plot);
		mch->data_ref = realloc(mch->data_ref,
				sizeof(gfloat) * num_samples);
	}
}

static void collect_parameters_from_plot(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	struct iio_context *ctx = priv->ctx;
	struct plot_params *prms;
	GSList *list;
	unsigned int i;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct extra_dev_info *info = iio_device_get_data(dev);
		const char *dev_name = get_iio_device_label_or_name(dev);

		if (info->input_device == false)
			continue;

		prms = malloc(sizeof(struct plot_params));
		prms->plot_id = priv->object_id;
		prms->sample_count = plot_get_sample_count_of_device(plot, dev_name);
		list = info->plots_sample_counts;
		list = g_slist_prepend(list, prms);
		info->plots_sample_counts = list;
	}
}

static void dispose_parameters_from_plot(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	struct iio_context *ctx = priv->ctx;
	struct plot_params *prms;
	GSList *node;
	GSList *del_link = NULL;
	unsigned int i;

	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct extra_dev_info *info = iio_device_get_data(dev);
		GSList *list = info->plots_sample_counts;

		if (info->input_device == false)
			continue;

		for (node = list; node; node = g_slist_next(node)) {
			prms = node->data;
			if (prms->plot_id == priv->object_id) {
				del_link = node;
				break;
			}
		}
		if (del_link) {
			list = g_slist_delete_link(list, del_link);
			info->plots_sample_counts = list;
		}
	}
}

static gdouble prefix2scale (char adc_scale)
{
	switch (adc_scale) {
		case 'M':
			return 1000000.0;
		case 'k':
			return 1000.0;
		default:
			return 1.0;
			break;
	}
}

static void markers_phase_diff_show(OscPlotPrivate *priv)
{
	static float avg[MAX_MARKERS] = {NAN};

	GtkTextIter iter;
	char text[256];
	int m;
	struct marker_type *trA_markers;
	struct marker_type *trB_markers;
	float angle_diff, lead_lag;
	float filter, angle;

	gtk_text_buffer_set_text(priv->phase_buf, "", -1);
	gtk_text_buffer_get_iter_at_line(priv->phase_buf, &iter, 1);

	if (priv->active_transform_type == COMPLEX_FFT_TRANSFORM &&
					priv->transform_list->size == 2) {
		trA_markers = FFT_SETTINGS(
				priv->transform_list->transforms[0])->markers;
		trB_markers = FFT_SETTINGS(
				priv->transform_list->transforms[1])->markers;

		filter =  gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->fft_avg_widget));
		if (!filter)
			filter = 1;
		filter = 1.0 / filter;

		if (MAX_MARKERS && priv->marker_type != MARKER_OFF) {
			for (m = 0; m < MAX_MARKERS &&
						trA_markers[m].active; m++) {

				/* find out the quadrant
				 * since carg() returns something from [-pi, +pi], use that.
				 * this handles reflex angles up to [-2*pi, +2*pi]
				 */
				lead_lag = (cargf(trA_markers[m].vector) - cargf(trB_markers[m].vector)) * 180 / M_PI;

				/* [-2*pi, +2*pi] is kind of silly
				 * move things to [-pi, +pi]
				 */
				if (lead_lag > 180)
					lead_lag = lead_lag - 360;
				if (lead_lag < -180)
					lead_lag = 360 + lead_lag;

				if (isnan(avg[m]))
					avg[m] = lead_lag;

				/* Cosine law, answers are [0, +pi] */
				if (cabsf(trA_markers[m].vector) == 0.0 || cabsf(trB_markers[m].vector) == 0.0) {
					/* divide by 0 is nan */
					angle_diff = lead_lag;
				} else {
					angle_diff =  acosf((crealf(trA_markers[m].vector) * crealf(trB_markers[m].vector) +
						cimagf(trA_markers[m].vector) * cimagf(trB_markers[m].vector)) /
						(cabsf(trA_markers[m].vector) * cabsf(trB_markers[m].vector))) * 180 / M_PI;
				}

				/* put back into the correct quadrant */
				if (lead_lag < 0)
					angle_diff *= -1.0;

				if (lead_lag > 180.0)
					angle_diff = 360.0 - angle_diff;

				avg[m] = ((1 - filter) * avg[m]) + (filter * angle_diff);

				angle = avg[m];
				if (angle > 180.0)
					angle -= 360.0;
				if (angle < -180.0)
					angle += 360.0;

				trA_markers[m].angle = angle;
				trB_markers[m].angle = angle;

				snprintf(text, sizeof(text),
					"%s: %02.3f° @ %2.3f %cHz %c",
					trA_markers[m].label,
					angle,
					/* lo_freq / markers_scale */ trA_markers[m].x,
					/*dev_info->adc_scale */ 'M',
					m != (MAX_MARKERS - 1) ? '\n' : '\0');

				gtk_text_buffer_insert(priv->phase_buf,
						&iter, text, -1);
			}
		} else {
			gtk_text_buffer_set_text(priv->phase_buf,
				"No markers active", 17);
		}
	}
}

static void draw_marker_values(OscPlotPrivate *priv, Transform *tr)
{
	struct iio_device *iio_dev;
	struct extra_dev_info *dev_info;
	struct marker_type *markers;
	GtkTextIter iter;
	char text[256];
	int markers_scale;
	double lo_freq;
	int m;

	if (tr->type_id == CROSS_CORRELATION_TRANSFORM)
		markers = XCORR_SETTINGS(tr)->markers;
	else if (tr->type_id == FREQ_SPECTRUM_TRANSFORM)
		markers = FREQ_SPECTRUM_SETTINGS(tr)->markers;
	else if(tr->type_id == FFT_TRANSFORM)
		markers = FFT_SETTINGS(tr)->markers;
	else if(tr->type_id == COMPLEX_FFT_TRANSFORM)
		markers = FFT_SETTINGS(tr)->markers;
	else
		return;

	if (priv->tbuf == NULL) {
			priv->tbuf = gtk_text_buffer_new(NULL);
			gtk_text_view_set_buffer(GTK_TEXT_VIEW(priv->marker_label), priv->tbuf);
	}
	iio_dev = transform_get_device_parent(tr);
	if (!iio_dev) {
		fprintf(stderr,
			"Error: Could not find iio device parent for the given transform.%s\n",
			__func__);
		return;
	}
	dev_info = iio_device_get_data(iio_dev);

	/* Get the LO frequency stored by a iio channel which is used by
	 * this transform. All channels should have the same lo freq. */
	lo_freq = 0.0;
	if (tr->plot_channels && g_slist_length(tr->plot_channels)) {
		PlotChn *p = PLOT_CHN(tr->plot_channels->data);
		if (p->type == PLOT_IIO_CHANNEL) {
			struct iio_channel *ch;
			struct extra_info *ch_info;
			ch = PLOT_IIO_CHN(p)->iio_chn;
			if (ch) {
				ch_info = iio_channel_get_data(ch);
				if (ch_info)
					lo_freq = ch_info->lo_freq;
			}
		}
	}

	markers_scale = prefix2scale(dev_info->adc_scale);

	if (MAX_MARKERS && priv->marker_type != MARKER_OFF) {
		for (m = 0; m <= MAX_MARKERS && markers[m].active; m++) {
			if (tr->type_id == FFT_TRANSFORM || tr->type_id == COMPLEX_FFT_TRANSFORM) {
				sprintf(text, "%s: %2.2f dBFS @ %2.3f %cHz%c",
					markers[m].label, markers[m].y,
					lo_freq / markers_scale + markers[m].x,
					dev_info->adc_scale,
					m != MAX_MARKERS ? '\n' : '\0');
			} else if (tr->type_id == CROSS_CORRELATION_TRANSFORM) {
				sprintf(text, "M%i: %1.6f @ %2.3f%c", m, markers[m].y, markers[m].x,
					m != MAX_MARKERS ? '\n' : '\0');
			} else if (tr->type_id == FREQ_SPECTRUM_TRANSFORM) {
				sprintf(text, "M%i: %1.6f @ %2.3f%c", m, markers[m].y, markers[m].x,
					m != MAX_MARKERS ? '\n' : '\0');
			}

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

static void device_rx_info_update(OscPlotPrivate *priv)
{
	GtkTextIter iter;
	char text[256];
	unsigned int i, num_devices = 0;

	gtk_text_buffer_set_text(priv->devices_buf, "", -1);
	gtk_text_buffer_get_iter_at_line(priv->devices_buf, &iter, 1);

	if (priv->ctx)
		num_devices = iio_context_get_devices_count(priv->ctx);

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(priv->ctx, i);
		const char *name = get_iio_device_label_or_name(dev);
		struct extra_dev_info *dev_info = iio_device_get_data(dev);
		double freq, percent, seconds;
		char freq_prefix, sec_prefix;

		if (dev_info->input_device == false)
			continue;

		freq = dev_info->adc_freq * prefix2scale(dev_info->adc_scale);
		freq = freq / comboboxtext_get_active_text_as_int(GTK_COMBO_BOX_TEXT(priv->fft_size_widget));;
		seconds = 1 / freq;
		percent = seconds * priv->fps * 100.0;
		if (freq > 1e6) {
			freq = freq / 1e6;
			freq_prefix = 'M';
		} else if (freq > 1e3) {
			freq = freq / 1e3;
			freq_prefix = 'k';
		} else
			freq_prefix = ' ';
		if (seconds < 1e-6) {
			seconds = seconds * 1e9;
			sec_prefix = 'n';
		} else if (seconds < 1e-3) {
			seconds = seconds * 1e6;
			sec_prefix = 'u';
		} else {
			seconds = seconds * 1e3;
			sec_prefix = 'm';
		}

		snprintf(text, sizeof(text), "%s:\n\tSampleRate: %3.2f %cSPS\n"
				"\tHz/Bin: %3.2f %cHz\n"
				"\tSweep: %3.2f %cs (%2.2f%%)\n"
				"\tFPS: %2.2f\n",
			name, dev_info->adc_freq, dev_info->adc_scale,
			freq, freq_prefix, seconds, sec_prefix, percent, priv->fps);
		gtk_text_buffer_insert(priv->devices_buf, &iter, text, -1);
	}
}

static bool call_all_transform_functions(OscPlotPrivate *priv)
{
	TrList *tr_list = priv->transform_list;
	Transform *tr;
	bool valid = true;
	bool tr_valid;
	int i = 0;

	if (priv->redraw_function <= 0)
		return false;

	for (; i < tr_list->size; i++) {
		tr = tr_list->transforms[i];
		tr_valid = Transform_update_output(tr);
		if (tr_valid)
			gtk_databox_graph_set_hide(tr->graph, FALSE);
		valid &= tr_valid;
	}

	return valid;
}

static int enabled_channels_of_device(GtkTreeView *treeview, const char *name, unsigned *enabled_mask)
{
	GtkTreeIter iter;
	GtkTreeIter child_iter;
	gboolean next_child_iter;
	char *str_device;
	gboolean enabled;
	int num_enabled = 0;
	int ch_pos = 0;

	GtkTreeModel *model = gtk_tree_view_get_model(treeview);
	gboolean next_iter = gtk_tree_model_get_iter_first(model, &iter);

	if (enabled_mask)
		*enabled_mask = 0;

	while (next_iter) {
		if (!gtk_tree_model_iter_children(model, &child_iter, &iter)) {
			next_iter = gtk_tree_model_iter_next(model, &iter);
			continue;
		}
		gtk_tree_model_get(model, &iter, ELEMENT_NAME, &str_device, -1);
		if (!strcmp(name, str_device)) {
			next_child_iter = true;
			while (next_child_iter) {
				gtk_tree_model_get(model, &child_iter, CHANNEL_ACTIVE, &enabled, -1);
				if (enabled) {
					num_enabled++;
					if (enabled_mask)
						*enabled_mask |= 1 << ch_pos;
				}
				ch_pos++;
				next_child_iter = gtk_tree_model_iter_next(model, &child_iter);
			}
		}
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}

	return num_enabled;
}

static int num_of_channels_of_device(GtkTreeView *treeview, const char *name)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean dev_exists;

	model = gtk_tree_view_get_model(treeview);
	dev_exists = get_iter_by_name(treeview, &iter, name, NULL);
	if (!dev_exists)
		return 0;

	return gtk_tree_model_iter_n_children(model, &iter);
}

static int enabled_channels_count(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean next_iter;
	gchar *dev_name;
	int count = 0;

	model = gtk_tree_view_get_model(treeview);
	next_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (next_iter) {
		gtk_tree_model_get(model, &iter, ELEMENT_NAME, &dev_name, -1);
		count += enabled_channels_of_device(treeview, dev_name, NULL);
		g_free(dev_name);
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}

	return count;
}

static gfloat * plot_channels_get_nth_data_ref(GSList *list, guint n)
{
	GSList *nth_node;
	gfloat *data = NULL;

	if (!list) {
		fprintf(stderr, "Invalid list argument.");
		goto end;
	}

	nth_node = g_slist_nth(list, n);
	if (!nth_node || !nth_node->data) {
		fprintf(stderr, "Element at index %d does not exist.", n);
		goto end;
	}

	PlotChn *plot_ch = nth_node->data;

	data = plot_ch->get_data_ref(plot_ch);

end:
	if (!data)
		fprintf(stderr, "Could not find data reference in %s\n",
				__func__);

	return data;
}

struct ch_tr_params {
	OscPlot *plot;
	int enabled_channels;
	GSList *ch_settings;
};

static void channels_transform_assignment(GtkTreeModel *model,
	GtkTreeIter *iter, void *user_data)
{
	struct ch_tr_params *prm = user_data;
	OscPlot *plot = prm->plot;
	OscPlotPrivate *priv = plot->priv;
	PlotChn *settings;
	Transform *transform = NULL;
	gboolean enabled;
	int num_added_chs;

	gtk_tree_model_get(model, iter,
			CHANNEL_ACTIVE, &enabled,
			CHANNEL_SETTINGS, &settings,
			-1);
	if (!enabled)
		return;

	prm->ch_settings = g_slist_prepend(prm->ch_settings, settings);
	num_added_chs = g_slist_length(prm->ch_settings);

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain))) {
	case TIME_PLOT:
		transform = add_transform_to_list(plot, TIME_TRANSFORM, prm->ch_settings);
		break;
	case FFT_PLOT:
		if (prm->enabled_channels == 1) {
			transform = add_transform_to_list(plot, FFT_TRANSFORM, prm->ch_settings);
		} else if ((prm->enabled_channels == 2 || prm->enabled_channels == 4) && num_added_chs == 2) {
			if (plugin_installed("FMComms6")) {
				transform = add_transform_to_list(plot, COMPLEX_FFT_TRANSFORM, prm->ch_settings);
			} else {
				prm->ch_settings = g_slist_reverse(prm->ch_settings);
				transform = add_transform_to_list(plot, COMPLEX_FFT_TRANSFORM, prm->ch_settings);
			}
		}
		break;
	case XY_PLOT:
		if (prm->enabled_channels == 2 && num_added_chs == 2) {
			prm->ch_settings = g_slist_reverse(prm->ch_settings);
			transform = add_transform_to_list(plot, CONSTELLATION_TRANSFORM, prm->ch_settings);
		}
		break;
	case XCORR_PLOT:
		if (prm->enabled_channels == 4 && num_added_chs == 4) {
			prm->ch_settings = g_slist_reverse(prm->ch_settings);
			transform = add_transform_to_list(plot, CROSS_CORRELATION_TRANSFORM, prm->ch_settings);
		}
		break;
	case SPECTRUM_PLOT:
		if (prm->enabled_channels == 2 && num_added_chs == 2) {
			prm->ch_settings = g_slist_reverse(prm->ch_settings);
			transform = add_transform_to_list(plot, FREQ_SPECTRUM_TRANSFORM, prm->ch_settings);
		}
		break;
	default:
		break;
	}
	if (transform && prm->ch_settings) {
		g_slist_free(prm->ch_settings);
		prm->ch_settings = NULL;
	}
}

static void devices_transform_assignment(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean next_iter;
	gchar *dev_name;
	struct ch_tr_params prm;

	prm.plot = plot;
	prm.enabled_channels = enabled_channels_count(plot);
	prm.ch_settings = NULL;

	model = gtk_tree_view_get_model(treeview);
	next_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (next_iter) {
		gtk_tree_model_get(model, &iter,
				ELEMENT_NAME, &dev_name, -1);
		foreach_channel_iter_of_device(treeview, dev_name,
				*channels_transform_assignment, &prm);
		g_free(dev_name);
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}
}

static void deassert_used_channels(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	Transform *tr;
	GSList *node;
	int i;

	for (i = 0; i < priv->transform_list->size; i++) {
		tr = priv->transform_list->transforms[i];
		/* Disable iio channels used by the transform */
		for (node = tr->plot_channels; node; node = g_slist_next(node)) {
			PlotChn *plot_ch;

			plot_ch = node->data;
			if (plot_ch)
				plot_ch->assert_used_iio_channels(plot_ch, false);
		}
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
	struct timeval now, diff;

	priv->frame_counter++;
	if (gettimeofday(&now, NULL) == -1) {
		printf("err with gettimeofdate()\n");
		return;
	}
	if (!priv->fps) {
		priv->last_update.tv_sec = now.tv_sec;
		priv->last_update.tv_usec = now.tv_usec;
		priv->fps = -1.0;
		priv->frame_counter = 0;
		return;
	}

	timersub(&now, &priv->last_update, &diff);

	if (diff.tv_sec >= 5 || priv->fps == -1.0) {
		double tmp =  priv->frame_counter / (diff.tv_sec + diff.tv_usec / 1000000.0);
		priv->fps = tmp;
		priv->frame_counter = 0;
		priv->last_update.tv_sec = now.tv_sec;
		priv->last_update.tv_usec = now.tv_usec;
		device_rx_info_update(priv);
	}
}

static gboolean plot_redraw(OscPlotPrivate *priv)
{
	TrList *tr_list = priv->transform_list;
	Transform *tr;
	bool show_diff_phase = false;
	int i;

	if (!GTK_IS_DATABOX(priv->databox))
		return FALSE;

	if (priv->redraw) {
			auto_scale_databox(priv, GTK_DATABOX(priv->databox));
			gtk_widget_queue_draw(priv->databox);
			fps_counter(priv);
			for (i = 0; i < tr_list->size; i++) {
				tr = tr_list->transforms[i];
				if (tr->has_the_marker) {

					show_diff_phase = true;
					draw_marker_values(priv, tr);
				}
			}
			if (show_diff_phase)
				markers_phase_diff_show(priv);
	}
	if (priv->stop_redraw == TRUE)
		priv->redraw_function = 0;

	priv->redraw = FALSE;
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
	gfloat *transform_x_axis;
	gfloat *transform_y_axis;
	unsigned int max_x_axis = 0;
	GtkDataboxGraph *graph;
	int i;

	gtk_databox_graph_remove_all(GTK_DATABOX(priv->databox));
	markers_init(plot);
	for (i = 0; i < tr_list->size; i++) {
		transform = tr_list->transforms[i];
		Transform_setup(transform);
		transform_x_axis = Transform_get_x_axis_ref(transform);
		transform_y_axis = Transform_get_y_axis_ref(transform);

		gchar *plot_type_str = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->plot_type));
		if (strcmp(plot_type_str, "Lines") &&
			!is_frequency_transform(priv)) {
			graph = gtk_databox_points_new(transform->y_axis_size,
					transform_x_axis, transform_y_axis,
					transform->graph_color, 3);
		} else {
			graph = gtk_databox_lines_new(transform->y_axis_size,
					transform_x_axis, transform_y_axis,
					transform->graph_color, priv->line_thickness);
		}
		g_free(plot_type_str);

		transform->graph = graph;

		if (transform->x_axis_size > max_x_axis)
			max_x_axis = transform->x_axis_size;

		if (is_frequency_transform(priv) ||
			priv->active_transform_type == CROSS_CORRELATION_TRANSFORM) {
			if (i == 0)
				transform_add_plot_markers(plot, transform);
			else
				transform_add_own_markers(plot, transform);
		}

		gtk_databox_graph_set_hide(graph, TRUE);
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
		else if (priv->active_transform_type == FREQ_SPECTRUM_TRANSFORM) {
			double end_freq = priv->start_freq + priv->filter_bw * priv->fft_count;
			double width = end_freq - priv->start_freq;
			gtk_databox_set_total_limits(GTK_DATABOX(priv->databox),
				priv->start_freq - 0.05 * width, end_freq + 0.05 * width,
				0.0, -100.0);
		}
	}

	osc_plot_update_rx_lbl(plot, INITIAL_UPDATE);

	bool show_phase_info = false;
	if (priv->active_transform_type == COMPLEX_FFT_TRANSFORM &&
			priv->transform_list->size == 2) {
		show_phase_info = true;
	}
	notebook_info_set_page_visibility(GTK_NOTEBOOK(
		gtk_builder_get_object(priv->builder, "notebook_info")),
		2, show_phase_info);
}

static void single_shot_clicked_cb(GtkToggleToolButton *btn, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;

	priv->single_shot_mode = true;
	gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->capture_button), true);
}

static bool comboboxtext_input_devices_fill(struct iio_context *iio_ctx, GtkComboBoxText *box)
{
	unsigned int i, num_devices = 0;

	if (!box) {
		fprintf(stderr, "Error: invalid parameters in %s\n", __func__);
		return false;
	}
	if (iio_ctx)
		num_devices = iio_context_get_devices_count(iio_ctx);

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(iio_ctx, i);
		struct extra_dev_info *dev_info = iio_device_get_data(dev);
		const char *name;

		if (dev_info->input_device == false)
			continue;

		name = get_iio_device_label_or_name(dev);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(box), name);
	}

	return true;
}

static void capture_button_clicked_cb(GtkToggleToolButton *btn, gpointer data)
{
	OscPlot *plot = data;
	OscPlotPrivate *priv = plot->priv;
	gboolean button_state;

	if (!check_valid_setup(plot))
		return;

	button_state = gtk_toggle_tool_button_get_active(btn);

	if (button_state) {
		gtk_widget_set_tooltip_text(GTK_WIDGET(btn), "Capture / Stop");
		plot_channels_update(plot);
		collect_parameters_from_plot(plot);
		remove_all_transforms(plot);
		devices_transform_assignment(plot);

		g_mutex_trylock(&priv->g_marker_copy_lock);

		g_signal_emit(plot, oscplot_signals[CAPTURE_EVENT_SIGNAL], 0, button_state);

		plot_setup(plot);

		add_grid(plot);
		gtk_widget_queue_draw(priv->databox);
		priv->frame_counter = 0;
		priv->fps = 0.0;
		gettimeofday(&(priv->last_update), NULL);
		capture_start(priv);
	} else {
		priv->stop_redraw = TRUE;
		dispose_parameters_from_plot(plot);
		deassert_used_channels(plot);

		g_mutex_trylock(&priv->g_marker_copy_lock);
		g_mutex_unlock(&priv->g_marker_copy_lock);

		g_signal_emit(plot, oscplot_signals[CAPTURE_EVENT_SIGNAL], 0, button_state);
	}
}

static void new_plot_button_clicked_cb(GtkToolButton *btn, OscPlot *plot)
{
	OscPlot *new_plot;

	new_plot = OSC_PLOT(osc_plot_new_with_pref(plot->priv->ctx, plot->priv->preferences));
	osc_plot_set_visible(new_plot, true);
	g_signal_emit(plot, oscplot_signals[NEWPLOT_EVENT_SIGNAL], 0, new_plot);
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
		struct iio_channel *chn;
		gtk_tree_model_get(model, &child, ELEMENT_REFERENCE, &chn, -1);
		struct extra_info *info = iio_channel_get_data(chn);
		if (info) {
			if (info->constraints & CONSTR_CHN_UNTOGGLEABLE) {
				next_iter = gtk_tree_model_iter_next(model, &child);
				continue;
			}
		}

		gtk_tree_store_set(GTK_TREE_STORE(model), &child, SENSITIVE, state, -1);
		if (state == false)
			gtk_tree_store_set(GTK_TREE_STORE(model), &child, CHANNEL_ACTIVE, state, -1);
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
	PlotChn *settings;

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
	check_valid_setup(plot);
}

static void channel_toggled(GtkCellRendererToggle* renderer, gchar* pathStr, gpointer plot)
{
	OscPlotPrivate *priv = ((OscPlot *)plot)->priv;
	GtkTreePath* path = gtk_tree_path_new_from_string(pathStr);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean active;

	if (!gtk_cell_renderer_get_sensitive(GTK_CELL_RENDERER(renderer)))
		return;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->channel_list_view));
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, CHANNEL_ACTIVE, &active, -1);
	active = !active;
	set_channel_state_in_tree_model(model, &iter, active);
	gtk_tree_path_free(path);

	check_valid_setup(plot);
}

static void color_icon_renderer_visibility(GtkTreeViewColumn *col,
		GtkCellRenderer *cell, GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
	gboolean is_channel;
	gint plot_type;

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

static void plot_channel_add_to_plot(OscPlot *plot, PlotChn *settings)
{
	OscPlotPrivate *priv = plot->priv;
	GSList *list = priv->ch_settings_list;
	int index = priv->nb_plot_channels;

	g_return_if_fail(settings);

	/* Set a default color */
	settings->graph_color.red = color_graph[index % NUM_GRAPH_COLORS].red;
	settings->graph_color.green = color_graph[index % NUM_GRAPH_COLORS].green;
	settings->graph_color.blue = color_graph[index % NUM_GRAPH_COLORS].blue;
	settings->graph_color.alpha = color_graph[index % NUM_GRAPH_COLORS].alpha;

	/* Add the settings to an internal list */
	list = g_slist_prepend(list, settings);
	priv->ch_settings_list = list;
	priv->nb_plot_channels++;
}

static void plot_channel_remove_from_plot(OscPlot *plot,
				PlotChn *chn)
{
	OscPlotPrivate *priv = plot->priv;
	GSList *node, *list;

	/* Remove the plot channel from the internal list */
	list = priv->ch_settings_list;
	node = g_slist_find(list, chn);
	chn->destroy(chn);
	priv->ch_settings_list = g_slist_remove_link(list, node);
	priv->nb_plot_channels--;
}

static GdkPixbuf * channel_color_icon_new(OscPlot *plot)
{
	DIR *d;

	/* Check the local icons folder first */
	d = opendir("./icons");
	if (!d) {
		return gtk_image_get_pixbuf(GTK_IMAGE(gtk_image_new_from_file(OSC_GLADE_FILE_PATH"ch_color_icon.png")));
	} else {
		closedir(d);
		return gtk_image_get_pixbuf(GTK_IMAGE(gtk_image_new_from_file("icons/ch_color_icon.png")));
	}
}

static void channel_color_icon_set_color(GdkPixbuf *pb, GdkRGBA *color)
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
			pixel[i * rowstride + j + 0] = color->red * 255;
			pixel[i * rowstride + j + 1] = color->green * 255;
			pixel[i * rowstride + j + 2] = color->blue * 255;
			pixel[i * rowstride + j + 3] = color->alpha * 255;
		}
}

static bool show_channel(struct iio_channel *chn)
{
	const char *id = iio_channel_get_id(chn);

	if (iio_channel_is_output(chn) || !strcmp(id, "timestamp"))
		return false;
	else
		return iio_channel_is_scan_element(chn);
}

static void plot_channels_add_device(OscPlot *plot, const char *dev_name)
{
	OscPlotPrivate *priv = plot->priv;
	struct iio_context *ctx = priv->ctx;
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeStore *treestore;
	GtkTreeIter iter;

	treestore = GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	struct iio_device *iio_dev = NULL;

	if (ctx)
		iio_dev = iio_context_find_device(ctx, dev_name);

	gtk_tree_store_append(treestore, &iter, NULL);
	gtk_tree_store_set(treestore, &iter,
		ELEMENT_NAME, dev_name,
		IS_DEVICE, !!iio_dev,
		DEVICE_ACTIVE, !priv->nb_input_devices,
		ELEMENT_REFERENCE, iio_dev,
		SENSITIVE, TRUE,
		EXPANDED, TRUE,
		-1);
}

static void plot_channels_add_channel(OscPlot *plot, PlotChn *pchn)
{
	OscPlotPrivate *priv = plot->priv;
	struct iio_context *ctx = priv->ctx;
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeStore *treestore;
	GtkTreeIter parent_iter, child_iter;
	GdkPixbuf *new_icon;
	GdkRGBA *icon_color;
	gboolean ret;

	treestore = GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	new_icon = channel_color_icon_new(plot);
	icon_color = &pchn->graph_color;
	channel_color_icon_set_color(new_icon, icon_color);

	ret = get_iter_by_name(treeview, &parent_iter, pchn->parent_name, NULL);
	if (!ret) {
		fprintf(stderr,
			"Could not add %s channel to device %s. Device not found\n",
			pchn->name, pchn->parent_name);
		return;
	}

	struct iio_device *iio_dev = NULL;
	struct iio_channel *iio_chn = NULL;
	bool sensitive = true;
	bool active = false;

	if (ctx && (iio_dev = iio_context_find_device(ctx, pchn->parent_name))) {
		iio_chn = iio_device_find_channel(iio_dev, pchn->name, false);

		struct extra_info *ch_info = iio_channel_get_data(iio_chn);
		/* Check if there are any channel constraints */
		if (ch_info) {
			active = (ch_info->constraints & CONSTR_CHN_INITIAL_ENABLED);
			sensitive = !(ch_info->constraints & CONSTR_CHN_UNTOGGLEABLE);
		}
	}

	gtk_tree_store_append(treestore, &child_iter, &parent_iter);
	gtk_tree_store_set(treestore, &child_iter,
			ELEMENT_NAME, pchn->name,
			IS_CHANNEL, TRUE,
			CHANNEL_TYPE, pchn->type,
			CHANNEL_ACTIVE, active,
			ELEMENT_REFERENCE, iio_chn,
			CHANNEL_SETTINGS, pchn,
			CHANNEL_COLOR_ICON, new_icon,
			SENSITIVE, sensitive,
			PLOT_TYPE, TIME_PLOT,
			-1);
}

static void plot_channels_remove_channel(OscPlot *plot, GtkTreeIter *iter)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeModel *model;
	PlotChn *settings;

	model = gtk_tree_view_get_model(treeview);
	gtk_tree_model_get(model, iter, CHANNEL_SETTINGS, &settings, -1);
	plot_channel_remove_from_plot(plot, settings);
	gtk_tree_store_remove(GTK_TREE_STORE(model), iter);
}

static void device_list_treeview_init(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	struct iio_context *ctx = priv->ctx;
	unsigned int i, j;

	priv->nb_input_devices = 0;
	if (!ctx)
		goto math_channels;
	for (i = 0; i < iio_context_get_devices_count(ctx); i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		struct extra_dev_info *dev_info = iio_device_get_data(dev);
		const char *dev_name = get_iio_device_label_or_name(dev);

		if (dev_info->input_device == false)
			continue;

		if (!priv->current_device)
			priv->current_device = dev;

		plot_channels_add_device(plot, dev_name);
		priv->nb_input_devices++;

		GArray *channels = get_iio_channels_naturally_sorted(dev);

		for (j = 0; j < channels->len; ++j) {
			struct iio_channel *ch = g_array_index(channels,
				struct iio_channel *, j);
			if (!show_channel(ch))
				continue;

			const char *chn_name = iio_channel_get_name(ch) ?:
				iio_channel_get_id(ch);
			PlotIioChn *pic;

			pic = plot_iio_channel_new(priv->ctx, dev);
			if (!pic) {
				fprintf(stderr, "Could not create an iio plot"
					"channel with name %s in function %s\n",
					chn_name, __func__);
				break;
			}
			plot_channel_add_to_plot(plot, PLOT_CHN(pic));
			pic->iio_chn = ch;
			pic->base.type = PLOT_IIO_CHANNEL;
			pic->base.name = g_strdup(chn_name);
			pic->base.parent_name = g_strdup(dev_name);
			plot_channels_add_channel(plot, PLOT_CHN(pic));
		}
		g_array_free(channels, FALSE);
	}
math_channels:
#ifdef linux
	plot_channels_add_device(plot, MATH_CHANNELS_DEVICE);
	priv->nb_input_devices++;
#endif
	create_channel_list_view(plot);
	treeview_expand_update(plot);
}

static void saveas_device_changed_cb(GtkComboBoxText *box, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	struct iio_context *ctx = priv->ctx;
	struct iio_device *dev;
	GtkWidget *parent;
	GtkWidget *ch_checkbtn;
	gchar *active_device;
	unsigned int i;
	int d;

	parent = gtk_widget_get_parent(priv->saveas_channels_list);
	gtk_widget_destroy(priv->saveas_channels_list);
	priv->saveas_channels_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(parent), priv->saveas_channels_list, FALSE, TRUE, 0);

	active_device = gtk_combo_box_text_get_active_text(box);
	d = device_find_by_name(ctx, active_device);
	g_free(active_device);
	if (d < 0)
		return;

	dev = iio_context_get_device(ctx, d);

	GArray *channels = get_iio_channels_naturally_sorted(dev);

	for (i = 0; i < channels->len; ++i) {
		struct iio_channel *chn = g_array_index(channels, struct iio_channel *, i);

		if (!iio_channel_is_output(chn) && iio_channel_is_scan_element(chn)) {

			const char *name = iio_channel_get_name(chn) ?:
				iio_channel_get_id(chn);
			ch_checkbtn = gtk_check_button_new_with_label(name);
			gtk_box_pack_start(GTK_BOX(priv->saveas_channels_list), ch_checkbtn, FALSE, TRUE, 0);
		}
	}
	g_array_free(channels, FALSE);
	gtk_widget_show_all(priv->saveas_channels_list);
}

static void saveas_channels_list_fill(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkWidget *ch_window;
	GtkWidget *vbox;
	unsigned int num_devices = 0;
	unsigned int i;

	if (priv->ctx)
		num_devices = iio_context_get_devices_count(priv->ctx);
	ch_window = priv->viewport_saveas_channels;
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
	gtk_container_add(GTK_CONTAINER(ch_window), vbox);
	priv->device_combobox = gtk_combo_box_text_new();
	gtk_box_pack_start(GTK_BOX(vbox), priv->device_combobox, FALSE, TRUE, 0);
	priv->saveas_channels_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_end(GTK_BOX(vbox), priv->saveas_channels_list, FALSE, TRUE, 0);

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(priv->ctx, i);
		const char *name = get_iio_device_label_or_name(dev);
		struct extra_dev_info *dev_info = iio_device_get_data(dev);

		if (dev_info->input_device == false)
			continue;

		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(priv->device_combobox), name);
	}

	if (num_devices == 0)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(priv->device_combobox),
			"No Devices Available");
	g_signal_connect(priv->device_combobox, "changed",
		G_CALLBACK(saveas_device_changed_cb), (gpointer)plot);
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->device_combobox), 0);
	gtk_widget_set_size_request(priv->viewport_saveas_channels, -1, 150);
	gtk_widget_show_all(vbox);
}

static GSList * iio_chn_basenames_get(OscPlot *plot, const char *dev_name)
{
	struct iio_device *iio_dev;
	struct iio_channel *iio_chn;
	GSList *list = NULL;
	unsigned int i;

	if (!dev_name)
		return NULL;

	iio_dev = iio_context_find_device(plot->priv->ctx, dev_name);
	if (!iio_dev)
		return NULL;

	unsigned int nb_channels = iio_device_get_channels_count(iio_dev);

	for (i = 0; i < nb_channels; i++) {
		iio_chn = iio_device_get_channel(iio_dev, i);
		if (!show_channel(iio_chn))
			continue;

		const char *chn_name = iio_channel_get_name(iio_chn) ?:
			iio_channel_get_id(iio_chn);

		char *basename, *c;

		basename = g_strdup_printf("%s", chn_name);
		for (c = basename; *c; c++) {
			if (g_ascii_isdigit(*c))
				*c = 0;
		}

		if (list) {
			if (!g_slist_find_custom(list, basename, (GCompareFunc)strcmp)) {
				list = g_slist_append(list, basename);
			}
		} else {
			list = g_slist_append(list, basename);
		}
	}

	return list;
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

static void update_grid(OscPlot *plot, gfloat left, gfloat right)
{
	OscPlotPrivate *priv = plot->priv;

	if (priv->active_transform_type == FFT_TRANSFORM ||
	    priv->active_transform_type == COMPLEX_FFT_TRANSFORM) {
		gfloat spacing;

		spacing = ceil((right - left) / 130) * 10;
		if (spacing < 10)
			spacing = 10;
		fill_axis(priv->gridx, left, spacing, 14);
		fill_axis(priv->gridy, 10, -10, 25);
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

	if (priv->active_transform_type == FFT_TRANSFORM ||
	    priv->active_transform_type == COMPLEX_FFT_TRANSFORM) {
		priv->grid = gtk_databox_grid_array_new (25, 14, priv->gridy, priv->gridx, &color_grid, 1);
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

	} else if (priv->active_transform_type == FREQ_SPECTRUM_TRANSFORM) {
		gfloat min_x;
		gfloat max_x;
		gfloat min_y;
		gfloat max_y;
		gfloat width;

		gint extrema_success = gtk_databox_calculate_extrema(box,
				&min_x, &max_x, &min_y, &max_y);
		if (extrema_success)
			return;
		if (min_x == 0) {
			min_x = priv->start_freq;
		}
		width = priv->filter_bw * priv->fft_count;

		gtk_databox_set_total_limits(box, min_x - 0.05 * width,
				max_x + 0.05 * width, max_y, min_y);
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
	}

	gtk_databox_set_visible_limits(GTK_DATABOX(priv->databox), left, right, top, bottom);
}

static void transform_csv_print(OscPlotPrivate *priv, FILE *fp, Transform *tr)
{
	gfloat *tr_data;
	gfloat *tr_x_axis;
	unsigned int i;
	GSList *node;
	const char *id1 = NULL, *id2 = NULL;

	switch (g_slist_length(tr->plot_channels)) {
	case 2:
		node = g_slist_nth(tr->plot_channels, 1);
		id2 = PLOT_CHN(node->data)->name;
		fallthrough;
	case 1:
		node = g_slist_nth(tr->plot_channels, 0);
		id1 = PLOT_CHN(node->data)->name;
		break;
	default:
		break;
	}

	if (tr->type_id == TIME_TRANSFORM)
		fprintf(fp, "X Axis(Sample Index)    Y Axis(%s)\n", id1);
	else if (tr->type_id == FFT_TRANSFORM)
		fprintf(fp, "X Axis(Frequency)    Y Axis(FFT - %s)\n", id1);
	else if (tr->type_id == COMPLEX_FFT_TRANSFORM)
		fprintf(fp, "X Axis(Frequency)    Y Axis(Complex FFT - %s, %s)\n", id1, id2);
	else if (tr->type_id == CONSTELLATION_TRANSFORM)
		fprintf(fp, "X Axis(%s)    Y Axis(%s)\n", id2, id1);

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
	osc_plot_draw_stop(plot);
	g_slist_free_full(plot->priv->ch_settings_list, (GDestroyNotify)g_free);
	g_mutex_trylock(&plot->priv->g_marker_copy_lock);
	g_mutex_unlock(&plot->priv->g_marker_copy_lock);

	g_signal_emit(plot, oscplot_signals[DESTROY_EVENT_SIGNAL], 0);
}

static GdkPixbuf * window_get_screenshot_pixbuf(GtkWidget *window)
{
	GdkWindow *gdk_w;
	GtkAllocation allocation;
	gint width, height;

	gdk_w = gtk_widget_get_window(window);
	gtk_widget_get_allocation(window, &allocation);
	width = gdk_window_get_width(gdk_w);
	height = gdk_window_get_height(gdk_w);

	return gdk_pixbuf_get_from_window(gdk_w, allocation.x, allocation.y, width, height);
}

static void screenshot_saveas_png(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GdkPixbuf *pixbuf;
	char *filename;
	GError *err = NULL;
	gboolean ret = true;

	filename = priv->saveas_filename;
	if (!filename) {
		fprintf(stderr, "error invalid filename");
		return;
	}

	pixbuf = window_get_screenshot_pixbuf(priv->window);
	if (pixbuf)
		ret = gdk_pixbuf_save(pixbuf, filename, "png", &err, NULL);
	else
		fprintf(stderr,
			"error getting the pixbug of the Capture Plot window\n");


	if (!ret) {
		fprintf(stderr, "error creating %s\n", filename);
		if (err)
			fprintf(stderr, "error(%d):%s\n", err->code,
					err->message);
	}

	return;
}

static void copy_channel_state_to_selection_channel(GtkTreeModel *model,
		GtkTreeIter *iter, void *user_data)
{
	OscPlot *plot = user_data;
	OscPlotPrivate *priv = plot->priv;
	struct iio_channel *chn;
	gboolean active_state;
	GList *ch_checkbtns = NULL;
	GList *node;
	GtkToggleButton *btn;
	const char *ch_name;
	gchar *btn_label;

	gtk_tree_model_get(model, iter, ELEMENT_REFERENCE, &chn, CHANNEL_ACTIVE, &active_state, -1);
	ch_name = iio_channel_get_name(chn) ?: iio_channel_get_id(chn);

	/* Get user channel selection from GUI widgets */
	ch_checkbtns = gtk_container_get_children(GTK_CONTAINER(priv->saveas_channels_list));
	for (node = ch_checkbtns; node; node = g_list_next(node)) {
		btn = (GtkToggleButton *)node->data;
		g_object_get(btn, "label", &btn_label, NULL);
		if (!strcmp(ch_name, btn_label) && !iio_channel_is_output(chn)) {
			gtk_toggle_button_set_active(btn, active_state);
			g_free(btn_label);
			break;
		}
		g_free(btn_label);
	}
	g_list_free(ch_checkbtns);

}

static void channel_selection_set_default(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	gchar *device_name;
	int d;

	device_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->device_combobox));
	d = device_find_by_name(priv->ctx, device_name);
	if (d >= 0)
		foreach_channel_iter_of_device(GTK_TREE_VIEW(priv->channel_list_view),
				device_name, *copy_channel_state_to_selection_channel, plot);
	g_free(device_name);
}

static int * get_user_saveas_channel_selection(OscPlot *plot, unsigned int *nb_channels)
{
	OscPlotPrivate *priv = plot->priv;
	GList *ch_checkbtns;
	GList *node;
	GtkToggleButton *btn;
	int *mask;

	/* Get user channel selection from GUI widgets */
	ch_checkbtns = gtk_container_get_children(GTK_CONTAINER(priv->saveas_channels_list));

	/* Create masks for all channels */
	mask = malloc(sizeof(int) * g_list_length(ch_checkbtns));
	*nb_channels = g_list_length(ch_checkbtns);

	for (node = ch_checkbtns; node; node = g_list_next(node)) {
		btn = (GtkToggleButton *)node->data;

		int dev_index, ch_index;
		gchar *dev_name, *ch_name;

		dev_name = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->device_combobox));
		dev_index = device_find_by_name(priv->ctx, dev_name);
		g_free(dev_name);
		g_object_get(btn, "label", &ch_name, NULL);
		ch_index = channel_find_by_name(priv->ctx, dev_index, ch_name);
		if (ch_index < 0) {
			fprintf(stderr, "Cannot find channel %s\n", ch_name);
			g_free(ch_name);
			break;
		}
		g_free(ch_name);
		mask[ch_index] = !gtk_toggle_button_get_active(btn);
	}
	g_list_free(ch_checkbtns);

	return mask;
}

#define SAVE_AS_RAW_DATA 1

static void saveas_dialog_show(GtkWidget *w, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	gtk_file_chooser_set_action(GTK_FILE_CHOOSER (priv->saveas_dialog), GTK_FILE_CHOOSER_ACTION_SAVE);
	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(priv->saveas_dialog), TRUE);

	if (!priv->saveas_filename) {
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (priv->saveas_dialog), getenv("HOME"));
	} else {
		if (!gtk_file_chooser_set_filename(GTK_FILE_CHOOSER (priv->saveas_dialog), priv->saveas_filename))
			gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (priv->saveas_dialog), getenv("HOME"));
		g_free(priv->saveas_filename);
		priv->saveas_filename = NULL;
	}

	priv->active_saveas_type = SAVE_AS_RAW_DATA;
	channel_selection_set_default(plot);
	gtk_widget_show(priv->saveas_dialog);
}

static void save_as(OscPlot *plot, const char *filename, int type)
{
	OscPlotPrivate *priv = plot->priv;
	struct iio_context *ctx = priv->ctx;
	FILE *fp;
	mat_t *mat;
	matvar_t *matvar;
	struct iio_device *dev;
	struct extra_dev_info *dev_info;
	char tmp[100];
	mat_dim dims[2] = {-1, 1};
	double freq;
	char *name;
	gchar *active_device;
	int *save_channels_mask;
	int d;
	unsigned int nb_channels, i, j;
	const char *dev_name;
	unsigned int dev_sample_count;

	name = malloc(strlen(filename) + 5);
	switch(type) {
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
			d = device_find_by_name(ctx, active_device);
			g_free(active_device);
			if (d < 0)
				break;
			dev = iio_context_get_device(ctx, d);
			dev_info = iio_device_get_data(dev);

			/* Find which channel need to be saved */
			save_channels_mask = get_user_saveas_channel_selection(plot, &nb_channels);

			/* Make a VSA file header */
			fprintf(fp, "InputZoom\tTRUE\n");
			fprintf(fp, "InputCenter\t0\n");
			fprintf(fp, "InputRange\t1\n");
			fprintf(fp, "InputRefImped\t50\n");
			fprintf(fp, "XStart\t0\n");
			freq = dev_info->adc_freq * prefix2scale(dev_info->adc_scale);
			fprintf(fp, "XDelta\t%-.17f\n", 1.0/freq);
			fprintf(fp, "XDomain\t2\n");
			fprintf(fp, "XUnit\tSec\n");
			fprintf(fp, "YUnit\tV\n");
			fprintf(fp, "FreqValidMax\t%e\n", freq / 2);
			fprintf(fp, "FreqValidMin\t-%e\n", freq / 2);
			fprintf(fp, "Y\n");

			dev_sample_count = dev_info->sample_count;
			if (dev_info->channel_trigger_enabled)
				dev_sample_count /= 2;

			/* Start writing the samples */
			for (i = 0; i < dev_sample_count; i++) {
				for (j = 0; j < nb_channels; j++) {
					struct extra_info *info = iio_channel_get_data(iio_device_get_channel(dev, j));
					if (save_channels_mask[j] == 1)
						continue;
					fprintf(fp, "%g\t", info->data_ref[i]);
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
				d = device_find_by_name(ctx, active_device);
				g_free(active_device);
				if (d < 0)
					break;

				dev = iio_context_get_device(ctx, d);
				dev_info = iio_device_get_data(dev);

				/* Find which channel need to be saved */
				save_channels_mask = get_user_saveas_channel_selection(plot, &nb_channels);

				dev_sample_count = dev_info->sample_count;
				if (dev_info->channel_trigger_enabled)
					dev_sample_count /= 2;

				for (i = 0; i < dev_sample_count; i++) {
					for (j = 0; j < nb_channels; j++) {
						struct extra_info *info = iio_channel_get_data(iio_device_get_channel(dev, j));
						if (save_channels_mask[j] == 1)
							continue;
						fprintf(fp, "%g, ", info->data_ref[i]);
					}
					fprintf(fp, "\n");
				}
				fprintf(fp, "\n");
				free(save_channels_mask);
			} else {
				for (d = 0; d < priv->transform_list->size; d++) {
						transform_csv_print(priv, fp, priv->transform_list->transforms[d]);
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
			priv->save_as_png = true;

			break;

		case SAVE_MAT:
			/* Matlab file
			 * http://na-wiki.csc.kth.se/mediawiki/index.php/MatIO
			 */
			 if (!strncasecmp(&filename[strlen(filename)-4], ".mat", 4))
					strcpy(name, filename);
				else
					sprintf(name, "%s.mat", filename);

			mat = Mat_Create(name, NULL);
			if (!mat) {
				fprintf(stderr, "Error creating MAT file %s: %s\n", name, strerror(errno));
				break;
			}

			active_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->device_combobox));
			d = device_find_by_name(ctx, active_device);
			g_free(active_device);
			if (d < 0)
				break;

			dev = iio_context_get_device(ctx, d);
			dev_info = iio_device_get_data(dev);
			dev_name = get_iio_device_label_or_name(dev);

			/* Find which channel need to be saved */
			save_channels_mask = get_user_saveas_channel_selection(plot, &nb_channels);

			dev_sample_count = dev_info->sample_count;
			if (dev_info->channel_trigger_enabled)
				dev_sample_count /= 2;

			dims[0] = dev_sample_count;
			for (i = 0; i < nb_channels; i++) {
				struct iio_channel *chn = iio_device_get_channel(dev, i);
				const char *ch_name = iio_channel_get_name(chn) ?:
					iio_channel_get_id(chn);
				struct extra_info *info = iio_channel_get_data(chn);
				if (save_channels_mask[i] == 1)
					continue;
				sprintf(tmp, "%s_%s", dev_name, ch_name);
				g_strdelimit(tmp, "-", '_');
				if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->save_mat_scale))) {
						matvar = Mat_VarCreate(tmp, MAT_C_SINGLE, MAT_T_SINGLE, 2, dims,
					info->data_ref, 0);
				} else {
					const struct iio_data_format* format = iio_channel_get_data_format(chn);
					gdouble *tmp_data;
					double k;

					tmp_data = g_new(gdouble, dev_sample_count);
					if (format->is_signed)
						k = format->bits - 1;
					else
						k = format->bits;
					for (j = 0; j < dev_sample_count; j++) {
						tmp_data[j] = (gdouble)info->data_ref[j] /
									(pow(2.0, k));
					}
					matvar = Mat_VarCreate(tmp, MAT_C_DOUBLE, MAT_T_DOUBLE,
							2, dims, tmp_data, 0);
					g_free(tmp_data);
				}

				if (!matvar)
					fprintf(stderr,
						"error creating matvar on channel %s\n",
						tmp);
				else {
					Mat_VarWrite(mat, matvar, 0);
					Mat_VarFree(matvar);
				}
			}
			free(save_channels_mask);

			Mat_Close(mat);
			break;

		default:
			fprintf(stderr, "SaveAs response: %i\n", type);
	}

	if (priv->saveas_filename)
		g_free(priv->saveas_filename);

	priv->saveas_filename = g_strdup(name);
	free(name);
}

void cb_saveas_response(GtkDialog *dialog, gint response_id, OscPlot *plot)
{
	/* Save as Dialog */
	OscPlotPrivate *priv = plot->priv;

	priv->saveas_filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (priv->saveas_dialog));

	if (response_id == GTK_RESPONSE_ACCEPT) {
		gint type = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->cmb_saveas_type));
		save_as(plot, priv->saveas_filename, type);
	}

	gtk_widget_hide(priv->saveas_dialog);

	if (priv->save_as_png) {
		int i = 0, timeout = 1000;
		while (gtk_events_pending() && i < timeout) {
			i++;
			gtk_main_iteration();
		}
		screenshot_saveas_png(plot);
		priv->save_as_png = false;
	}
}

static void enable_auto_scale_cb(GtkToggleButton *button, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	gfloat left, right, top, bottom;

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->enable_auto_scale))) {
		priv->do_a_rescale_flag = 1;
		gtk_widget_set_sensitive(plot->priv->y_axis_max, FALSE);
		gtk_widget_set_sensitive(plot->priv->y_axis_min, FALSE);
	} else {
		gtk_databox_get_visible_limits(GTK_DATABOX(plot->priv->databox),
			&left, &right, &top, &bottom);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(plot->priv->y_axis_max),
			top);
		gtk_widget_set_sensitive(plot->priv->y_axis_max, TRUE);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(plot->priv->y_axis_min),
			bottom);
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

static void count_changed_cb(GtkSpinButton *box, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	struct extra_dev_info *dev_info;
	gdouble freq = 0;

	if (priv->current_device) {
		dev_info = iio_device_get_data(priv->current_device);
		freq = dev_info->adc_freq * prefix2scale(dev_info->adc_scale);
	}

	switch(gtk_combo_box_get_active(GTK_COMBO_BOX(priv->hor_units))) {
	case HOR_SCALE_SAMPLES:
		priv->sample_count = (int)gtk_spin_button_get_value(box);
		break;
	case HOR_SCALE_TIME:
		priv->sample_count = (int)round((gtk_spin_button_get_value(box) *
				freq) / pow(10.0, 6));
		break;
	}
}

static void units_changed_cb(GtkComboBoxText *box, OscPlot *plot)
{

	OscPlotPrivate *priv = plot->priv;
	struct extra_dev_info *dev_info;
	int tmp_int;
	gdouble freq = 0, tmp_d;
	GtkAdjustment *limits;

	if (priv->current_device) {
		dev_info = iio_device_get_data(priv->current_device);
		freq = dev_info->adc_freq * prefix2scale(dev_info->adc_scale);
	}

	g_signal_handlers_block_by_func(priv->sample_count_widget, G_CALLBACK(count_changed_cb), plot);

	limits = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(priv->sample_count_widget));

	tmp_int = gtk_combo_box_get_active(GTK_COMBO_BOX(box));
	switch(tmp_int) {
	case 0:
		gtk_label_set_text(GTK_LABEL(priv->hor_scale), "Samples");
		gtk_spin_button_set_digits(GTK_SPIN_BUTTON(priv->sample_count_widget), 0);
		gtk_adjustment_set_lower(limits, 10.0);
		gtk_adjustment_set_upper(limits, MAX_SAMPLES);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->sample_count_widget), priv->sample_count);
		break;
	case 1:
		gtk_label_set_text(GTK_LABEL(priv->hor_scale), "µs");
		gtk_spin_button_set_digits(GTK_SPIN_BUTTON(priv->sample_count_widget), 3);
		if (freq) {
			gtk_adjustment_set_lower(limits, 10.0 * pow(10.0, 6)/freq);
			gtk_adjustment_set_upper(limits, MAX_SAMPLES * pow(10.0, 6)/freq);
			tmp_d = (pow(10.0, 6)/freq) * priv->sample_count;
			tmp_d = round(tmp_d * 1000.0) / 1000.0;
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->sample_count_widget), tmp_d);
		}
		break;
	}

	g_signal_handlers_unblock_by_func(priv->sample_count_widget, G_CALLBACK(count_changed_cb), plot);
}

static gboolean get_iter_by_name(GtkTreeView *tree, GtkTreeIter *iter,
		const char *dev_name, const char *ch_name)
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
	gboolean expanded;
	gboolean device_active;
	gboolean ch_enabled;
	PlotChn *csettings;
	FILE *fp;

	model = gtk_tree_view_get_model(tree);

	fp = fopen(filename, "a");
	if (!fp) {
		fprintf(stderr, "Failed to open %s : %s\n", filename, strerror(errno));
		return;
	}
	fprintf(fp, "\n[%s%d]\n", CAPTURE_INI_SECTION, priv->object_id);

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
	else if (tmp_int == XCORR_PLOT)
		fprintf(fp, "correlation\n");
	else
		fprintf(fp, "unknown\n");

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(priv->hor_units))) {
	case HOR_SCALE_SAMPLES:
		fprintf(fp, "sample_count=%d\n",
			(int) gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->sample_count_widget)));
		break;
	case HOR_SCALE_TIME:
		fprintf(fp, "micro_seconds=%f\n",
			gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->sample_count_widget)));
		break;
	}

	tmp_int = comboboxtext_get_active_text_as_int(GTK_COMBO_BOX_TEXT(priv->fft_size_widget));
	fprintf(fp, "fft_size=%d\n", tmp_int);

	tmp_string = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->fft_win_widget));
	fprintf(fp, "fft_win=%s\n", tmp_string);

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

	fprintf(fp, "line_thickness = %i\n", priv->line_thickness);

	fprintf(fp, "plot_title = %s\n", gtk_window_get_title(GTK_WINDOW(priv->window)));

	fprintf(fp, "show_capture_options = %d\n", gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(priv->menu_show_options)));

	gtk_window_get_size(GTK_WINDOW(priv->window), &priv->size.width, &priv->size.height);
	fprintf(fp, "plot_width = %d\n", priv->size.width);
	fprintf(fp, "plot_height = %d\n", priv->size.height);

	gint x_pos, y_pos;
	gtk_window_get_position(GTK_WINDOW(priv->window), &x_pos, &y_pos);
	fprintf(fp, "plot_x_pos=%d\n", x_pos);
	fprintf(fp, "plot_y_pos=%d\n", y_pos);

	next_dev_iter = gtk_tree_model_get_iter_first(model, &dev_iter);
	while (next_dev_iter) {
		struct iio_device *dev;
		char *name;

		gtk_tree_model_get(model, &dev_iter,
				ELEMENT_REFERENCE, &dev,
				ELEMENT_NAME, &name,
				DEVICE_ACTIVE, &device_active,
				-1);
		/* TO DO: Remove this hack (the if-branch) that skips saving to
		 * .ini file all settings of the Math device including its math
		 * expressions. Implement a way to save and also load math
		 * expressions. */
		if (!strncmp(name, "Math", strlen("Math"))) {
			g_free(name);
			next_dev_iter = gtk_tree_model_iter_next(model, &dev_iter);
			continue;
		}

		expanded = gtk_tree_view_row_expanded(tree, gtk_tree_model_get_path(model, &dev_iter));
		fprintf(fp, "%s.expanded=%d\n", name, (expanded) ? 1 : 0);
		fprintf(fp, "%s.active=%d\n", name, (device_active) ? 1 : 0);

		if (dev) {
			struct extra_dev_info *info = iio_device_get_data(dev);
			fprintf(fp, "%s.trigger_enabled=%i\n", name,
					info->channel_trigger_enabled);
			if (info->channel_trigger_enabled) {
				fprintf(fp, "%s.trigger_channel=%u\n", name,
						info->channel_trigger);
				fprintf(fp, "%s.trigger_falling_edge=%i\n", name,
						info->trigger_falling_edge);
				fprintf(fp, "%s.trigger_value=%f\n", name,
						info->trigger_value);
			}
		}

		next_ch_iter = gtk_tree_model_iter_children(model, &ch_iter, &dev_iter);
		while (next_ch_iter) {
			struct iio_channel *ch;
			char *ch_name;

			gtk_tree_model_get(model, &ch_iter,
					ELEMENT_REFERENCE, &ch,
					CHANNEL_ACTIVE, &ch_enabled,
					CHANNEL_SETTINGS, &csettings,
					-1);
			ch_name = csettings->name;

			fprintf(fp, "%s.%s.enabled=%d\n", name, ch_name, (ch_enabled) ? 1 : 0);
			fprintf(fp, "%s.%s.color_red=%d\n", name, ch_name, (int)(csettings->graph_color.red * 255 + 0.5));
			fprintf(fp, "%s.%s.color_green=%d\n", name, ch_name, (int)(csettings->graph_color.green * 255 + 0.5));
			fprintf(fp, "%s.%s.color_blue=%d\n", name, ch_name, (int)(csettings->graph_color.blue * 255 + 0.5));
			switch (csettings->type) {
			case PLOT_IIO_CHANNEL:
				fprintf(fp, "%s.%s.math_apply_inverse_funct=%d\n", name, ch_name, PLOT_IIO_CHN(csettings)->apply_inverse_funct);
				fprintf(fp, "%s.%s.math_apply_multiply_funct=%d\n", name, ch_name, PLOT_IIO_CHN(csettings)->apply_multiply_funct);
				fprintf(fp, "%s.%s.math_apply_add_funct=%d\n", name, ch_name, PLOT_IIO_CHN(csettings)->apply_add_funct);
				fprintf(fp, "%s.%s.math_multiply_value=%f\n", name, ch_name, PLOT_IIO_CHN(csettings)->multiply_value);
				fprintf(fp, "%s.%s.math_add_value=%f\n", name, ch_name, PLOT_IIO_CHN(csettings)->add_value);
				break;
			case PLOT_MATH_CHANNEL:
				break;
			}
			next_ch_iter = gtk_tree_model_iter_next(model, &ch_iter);
		}
		g_free(name);
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

static int comboboxtext_get_active_text_as_int(GtkComboBoxText* combobox)
{
	gchar *active_text;
	int value = 0;

	active_text = gtk_combo_box_text_get_active_text(combobox);
	if (active_text) {
		value = atoi(active_text);
		g_free(active_text);
	}

	return value;
}

#define MATCH(s1, s2) strcmp(s1, s2) == 0
#define MATCH_N(s1, s2, n) strncmp(s1, s2, n) == 0
#define MATCH_SECT(s) strcmp(section, s) == 0
#define MATCH_NAME(n) strcmp(name, n) == 0
#define PLOT_ATTRIBUTE 0
#define DEVICE 1
#define CHANNEL 2

static int device_find_by_name(struct iio_context *ctx, const char *name)
{
	unsigned int num_devices = 0;
	unsigned int i;

	if (!name)
		return -1;
	if (ctx)
		num_devices = iio_context_get_devices_count(ctx);

	for (i = 0; i < num_devices; i++) {
		struct iio_device *dev = iio_context_get_device(ctx, i);
		const char *id = get_iio_device_label_or_name(dev);
		if (!strcmp(id, name))
			return i;
	}
	return -1;
}

static int channel_find_by_name(struct iio_context *ctx, int device_index,
				const char *name)
{
	struct iio_device *dev;
	unsigned int i, nb_channels = 0;

	if (!ctx)
		return -1;
	if (!name)
		return -1;

	dev = iio_context_get_device(ctx, device_index);

	if (dev)
		nb_channels = iio_device_get_channels_count(dev);

	for (i = 0; i < nb_channels; i++) {
		struct iio_channel *chn = iio_device_get_channel(dev, i);

		if (!iio_channel_is_output(chn) && iio_channel_is_scan_element(chn)) {

			const char *id = iio_channel_get_name(chn) ?:
				iio_channel_get_id(chn);
			if (!strcmp(id, name))
				return i;
		}
	}
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

int osc_plot_ini_read_handler (OscPlot *plot, int line, const char *section,
		const char *name, const char *value)
{
	OscPlotPrivate *priv = plot->priv;
	struct iio_context *ctx = priv->ctx;
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
	PlotChn *csettings;
	int ret = 0, i;
	FILE *fd;
	struct extra_dev_info *dev_info;

	elem_type = count_char_in_string('.', name);
	switch(elem_type) {
		case PLOT_ATTRIBUTE:
			if (MATCH_NAME("capture_started")) {
				if (priv->redraw_function && atoi(value))
					goto handled;
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
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->fft_win_correction), false);
				priv->profile_loaded_scale = TRUE;
				gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(priv->capture_button), atoi(value));
				priv->profile_loaded_scale = FALSE;
				osc_plot_set_visible(plot, true);
			} else if (MATCH_NAME("destroy_plot")) {
				osc_plot_destroy(plot);
			} else if (MATCH_NAME("domain")) {
				if (!strcmp(value, "time"))
					gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_domain), TIME_PLOT);
				else if (!strcmp(value, "fft"))
					gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_domain), FFT_PLOT);
				else if (!strcmp(value, "constellation"))
					gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_domain), XY_PLOT);
				else if (!strcmp(value, "correlation"))
					gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_domain), XCORR_PLOT);
				else
					goto unhandled;
			} else if (MATCH_NAME("sample_count")) {
				gtk_combo_box_set_active(GTK_COMBO_BOX(priv->hor_units), HOR_SCALE_SAMPLES);
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->sample_count_widget), atof(value));
			} else if (MATCH_NAME("micro_seconds")) {
				gtk_combo_box_set_active(GTK_COMBO_BOX(priv->hor_units), HOR_SCALE_TIME);
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->sample_count_widget), atof(value));
			} else if (MATCH_NAME("fft_size")) {
				if (!comboboxtext_set_active_by_string(GTK_COMBO_BOX(priv->fft_size_widget), value)) {
					gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(priv->fft_size_widget), value);
					if (!comboboxtext_set_active_by_string(GTK_COMBO_BOX(priv->fft_size_widget), value))
						goto unhandled;
				}
			} else if (MATCH_NAME("fft_win")) {
				if (!comboboxtext_set_active_by_string(GTK_COMBO_BOX(priv->fft_win_widget), value))
					goto unhandled;
			} else if (MATCH_NAME("fft_avg")) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->fft_avg_widget), atoi(value));
			} else if (MATCH_NAME("fft_pwr_offset")) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->fft_pwr_offset_widget), atof(value));
			} else if (MATCH_NAME("graph_type")) {
				if (!comboboxtext_set_active_by_string(GTK_COMBO_BOX(priv->plot_type), value))
					goto unhandled;
			} else if (MATCH_NAME("show_grid"))
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->show_grid), atoi(value));
			else if (MATCH_NAME("enable_auto_scale")) {
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->enable_auto_scale), atoi(value));
				if (atoi(value)) {
					gtk_widget_hide(priv->y_axis_max);
					gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(priv->builder, "labelYMax")));
					gtk_widget_hide(priv->y_axis_min);
					gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(priv->builder, "labelYMin")));
				} 
			} else if (MATCH_NAME("user_y_axis_max"))
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
			} else if (MATCH_NAME("plot_title")) {
				gtk_window_set_title(GTK_WINDOW(priv->window), value);
			} else if (MATCH_NAME("show_capture_options")) {
				gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(priv->menu_show_options), atoi(value));
			} else if (MATCH_NAME("plot_width")) {
				priv->size.width = atoi(value);
				gtk_window_resize(GTK_WINDOW(priv->window), priv->size.width, priv->size.height);
			} else if (MATCH_NAME("plot_height")) {
				priv->size.height = atoi(value);
				gtk_window_resize(GTK_WINDOW(priv->window), priv->size.width, priv->size.height);
			} else if (MATCH_NAME("plot_x_pos")) {
				if (atoi(value)) {
					priv->plot_x_pos = atoi(value);
					move_gtk_window_on_screen(GTK_WINDOW(priv->window), priv->plot_x_pos, priv->plot_y_pos);
				}
			} else if (MATCH_NAME("plot_y_pos")) {
				if (atoi(value)) {
					priv->plot_y_pos = atoi(value);
					move_gtk_window_on_screen(GTK_WINDOW(priv->window), priv->plot_x_pos, priv->plot_y_pos);
				}
			} else if (MATCH_NAME("marker_type")) {
				if (!strncmp(value, PEAK_MRK, strlen(PEAK_MRK))) {
					printf("set to peak\n");
					osc_plot_set_marker_type(plot, MARKER_PEAK);
				} else if (!strncmp(value, FIX_MRK, strlen(FIX_MRK))) {
					printf("set to fixed\n");
					osc_plot_set_marker_type(plot, MARKER_FIXED);
				} else if (!strncmp(value, SINGLE_MRK, strlen(SINGLE_MRK))) {
					printf("set to single tone markers\n");
					osc_plot_set_marker_type(plot, MARKER_ONE_TONE);
				} else if (!strncmp(value, DUAL_MRK, strlen(DUAL_MRK))) {
					printf("set to two tone markers\n");
					osc_plot_set_marker_type(plot, MARKER_TWO_TONE);
				} else if (!strncmp(value, IMAGE_MRK, strlen(IMAGE_MRK))) {
					printf("set to image markers\n");
					osc_plot_set_marker_type(plot, MARKER_IMAGE);
				} else {
					printf("setting all off\n");
					osc_plot_set_marker_type(plot, MARKER_OFF);
					for (i = 0; i <= MAX_MARKERS; i++)
						priv->markers[i].active = FALSE;
				}
			} else if (MATCH_NAME("save_png")) {
				save_as(plot, value, SAVE_PNG);
				i = 0;
				while (gtk_events_pending() && i < 1000) {
					gtk_main_iteration();
					i++;
				}
				screenshot_saveas_png(plot);
				priv->save_as_png = false;
			} else if (MATCH_NAME("cycle")) {
				unsigned int msecs;
				sscanf(value, "%u", &msecs);
				osc_process_gtk_events(msecs);
			} else if (MATCH_NAME("save_markers")) {
				fd = osc_get_log_file(value);
				if (!fd)
					return 0;

				for (i = 0; i <= MAX_MARKERS; i++) {
					if (priv->markers[i].active) {
						fprintf(fd, ", %f, %f", priv->markers[i].x, priv->markers[i].y);
						if (!isnan(priv->markers[i].angle))
							fprintf(fd, ", %f", priv->markers[i].angle);
					}
				}
				fprintf(fd, "\n");
				fclose(fd);
			} else if (MATCH_NAME("fru_connect")) {
				if (atoi(value) == 1) {
					i = fru_connect();
					if (i == GTK_RESPONSE_OK)
						ret = 0;
					else
						ret = -1;
				} else {
					ret = -1;
				}
			} else if (MATCH_NAME("line_thickness")) {
				if (atoi(value))
					priv->line_thickness = atoi(value);
			} else if (MATCH_NAME("quit") || MATCH_NAME("stop")) {
				application_quit();
				return 0;
			} else if (MATCH_NAME("echo")) {
				printf("echoing : '%s'\n", value);
				ret = 0;
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
					create_blocking_popup(GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK,
						"Profile status", value);
					break;
				} else {
					goto unhandled;
				}
			}
			/* TO DO: Remove this hack (the if-branch) that skips
			 * loading the settings of a math device and implement a way
			 * to load and save a math expression. */
			if (strncmp(dev_name, "Math", 4) == 0) {
				break;
			}

			if (strncmp(dev_name, "Math", 4) == 0) {
				dev_info = NULL;
			} else {
				dev = device_find_by_name(ctx, dev_name);
				if (dev == -1)
					goto unhandled;
				dev_info = iio_device_get_data(iio_context_find_device(ctx, dev_name));
			}

			if (MATCH(dev_property, "expanded")) {
				expanded = atoi(value);
				get_iter_by_name(tree, &dev_iter, dev_name, NULL);
				gtk_tree_store_set(store, &dev_iter, EXPANDED, expanded, -1);
			}else if (MATCH(dev_property, "active")) {
				device_active = atoi(value);
				get_iter_by_name(tree, &dev_iter, dev_name, NULL);
				gtk_tree_store_set(store, &dev_iter, DEVICE_ACTIVE, device_active, -1);
			} else if (MATCH(dev_property, "trigger_enabled")) {
				if (!dev_info)
					goto unhandled;
				dev_info->channel_trigger_enabled = !!atoi(value);
			} else if (MATCH(dev_property, "trigger_channel")) {
				if (!dev_info)
					goto unhandled;
				dev_info->channel_trigger = atoi(value);
			} else if (MATCH(dev_property, "trigger_falling_edge")) {
				if (!dev_info)
					goto unhandled;
				dev_info->trigger_falling_edge = !!atoi(value);
			} else if (MATCH(dev_property, "trigger_value")) {
				if (!dev_info)
					goto unhandled;
				dev_info->trigger_value = (float) atof(value);
			}
			break;
		case CHANNEL:
			elems = g_strsplit(name, ".", CHANNEL + 1);
			dev_name = elems[0];
			ch_name = elems[1];
			ch_property = elems[2];

			if (g_str_has_prefix(ch_name, "in_"))
				ch_name = ch_name + strlen("in_");

			if (MATCH(elems[0], "test")) {
				if (MATCH(elems[1], "marker")) {
					min_max = g_strsplit(value, " ", 0);
					min_f = atof(min_max[0]);
					max_f = atof(min_max[1]);
					i = atoi(elems[2]);

					printf("Line %i: (test.marker.%i = %f %f): %f\n",
							line, i, min_f, max_f,
							priv->markers[i].y);
					if (priv->markers[i].active &&
						priv->markers[i].y >= min_f &&
						priv->markers[i].y <= max_f) {
						ret = 0;
						printf("Test passed.\n");
					} else {
						ret = -1;
						printf("*** Test failed! ***\n");
						create_blocking_popup(GTK_MESSAGE_ERROR,
								GTK_BUTTONS_CLOSE,
								"Test failure",
								"Test failed! Line: %i\n\n"
								"Test was: test.marker.%i = %f %f\n"
								"Value read = %f\n",
								line, i, min_f, max_f, priv->markers[i].y);
					}
					g_strfreev(min_max);
				} else {
					goto unhandled;
				}
				break;
			}

			/* TO DO: Remove this hack (the if-branch) that skips
			 * loading settings of channels of a math device and
			 * implement a way to load and save a math expression. */
			if (strncmp(dev_name, "Math", 4) == 0)
				break;

			dev = device_find_by_name(ctx, dev_name);
			if (dev == -1)
				goto unhandled;
			ch = channel_find_by_name(ctx, dev, ch_name);
			if (ch == -1)
				goto unhandled;
			if (MATCH(ch_property, "enabled")) {
				enabled = atoi(value);
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				set_channel_state_in_tree_model(gtk_tree_view_get_model(tree), &ch_iter, enabled);
			} else if (MATCH(ch_property, "color_red")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				csettings->graph_color.red = atoi(value) / 255.0f;
			} else if (MATCH(ch_property, "color_green")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				csettings->graph_color.green = atoi(value) / 255.0f;
			} else if (MATCH(ch_property, "color_blue")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				csettings->graph_color.blue = atoi(value) / 255.0f;
			} else if (MATCH(ch_property, "math_apply_inverse_funct")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				PLOT_IIO_CHN(csettings)->apply_inverse_funct = atoi(value);
			} else if (MATCH(ch_property, "math_apply_multiply_funct")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				PLOT_IIO_CHN(csettings)->apply_multiply_funct = atoi(value);
			} else if (MATCH(ch_property, "math_apply_add_funct")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				PLOT_IIO_CHN(csettings)->apply_add_funct = atoi(value);
			} else if (MATCH(ch_property, "math_multiply_value")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				PLOT_IIO_CHN(csettings)->multiply_value = atof(value);
			} else if (MATCH(ch_property, "math_add_value")) {
				get_iter_by_name(tree, &ch_iter, dev_name, ch_name);
				gtk_tree_model_get(gtk_tree_view_get_model(tree), &ch_iter, CHANNEL_SETTINGS, &csettings, -1);
				PLOT_IIO_CHN(csettings)->add_value = atof(value);
			}
			break;
		default:
unhandled:
			create_blocking_popup(GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
					"Unhandled attribute",
					"Unhandled attribute in section [%s], "
					"line %i:\n%s = %s\n",section, line, name, value);
			fprintf(stderr, "Unhandled tokens in section [%s], line: %i: "
					"%s = %s\n", section, line, name, value);
			ret = -1;
			break;
	}
handled:
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
		snprintf(priv->markers[i].label, sizeof(priv->markers[i].label), buf, i);
		gtk_databox_markers_set_label(GTK_DATABOX_MARKERS(priv->markers[i].graph), 0,
			GTK_DATABOX_MARKERS_TEXT_N, priv->markers[i].label, FALSE);
		gtk_databox_graph_set_hide(priv->markers[i].graph, !priv->markers[i].active);
	}
}

static void set_marker_labels (OscPlot *plot, gchar *buf, enum marker_types type)
{
	OscPlotPrivate *priv = plot->priv;
	int i;

	if (!MAX_MARKERS)
		return;

	if ((buf && !strcmp(buf, PEAK_MRK)) || type == MARKER_PEAK) {
		priv->marker_type = MARKER_PEAK;
		for (i = 0; i <= MAX_MARKERS; i++)
			marker_set(plot, i, "P%i", FALSE);
		return;
	} else if ((buf && !strcmp(buf, FIX_MRK)) || type == MARKER_FIXED) {
		priv->marker_type = MARKER_FIXED;
		for (i = 0; i <= MAX_MARKERS; i++)
			marker_set(plot, i, "F%i", FALSE);
		return;
	} else if ((buf && !strcmp(buf, SINGLE_MRK)) || type == MARKER_ONE_TONE) {
		priv->marker_type = MARKER_ONE_TONE;
		marker_set(plot, 0, "Fund", TRUE);
		marker_set(plot, 1, "DC", TRUE);
		for (i = 2; i <= MAX_MARKERS; i++)
			marker_set(plot, i, "%iH", FALSE);
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
	} else if ((buf && !strcmp(buf, OFF_MRK)) || type == MARKER_OFF) {
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

	fprintf(stderr, "unhandled event at %s : %s\n", __func__, buf ? buf : "<null>");
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
	int max_size;
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

static gint marker_button(GtkDatabox *box, GdkEvent *event, gpointer data)
{
	OscPlot *plot = (OscPlot *)data;
	OscPlotPrivate *priv = plot->priv;
	gfloat x, y, dist;
	GtkWidget *popupmenu, *menuitem;
	gfloat left, right, top, bottom;
	int i, fix = -1;
	bool full = TRUE, empty = TRUE;
	GdkEventButton *event_button;

	/* FFT? */
	if (!is_frequency_transform(priv) &&
		priv->active_transform_type != CROSS_CORRELATION_TRANSFORM)
	return FALSE;

	event_button = (GdkEventButton *)event;

	/* Right button */
	if (event_button->button != 3)
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

	x = gtk_databox_pixel_to_value_x(box, event_button->x);
	y = gtk_databox_pixel_to_value_y(box, event_button->y);
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
		g_signal_connect_swapped(menuitem, "activate",
				G_CALLBACK(marker_menu), (gpointer) &priv->add_mrk);
		gtk_widget_show(menuitem);
		i++;
	}

	if (!empty && !(priv->marker_type == MARKER_OFF || priv->marker_type == MARKER_IMAGE)) {
		menuitem = gtk_menu_item_new_with_label(REMOVE_MRK);
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		g_signal_connect_swapped(menuitem, "activate",
				G_CALLBACK(marker_menu), (gpointer) &priv->remove_mrk);
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
	g_signal_connect_swapped(menuitem, "activate",
			G_CALLBACK(marker_menu), (gpointer) &priv->peak_mrk);
	gtk_widget_show(menuitem);
	i++;

	if (priv->active_transform_type == CROSS_CORRELATION_TRANSFORM ||
			priv->active_transform_type == FREQ_SPECTRUM_TRANSFORM)
		goto skip_no_peak_markers;

	menuitem = gtk_check_menu_item_new_with_label(FIX_MRK);
	gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
			priv->marker_type == MARKER_FIXED);
	g_signal_connect_swapped(menuitem, "activate",
			G_CALLBACK(marker_menu), (gpointer) &priv->fix_mrk);
	gtk_widget_show(menuitem);
	i++;

	menuitem = gtk_check_menu_item_new_with_label(SINGLE_MRK);
	gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
			priv->marker_type == MARKER_ONE_TONE);
	g_signal_connect_swapped(menuitem, "activate",
			G_CALLBACK(marker_menu), (gpointer) &priv->single_mrk);
	gtk_widget_show(menuitem);
	i++;

	/*
	menuitem = gtk_check_menu_item_new_with_label(DUAL_MRK);
	gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
			priv->marker_type == MARKER_TWO_TONE);
	g_signal_connect_swapped(menuitem, "activate",
			G_CALLBACK(marker_menu), (gpointer) &priv->dual_mrk);
	gtk_widget_show(menuitem);
	i++;
	*/

	if (priv->active_transform_type == COMPLEX_FFT_TRANSFORM) {
		menuitem = gtk_check_menu_item_new_with_label(IMAGE_MRK);
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
				priv->marker_type == MARKER_IMAGE);
		g_signal_connect_swapped(menuitem, "activate",
			G_CALLBACK(marker_menu), (gpointer) &priv->image_mrk);
		gtk_widget_show(menuitem);
		i++;
	}

skip_no_peak_markers:

	if (priv->marker_type != MARKER_OFF) {
		menuitem = gtk_check_menu_item_new_with_label(OFF_MRK);
		gtk_menu_attach(GTK_MENU(popupmenu), menuitem, 0, 1, i, i + 1);
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menuitem),
				priv->marker_type == MARKER_OFF);
		g_signal_connect_swapped(menuitem, "activate",
			G_CALLBACK(marker_menu), (gpointer) &priv->off_mrk);
		gtk_widget_show(menuitem);
		i++;
	}

	gtk_menu_popup_at_pointer(GTK_MENU(popupmenu), event);

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
	OscPlotPrivate *priv = plot->priv;
	gboolean force_sensitive = true;
	gboolean enabled_plot_units;
	gint plot_type;

	priv->marker_type = MARKER_OFF;
	check_valid_setup(plot);

	plot_type = gtk_combo_box_get_active(box);
	foreach_device_iter(GTK_TREE_VIEW(priv->channel_list_view),
			*iter_children_plot_type_update, plot);

	/* Allow horizontal units selection only for TIME plots */
	if (gtk_widget_is_sensitive(priv->hor_units))
		priv->last_hor_unit = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->hor_units));
	enabled_plot_units = (plot_type == TIME_PLOT) || (plot_type == XCORR_PLOT);
	gtk_widget_set_sensitive(priv->hor_units, enabled_plot_units);
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->hor_units),
		enabled_plot_units ? priv->last_hor_unit : 0);

	/* Allow only 1 active device for a FFT or XY plot */
	if (priv->nb_input_devices < 2)
		return;
	switch (plot_type) {
	case FFT_PLOT:
	case XY_PLOT:
		enable_tree_device_selection(plot, true);
		foreach_device_iter(GTK_TREE_VIEW(priv->channel_list_view),
			*iter_children_sensitivity_update, NULL);
		break;
	case TIME_PLOT:
	case XCORR_PLOT:
		enable_tree_device_selection(plot, false);
		foreach_device_iter(GTK_TREE_VIEW(priv->channel_list_view),
			*iter_children_sensitivity_update, &force_sensitive);
		break;
	};
}

static gboolean domain_is_fft(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer user_data)
{
	g_value_set_boolean(target_value, g_value_get_int(source_value) == FFT_PLOT ||
			g_value_get_int(source_value) == SPECTRUM_PLOT);
	return TRUE;
}

static gboolean domain_is_time(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer user_data)
{
	g_value_set_boolean(target_value, (g_value_get_int(source_value) != FFT_PLOT) &&
			(g_value_get_int(source_value) != SPECTRUM_PLOT));
	return TRUE;
}

static gboolean domain_is_xcorr_fft(GBinding *binding,
	const GValue *source_value, GValue *target_value, gpointer user_data)
{
	g_value_set_boolean(target_value, g_value_get_int(source_value) == FFT_PLOT ||
			g_value_get_int(source_value) == XCORR_PLOT ||
			g_value_get_int(source_value) == SPECTRUM_PLOT);
	return TRUE;
}

static void fft_avg_value_changed_cb(GtkSpinButton *button, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	int i, plot_type;

	plot_type = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain));

	for (i = 0; i < priv->transform_list->size; i++) {
		if (plot_type == FFT_PLOT)
			FFT_SETTINGS(priv->transform_list->transforms[i])->fft_avg = gtk_spin_button_get_value(button);
		else if (plot_type == XCORR_PLOT)
			XCORR_SETTINGS(priv->transform_list->transforms[i])->avg = gtk_spin_button_get_value(button);
		else if (plot_type == SPECTRUM_PLOT)
			FREQ_SPECTRUM_SETTINGS(priv->transform_list->transforms[i])->fft_avg = gtk_spin_button_get_value(button);
	}
}
static void fft_pwr_offset_value_changed_cb(GtkSpinButton *button, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	int i, plot_type;

	plot_type = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->plot_domain));

	for (i = 0; i < priv->transform_list->size; i++) {
		if (plot_type == FFT_PLOT)
			FFT_SETTINGS(priv->transform_list->transforms[i])->fft_pwr_off = gtk_spin_button_get_value(button);
		else if (plot_type == SPECTRUM_PLOT)
			FREQ_SPECTRUM_SETTINGS(priv->transform_list->transforms[i])->fft_pwr_off = gtk_spin_button_get_value(button);
	}
}

static gboolean tree_get_selected_row_iter(GtkTreeView *treeview, GtkTreeIter *iter)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GList *row;

	selection = gtk_tree_view_get_selection(treeview);
	model = gtk_tree_view_get_model(treeview);
	row = gtk_tree_selection_get_selected_rows(selection, &model);
	if (!row)
		return false;
	gtk_tree_model_get_iter(model, iter, row->data);

	return true;
}

static void device_trigger_settings_cb(GtkMenuItem *menuitem, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	struct iio_device *dev;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean selected;

	treeview = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(treeview);
	selected = tree_get_selected_row_iter(treeview, &iter);
	if (!selected)
		return;
	gtk_tree_model_get(model, &iter, ELEMENT_REFERENCE, &dev, -1);
	if (!dev) {
		fprintf(stderr, "Invalid reference of iio_device read from devicetree\n");
		return;
	}
	trigger_settings_for_device(priv->builder, iio_device_get_name(dev));
	check_valid_setup(plot);
}

static gint channel_compare(gconstpointer a, gconstpointer b)
{
	const char *a_name = iio_channel_get_name((struct iio_channel *)a) ?:
			iio_channel_get_id((struct iio_channel *)a);
	const char *b_name = iio_channel_get_name((struct iio_channel *)b) ?:
			iio_channel_get_id((struct iio_channel *)b);

	return strcmp(a_name, b_name);
}

static GSList * math_expression_get_iio_channel_list(const char *expression, struct iio_context *ctx, const char *device, bool *has_invalid_ch)
{

	GSList *chn_list = NULL;
	GRegex *regex, *regex_i, *regex_q;
	GMatchInfo *info;
	gchar *chn_name;
	struct iio_device *iio_dev;
	struct iio_channel *iio_chn;
	gboolean invalid_list = false, is_match = true, match_q, match_i, match;

	if (!device || !(iio_dev = iio_context_find_device(ctx, device)))
		return NULL;

	regex = g_regex_new("voltage[0-9]+", 0, 0, NULL);
	regex_i = g_regex_new("voltage[0-9]+_i", 0, 0, NULL);
	regex_q = g_regex_new("voltage[0-9]+_q", 0, 0, NULL);

	match_i = g_regex_match(regex_i, expression, 0, &info);

	if (!match_i) {
		match_q = g_regex_match(regex_q, expression, 0, &info);
		if (!match_q) {
			match = g_regex_match(regex, expression, 0, &info);
			if (!match)
				is_match = false;
		}
	}

	if (!is_match) {
		invalid_list = true;
	} else {
		*has_invalid_ch = false;
		do {
			chn_name = g_match_info_fetch(info, 0);
			if (chn_name && (iio_chn = iio_device_find_channel(iio_dev, chn_name, false))) {
				if (!g_slist_find_custom(chn_list, iio_chn, channel_compare))
					chn_list = g_slist_prepend(chn_list, iio_chn);
			} else {
				invalid_list = true;
				*has_invalid_ch = true;
			}
		} while (g_match_info_next(info, NULL) && !invalid_list);
	}
	g_match_info_free(info);
	g_regex_unref(regex);
	g_regex_unref(regex_i);
	g_regex_unref(regex_q);

	if (invalid_list) {
		if (chn_list)
			g_slist_free(chn_list);
		chn_list = NULL;
	}

	return chn_list;
}

static void math_chooser_clear_key_pressed_cb(GtkButton *btn, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTextBuffer *tbuf = priv->math_expression;

	gtk_text_buffer_set_text(tbuf, "", -1);
	gtk_widget_grab_focus(priv->math_expression_textview);
}

static void math_chooser_backspace_key_pressed_cb(GtkButton *btn, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTextBuffer *tbuf = priv->math_expression;
	GtkTextMark *insert_mark;
	GtkTextIter insert_iter;

	insert_mark = gtk_text_buffer_get_insert(tbuf);
	gtk_text_buffer_get_iter_at_mark(tbuf, &insert_iter, insert_mark);
	gtk_text_buffer_backspace(tbuf, &insert_iter, true, true);
	gtk_widget_grab_focus(priv->math_expression_textview);
}

static void math_chooser_fullscale_key_pressed_cb(GtkButton *btn, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTextBuffer *tbuf = priv->math_expression;
	struct iio_device *iio_dev;
	char key_val[128] = "0";
	gchar *device_name;

	device_name = gtk_combo_box_text_get_active_text(
			GTK_COMBO_BOX_TEXT(priv->math_device_select));
	if (device_name) {
		iio_dev = iio_context_find_device(priv->ctx, device_name);
		g_free(device_name);
		if (iio_dev) {
			unsigned int i;
			struct iio_channel *iio_chn;
			const struct iio_data_format *format;
			int full_scale;
			for (i = 0; i < iio_device_get_channels_count(iio_dev); i++) {
				iio_chn = iio_device_get_channel(iio_dev, i);
				if (!iio_channel_is_scan_element(iio_chn))
					continue;
				format = iio_channel_get_data_format(iio_chn);
				full_scale = 2 << (format->bits - 1);
				if (format->is_signed)
					full_scale /= 2;
				snprintf(key_val, sizeof(key_val), "%d", full_scale);
				break;
			}
		}
	}

	gtk_text_buffer_insert_at_cursor(tbuf, key_val, -1);
	gtk_widget_grab_focus(priv->math_expression_textview);
}

static void math_chooser_key_pressed_cb(GtkButton *btn, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTextBuffer *tbuf = priv->math_expression;
	GtkTextMark *insert_mark;
	GtkTextIter insert_iter;
	const gchar *key_label;

	key_label = gtk_button_get_label(btn);
	gtk_text_buffer_insert_at_cursor(tbuf, key_label, -1);

	if (g_str_has_suffix(key_label, ")") && g_strrstr(key_label, "(")) {
		insert_mark = gtk_text_buffer_get_insert(tbuf);
		gtk_text_buffer_get_iter_at_mark(tbuf, &insert_iter, insert_mark);
		gtk_text_iter_backward_char(&insert_iter);
		gtk_text_buffer_place_cursor(tbuf, &insert_iter);
	}

	gtk_widget_grab_focus(priv->math_expression_textview);
}

static void buttons_table_remove_child(GtkWidget *child, gpointer data)
{
	GtkContainer *container = GTK_CONTAINER(data);

	gtk_container_remove(container, child);
}

static void math_device_cmb_changed_cb(GtkComboBoxText *box, OscPlot *plot)
{

        char *device_name;
        const char *channel_name;
        struct iio_device *iio_dev;
        struct iio_channel *iio_chn;
        GtkWidget *button, *buttons_table;
        int row, col, sc;
        unsigned int i;

        device_name = gtk_combo_box_text_get_active_text(box);
        if (!device_name)
          return;

        iio_dev = iio_context_find_device(plot->priv->ctx, device_name);
        if (!iio_dev)
          goto end;

        buttons_table = GTK_WIDGET(gtk_builder_get_object(plot->priv->builder,
                                                          "table_channel_buttons"));
        gtk_container_foreach(GTK_CONTAINER(buttons_table),
                             buttons_table_remove_child, buttons_table);

        for (i = 0, sc = 0; i < iio_device_get_channels_count(iio_dev); i++) {
            iio_chn = iio_device_get_channel(iio_dev, i);

            if (iio_channel_is_scan_element(iio_chn)) {
                channel_name = iio_channel_get_name(iio_chn) ?:
                                                              iio_channel_get_id(iio_chn);
                button = gtk_button_new_with_label(channel_name);
                row = sc % 4;
                col = sc / 4;
                gtk_grid_attach(GTK_GRID(buttons_table),
                                button, col, row, 1, 1);
                sc++;
              }
          }
        GList *node;

        for (node = gtk_container_get_children(GTK_CONTAINER(buttons_table)); node; node = g_list_next(node)) {
            g_signal_connect(node->data, "clicked", G_CALLBACK(math_chooser_key_pressed_cb), plot);
          }

        gtk_widget_show_all(buttons_table);
end:
        g_free(device_name);
}

static int math_expression_get_settings(OscPlot *plot, PlotMathChn *pmc)
{
	OscPlotPrivate *priv = plot->priv;
	char *active_device;
	int ret;
	void *lhandler;
	math_function fn;
	GSList *channels = NULL;
	gchar *txt_math_expr;
	bool invalid_channels;
	const char *channel_name;
	char *expression_name;
	struct iio_device *dev;

	math_device_cmb_changed_cb(GTK_COMBO_BOX_TEXT(priv->math_device_select), plot);

	active_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->math_device_select));
	if (!active_device) {
		fprintf(stderr, "Error: No device available in %s\n", __func__);
		return -1;
	}

	dev = iio_context_find_device(priv->ctx, active_device);
	if (dev) {
		pmc->base.dev = dev;
	} else {
		fprintf(stderr, "Error: Failed to get 'iio_device *' for %s\n", active_device);
	}

	if (pmc->txt_math_expression)
		gtk_text_buffer_set_text(priv->math_expression,
			pmc->txt_math_expression, -1);
	else
		gtk_text_buffer_set_text(priv->math_expression, "", -1);

	if (pmc->base.name) {
		gtk_entry_set_text(GTK_ENTRY(priv->math_channel_name_entry),
			pmc->base.name);
	} else {
		expression_name = g_strdup_printf("expression %d",
			num_of_channels_of_device(GTK_TREE_VIEW(priv->channel_list_view),
				MATH_CHANNELS_DEVICE));
		gtk_entry_set_text(GTK_ENTRY(priv->math_channel_name_entry),
			expression_name);
		g_free(expression_name);
	}

	gtk_widget_set_visible(priv->math_expr_error, false);

	/* Get the math expression from user */
	do {
		ret = gtk_dialog_run(GTK_DIALOG(priv->math_expression_dialog));
		if (ret != GTK_RESPONSE_OK)
			break;

		g_free(active_device);
		active_device = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(priv->math_device_select));

		channel_name = gtk_entry_get_text(GTK_ENTRY(priv->math_channel_name_entry));
		if (plot_channel_check_name_exists(plot, channel_name, PLOT_CHN(pmc)))
			channel_name = NULL;

		/* Get the string of the math expression */
		GtkTextIter start;
		GtkTextIter end;

		gtk_text_buffer_get_start_iter(priv->math_expression, &start);
		gtk_text_buffer_get_end_iter(priv->math_expression, &end);
		txt_math_expr = gtk_text_buffer_get_text(priv->math_expression, &start, &end, FALSE);

		/* Find device channels used in the expression */
		channels = math_expression_get_iio_channel_list(txt_math_expr,
				priv->ctx, active_device, &invalid_channels);

		/* Get the compiled math expression */
		GSList *basenames = iio_chn_basenames_get(plot, active_device);
		fn = math_expression_get_math_function(txt_math_expr, &lhandler, basenames);
		if (basenames) {
			g_slist_free_full(basenames, (GDestroyNotify)g_free);
			basenames = NULL;
		}

		gtk_widget_set_visible(priv->math_expr_error, true);
		if (!fn)
			gtk_label_set_text(GTK_LABEL(priv->math_expr_error), "Invalid math expression.");
		else if (!channel_name)
			gtk_label_set_text(GTK_LABEL(priv->math_expr_error), "An expression with the same name already exists");
		else
			gtk_widget_set_visible(priv->math_expr_error, false);
	} while (!fn || !channel_name);
	gtk_widget_hide(priv->math_expression_dialog);
	if (ret != GTK_RESPONSE_OK) {
		g_free(active_device);
		return - 1;
	}

	/* Store the settings of the new channel*/
	if (pmc->txt_math_expression)
		g_free(pmc->txt_math_expression);
	if (pmc->base.name)
		g_free(pmc->base.name);
	if (pmc->iio_device_name)
		g_free(pmc->iio_device_name);
	if (pmc->iio_channels)
		g_slist_free(pmc->iio_channels);

	pmc->txt_math_expression = txt_math_expr;
	pmc->base.name = g_strdup(channel_name);
	pmc->iio_device_name = g_strdup(active_device);
	pmc->iio_channels = channels;
	pmc->math_expression = fn;
	pmc->math_lib_handler = lhandler;
	pmc->num_channels = g_slist_length(pmc->iio_channels);
	pmc->iio_channels_data = iio_channels_get_data(priv->ctx,
					pmc->iio_device_name);

	g_free(active_device);

	return 0;
}

static void new_math_channel_cb(GtkMenuItem *menuitem, OscPlot *plot)
{
	int ret;

	/* Build a new Math Channel */
	PlotMathChn *pmc;

	pmc = plot_math_channel_new(plot->priv->ctx);
	if (!pmc)
		return;

	plot_channel_add_to_plot(plot, PLOT_CHN(pmc));
	pmc->base.type = PLOT_MATH_CHANNEL;
	pmc->base.parent_name = g_strdup(MATH_CHANNELS_DEVICE);

	ret = math_expression_get_settings(plot, pmc);
	if (ret < 0) {
		plot_channel_remove_from_plot(plot, PLOT_CHN(pmc));
		pmc = NULL;
		return;
	}

	/* Create GUI for the new channel */
	plot_channels_add_channel(plot, PLOT_CHN(pmc));

	treeview_expand_update(plot);
}

static void channel_edit_settings_cb(GtkMenuItem *menuitem, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean selected;

	treeview = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(treeview);
	selected = tree_get_selected_row_iter(treeview, &iter);
	if (!selected)
		return;

	/* Get Channel settings structure */
	PlotMathChn *settings;

	gtk_tree_model_get(model, &iter, CHANNEL_SETTINGS, &settings, -1);
	math_expression_get_settings(plot, settings);
	gtk_tree_store_set(GTK_TREE_STORE(model), &iter,
			ELEMENT_NAME, settings->base.name, -1);
}

static void plot_channel_remove_cb(GtkMenuItem *menuitem, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview;
	GtkTreeIter iter;
	gboolean selected;

	treeview = GTK_TREE_VIEW(priv->channel_list_view);
	selected = tree_get_selected_row_iter(treeview, &iter);
	if (!selected)
		return;

	plot_channels_remove_channel(plot, &iter);
	check_valid_setup(plot);
}

static void plot_trigger_save_settings(OscPlotPrivate *priv,
		const struct iio_device *dev)
{
	struct extra_dev_info *dev_info = iio_device_get_data(dev);
	struct iio_channel *chn;
	GtkComboBoxText *box;
	GtkToggleButton *radio;
	GtkSpinButton *btn;
	gchar *active_channel;

	radio = GTK_TOGGLE_BUTTON(gtk_builder_get_object(priv->builder, "radio_enable_trigger"));
	dev_info->channel_trigger_enabled = gtk_toggle_button_get_active(radio);

	if (!dev_info->channel_trigger_enabled)
		return;

	box = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(priv->builder, "comboboxtext_trigger_channel"));
	active_channel = gtk_combo_box_text_get_active_text(box);

	if (active_channel) {
		chn = iio_device_find_channel(dev, active_channel, false);
		dev_info->channel_trigger = iio_channel_get_index(chn);
	}

	radio = GTK_TOGGLE_BUTTON(gtk_builder_get_object(priv->builder, "radio_trigger_falling"));
	dev_info->trigger_falling_edge = gtk_toggle_button_get_active(radio);

	btn = GTK_SPIN_BUTTON(gtk_builder_get_object(
				priv->builder, "spin_trigger_value"));
	dev_info->trigger_value = gtk_spin_button_get_value(btn);

	if (active_channel)
		g_free(active_channel);
}

static void plot_trigger_settings_cb(GtkMenuItem *menuitem, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	struct iio_device *dev;
	struct extra_dev_info *dev_info;
	GtkDialog *dialog;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkComboBoxText *box;
	GtkWidget *item;
	GtkTreeIter iter, child_iter;
	GtkListStore *store;
	gboolean selected;
	bool box_has_channels = false;
	gchar *active_channel;
	unsigned cpt = 0;

	treeview = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(treeview);
	selected = tree_get_selected_row_iter(treeview, &iter);
	if (!selected)
		return;
	gtk_tree_model_get(model, &iter, ELEMENT_REFERENCE, &dev, -1);
	if (!dev) {
		fprintf(stderr, "Invalid reference of iio_device read from devicetree\n");
		return;
	}

	dev_info = iio_device_get_data(dev);

	box = GTK_COMBO_BOX_TEXT(gtk_builder_get_object(priv->builder, "comboboxtext_trigger_channel"));
	gtk_combo_box_set_active(GTK_COMBO_BOX(box), dev_info->channel_trigger);
	active_channel = gtk_combo_box_text_get_active_text(box);

	/* This code empties the GtkComboBoxText (as it may contain channels
	 * that have been de-selected); the do-while loop will re-insert only
	 * the names of the enabled channels. */
	store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(box)));
	gtk_list_store_clear(store);

	gtk_tree_model_iter_children(model, &child_iter, &iter);
	do {
		gboolean enabled;
		struct iio_channel *ch;
		const char *name;

		gtk_tree_model_get(model, &child_iter,
				CHANNEL_ACTIVE, &enabled, -1);
		if (!enabled)
			continue;

		gtk_tree_model_get(model, &child_iter,
				ELEMENT_REFERENCE, &ch, -1);
		name = iio_channel_get_name(ch) ?: iio_channel_get_id(ch);
		gtk_combo_box_text_append_text(box, name);
		box_has_channels = true;

		if (active_channel && !strcmp(name, active_channel))
			dev_info->channel_trigger = cpt;
		cpt++;
	} while (gtk_tree_model_iter_next(model, &child_iter));

	gtk_widget_set_sensitive(GTK_WIDGET(box), box_has_channels);

	if (box_has_channels)
		gtk_combo_box_set_active(GTK_COMBO_BOX(box), dev_info->channel_trigger);
	else
		dev_info->channel_trigger_enabled = FALSE;

	item = GTK_WIDGET(gtk_builder_get_object(priv->builder, "radio_disable_trigger"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(item),
			!dev_info->channel_trigger_enabled);

	item = GTK_WIDGET(gtk_builder_get_object(priv->builder, "radio_enable_trigger"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(item),
			dev_info->channel_trigger_enabled);
	gtk_widget_set_sensitive(item, box_has_channels);

	item = GTK_WIDGET(gtk_builder_get_object(priv->builder, "radio_trigger_falling"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(item), dev_info->trigger_falling_edge);

	item = GTK_WIDGET(gtk_builder_get_object(priv->builder, "spin_trigger_value"));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(item), dev_info->trigger_value);

	dialog = GTK_DIALOG(gtk_builder_get_object(priv->builder, "channel_trigger_dialog"));
	switch (gtk_dialog_run(dialog)) {
	case GTK_RESPONSE_CANCEL:
		break;
	case GTK_RESPONSE_OK:
		plot_trigger_save_settings(priv, dev);
		fallthrough;
	default:
		break;
	}

	if (active_channel)
		g_free(active_channel);
	gtk_widget_hide(GTK_WIDGET(dialog));
}

static void channel_color_settings_cb(GtkMenuItem *menuitem, OscPlot *plot)
{
          // TO DO : update glade files to match new ColorChooserDialog
		  OscPlotPrivate *priv = plot->priv;
          PlotChn *settings;
          GtkWidget *color_dialog;
          //GtkWidget *colorsel;
          GdkRGBA *color;
          GtkTreeView *treeview;
          GtkTreeModel *model;
          GtkTreeIter iter;
          GdkPixbuf *color_icon;
          gboolean selected;
          gint response;

          treeview = GTK_TREE_VIEW(priv->channel_list_view);
          model = gtk_tree_view_get_model(treeview);
          selected = tree_get_selected_row_iter(treeview, &iter);
          if (!selected)
            return;
          gtk_tree_model_get(model, &iter, CHANNEL_SETTINGS, &settings,
                             CHANNEL_COLOR_ICON, &color_icon, -1);
          color = &settings->graph_color;

          color_dialog = gtk_color_chooser_dialog_new("Channel Graph Color Selection", NULL);

          response = gtk_dialog_run(GTK_DIALOG(color_dialog));
          gtk_widget_hide(color_dialog);
          if (response != GTK_RESPONSE_OK)
            return;
          gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(color_dialog), color);

          /* Change icon color */
          channel_color_icon_set_color(color_icon, color);

          gtk_widget_destroy(color_dialog);
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
	PlotIioChn *csettings;

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

static gboolean right_click_menu_show(OscPlot *plot, GdkEvent *event)
{
	OscPlotPrivate *priv = plot->priv;
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	GtkTreePath *path;
	gboolean is_device = false;
	gboolean is_channel = false;
	gpointer ref;
	gchar *element_name;
	int channel_type;
	char name[1024];
	GdkEventButton *event_button;

	treeview = GTK_TREE_VIEW(priv->channel_list_view);
	model = gtk_tree_view_get_model(treeview);
	selection = gtk_tree_view_get_selection(treeview);

	event_button = (GdkEventButton *)event;

	/* Get tree path for row that was clicked */
	if (!gtk_tree_view_get_path_at_pos(treeview,
				(gint) event_button->x, (gint) event_button->y,
				&path, NULL, NULL, NULL))
		return false;

	gtk_tree_selection_unselect_all(selection);
	gtk_tree_selection_select_path(selection, path);

	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter,
		ELEMENT_NAME, &element_name,
		ELEMENT_REFERENCE, &ref,
		IS_DEVICE, &is_device,
		IS_CHANNEL, &is_channel,
		CHANNEL_TYPE, &channel_type,
		-1);
	snprintf(name, sizeof(name), "%s", element_name);
	g_free(element_name);

	if (is_channel) {
		if (gtk_combo_box_get_active(GTK_COMBO_BOX(plot->priv->plot_domain)) != TIME_PLOT)
			return false;

		switch(channel_type) {
		case PLOT_IIO_CHANNEL:
			gtk_menu_popup_at_pointer(GTK_MENU(priv->channel_settings_menu), event);
			return true;
		case PLOT_MATH_CHANNEL:
			gtk_menu_popup_at_pointer(GTK_MENU(priv->math_channel_settings_menu), event);
			return true;
		}
	}

	if (is_device) {
		/* Check if device needs a trigger */
		struct iio_device *dev = ref;
		const struct iio_device *trigger;
		bool needs_trigger;
		int ret;

		ret = iio_device_get_trigger(dev, &trigger);
		needs_trigger = false;
		if (ret == 0) {
			needs_trigger = true;
		}

		gtk_widget_set_sensitive(priv->device_trigger_menuitem,
				needs_trigger);

		gtk_menu_popup_at_pointer(GTK_MENU(priv->device_settings_menu), event);
		return true;
	}

	if (!strncmp(name, MATH_CHANNELS_DEVICE, sizeof(MATH_CHANNELS_DEVICE))) {
		gtk_menu_popup_at_pointer(GTK_MENU(priv->math_settings_menu), event);
		return true;
	}

	return false;
}

static gboolean right_click_on_ch_list_cb(GtkTreeView *treeview, GdkEvent *event, OscPlot *plot)
{
	GdkEventButton *event_button = (GdkEventButton *)event;

	/* single click with the right mouse button */
	if (event_button->type == GDK_BUTTON_PRESS && event_button->button == 3)
		return right_click_menu_show(plot, event);

	return false;
}

static void menu_quit_cb(GtkMenuItem *menuitem, OscPlot *plot)
{
	osc_plot_destroy(plot);
}

static void quit_callback_default_cb(GtkMenuItem *menuitem, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	if (priv->quit_callback)
		priv->quit_callback(priv->qcb_user_data);
	else
		fprintf(stderr, "Plot %d does not have a quit callback!\n",
			priv->object_id);
}

static void menu_title_edit_cb(GtkMenuItem *menuitem, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	GtkEntry *title_entry;
	const gchar *title;
	gint response;

	response = gtk_dialog_run(GTK_DIALOG(priv->title_edit_dialog));
	if (response == GTK_RESPONSE_OK) {
		title_entry = GTK_ENTRY(gtk_builder_get_object(priv->builder, "title_entry"));
		title = gtk_entry_get_text(title_entry);
		gtk_window_set_title(GTK_WINDOW(priv->window), title);
	}

	gtk_widget_hide(plot->priv->title_edit_dialog);
}

static void show_capture_options_toggled_cb(GtkCheckMenuItem *menu_item, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	if (gtk_check_menu_item_get_active(menu_item)) {
		gtk_window_get_size(GTK_WINDOW(priv->window), &priv->size.width, &priv->size.height);
		gtk_widget_show(plot->priv->capture_options_box);
    } else {
        gtk_widget_hide(plot->priv->capture_options_box);
		gtk_window_resize(GTK_WINDOW(priv->window), priv->size.width, priv->size.height);
	}
}

static void fullscreen_changed_cb(GtkWidget *widget, OscPlot *plot)
{
        // TO DO: handle the fullscreen through the window manager
     OscPlotPrivate *priv = plot->priv;
     //GtkWidget *img;


     if (priv->fullscreen_state) {
         gtk_window_unfullscreen(GTK_WINDOW(priv->window));
         gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(priv->fullscreen_button), "gtk-fullscreen");
         gtk_menu_item_set_label(GTK_MENU_ITEM(priv->menu_fullscreen), "Fullscreen");
     } else {
         gtk_window_fullscreen(GTK_WINDOW(priv->window));
         gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(priv->fullscreen_button), "gtk-leave-fullscreen");
         gtk_menu_item_set_label(GTK_MENU_ITEM(priv->menu_fullscreen), "Leave Fullscreen");
     }
}

static gboolean window_state_event_cb(GtkWidget *widget, GdkEventWindowState *event, OscPlot *plot)
{
	if (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN)
		plot->priv->fullscreen_state = true;
	else
		plot->priv->fullscreen_state = false;

	return FALSE;
}

static void capture_window_realize_cb(GtkWidget *widget, OscPlot *plot)
{
	gtk_window_get_size(GTK_WINDOW(plot->priv->window),
		&plot->priv->size.width, &plot->priv->size.height);
}

static bool set_channel_state_in_tree_model(GtkTreeModel *model, GtkTreeIter* chn_iter, gboolean state)
{
	gboolean sensitive;

	gtk_tree_model_get(model, chn_iter, SENSITIVE, &sensitive, -1);

	if (sensitive) {
		gtk_tree_store_set(GTK_TREE_STORE(model), chn_iter, CHANNEL_ACTIVE, state, -1);
		return true;
	}

	return false;
}

static void set_channel_state_via_iter(GtkTreeModel *model,
	GtkTreeIter *iter, void *user_data)
{
	gboolean enable_state = *(gboolean *)user_data;
	set_channel_state_in_tree_model(model, iter, enable_state);
}

static void enable_all_button_toggled_cb(GtkToggleButton *btn, OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;
	gboolean toggled = gtk_toggle_button_get_active(btn);

	if (toggled) {
		gtk_button_set_label(GTK_BUTTON(btn), "Disable All");
	} else {
		gtk_button_set_label(GTK_BUTTON(btn), "Enable All");
	}

	// Enable/disable by going through all channels of each device
	GtkTreeView *treeview = GTK_TREE_VIEW(priv->channel_list_view);
	GtkTreeModel *model;
	GtkTreeIter iter;
	gboolean next_iter;
	gchar *dev_name;

	model = gtk_tree_view_get_model(treeview);
	next_iter = gtk_tree_model_get_iter_first(model, &iter);
	while (next_iter) {
		gtk_tree_model_get(model, &iter,
				ELEMENT_NAME, &dev_name, -1);
		foreach_channel_iter_of_device(treeview, dev_name,
				*set_channel_state_via_iter, &toggled);
		g_free(dev_name);
		next_iter = gtk_tree_model_iter_next(model, &iter);
	}
	check_valid_setup(plot);
}

static GtkWidget * create_menuitem_with_label_and_icon(const gchar *label_text,
	const gchar *icon_name)
{
	GtkWidget *menu_item = gtk_menu_item_new();
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
	GtkWidget *image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
	GtkWidget *label = gtk_accel_label_new(label_text);

	gtk_container_add(GTK_CONTAINER(box), image);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_box_pack_end(GTK_BOX(box), label, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(menu_item), box);

	return menu_item;
}

static void create_plot(OscPlot *plot)
{
	OscPlotPrivate *priv = plot->priv;

	GtkWidget *table;
	GtkWidget *tmp;
	GtkBuilder *builder;
	GtkTreeSelection *tree_selection;
	GtkDataboxRuler *ruler_y;
	GtkTreeStore *tree_store;
	GtkStyleContext *style_context;
	GdkDisplay *display;
	GdkScreen *screen;
	GError *err = NULL;

	char buf[50];
	int i;

	/* Get the GUI from a glade file. */
	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file(builder, "./glade/oscplot.glade", NULL))
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
	priv->ss_button = GTK_WIDGET(gtk_builder_get_object(builder, "single_shot_button"));
	priv->channel_list_view = GTK_WIDGET(gtk_builder_get_object(builder, "channel_list_view"));
	priv->show_grid = GTK_WIDGET(gtk_builder_get_object(builder, "show_grid"));
	priv->plot_domain = GTK_WIDGET(gtk_builder_get_object(builder, "capture_domain"));
	priv->plot_type = GTK_WIDGET(gtk_builder_get_object(builder, "plot_type"));
	priv->enable_auto_scale = GTK_WIDGET(gtk_builder_get_object(builder, "auto_scale"));
	priv->hor_scale = GTK_WIDGET(gtk_builder_get_object(builder, "hor_scale"));
	priv->hor_units =  GTK_WIDGET(gtk_builder_get_object(builder, "sample_count_units"));
	priv->marker_label = GTK_WIDGET(gtk_builder_get_object(builder, "marker_info"));
	priv->devices_label = GTK_WIDGET(gtk_builder_get_object(builder, "device_info"));
	priv->phase_label = GTK_WIDGET(gtk_builder_get_object(builder, "phase_info"));
	priv->saveas_button = GTK_WIDGET(gtk_builder_get_object(builder, "save_as"));
	priv->saveas_dialog = GTK_WIDGET(gtk_builder_get_object(builder, "saveas_dialog"));
	priv->title_edit_dialog = GTK_WIDGET(gtk_builder_get_object(builder, "dialog_plot_title_edit"));
	priv->fullscreen_button = GTK_WIDGET(gtk_builder_get_object(builder, "fullscreen"));
	priv->menu_fullscreen = GTK_WIDGET(gtk_builder_get_object(builder, "menuitem_fullscreen"));
	priv->menu_show_options = GTK_WIDGET(gtk_builder_get_object(builder, "menuitem_show_options"));
	priv->y_axis_max = GTK_WIDGET(gtk_builder_get_object(builder, "spin_Y_max"));
	priv->y_axis_min = GTK_WIDGET(gtk_builder_get_object(builder, "spin_Y_min"));
	priv->viewport_saveas_channels = GTK_WIDGET(gtk_builder_get_object(builder, "saveas_channels_container"));
	priv->saveas_select_channel_message = GTK_WIDGET(gtk_builder_get_object(builder, "hbox_ch_sel_label"));
	priv->sample_count_widget = GTK_WIDGET(gtk_builder_get_object(builder, "sample_count"));
	priv->fft_size_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size"));
	priv->fft_win_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_win"));
	priv->fft_win_correction = GTK_WIDGET(gtk_builder_get_object(builder, "fft_win_correction"));
	priv->fft_avg_widget = GTK_WIDGET(gtk_builder_get_object(builder, "fft_avg"));
	priv->fft_pwr_offset_widget = GTK_WIDGET(gtk_builder_get_object(builder, "pwr_offset"));
	priv->math_dialog = GTK_WIDGET(gtk_builder_get_object(builder, "dialog_math_settings"));
	priv->capture_options_box = GTK_WIDGET(gtk_builder_get_object(builder, "box_capture_options"));
	priv->saveas_settings_box = GTK_WIDGET(gtk_builder_get_object(builder, "vbox_saveas_settings"));
	priv->save_mat_scale = GTK_WIDGET(gtk_builder_get_object(builder, "save_mat_scale"));
	priv->new_plot_button = GTK_WIDGET(gtk_builder_get_object(builder, "toolbutton_new_plot"));
	priv->cmb_saveas_type = GTK_WIDGET(gtk_builder_get_object(priv->builder, "save_formats"));
	priv->math_expression_dialog = GTK_WIDGET(gtk_builder_get_object(priv->builder, "math_expression_chooser"));
	priv->math_expression_textview = GTK_WIDGET(gtk_builder_get_object(priv->builder, "textview_math_expression"));
	priv->math_expression = GTK_TEXT_BUFFER(gtk_builder_get_object(priv->builder, "textbuffer_math_expression"));
	priv->math_channel_name_entry = GTK_WIDGET(gtk_builder_get_object(priv->builder, "entry_math_ch_name"));
	priv->math_expr_error = GTK_WIDGET(gtk_builder_get_object(priv->builder, "label_math_expr_invalid_msg"));

	priv->tbuf = NULL;
	priv->ch_settings_list = NULL;

	/* Count every object that is being created */
	object_count++;
	priv->object_id = object_count;

	/* Set a different title for every plot */
	snprintf(buf, sizeof(buf), "ADI IIO Oscilloscope - Capture%d", priv->object_id);
	gtk_window_set_title(GTK_WINDOW(priv->window), buf);

	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_domain), TIME_PLOT);
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_type), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->cmb_saveas_type), SAVE_CSV);

	/* Create a empty list of transforms */
	plot->priv->transform_list = TrList_new();

	/* No active transforms by default */
	plot->priv->active_transform_type = NO_TRANSFORM_TYPE;

	/* Create a GtkDatabox widget along with scrollbars and rulers */
	gtk_databox_create_box_with_scrollbars_and_rulers(&priv->databox, &table,
		TRUE, TRUE, TRUE, TRUE);
	gtk_box_pack_start(GTK_BOX(priv->capture_graph), table, TRUE, TRUE, 0);

	plot->priv->provider = gtk_css_provider_new();
	display = gdk_display_get_default();
	screen = gdk_display_get_default_screen (display);

	gtk_css_provider_load_from_path(GTK_CSS_PROVIDER(plot->priv->provider),"./styles.css",&err);
	if ( err ) {
		g_error_free(err);
		gtk_css_provider_load_from_path(GTK_CSS_PROVIDER(plot->priv->provider),OSC_STYLE_FILE_PATH"styles.css",NULL);
	}
	
	//gtk_css_provider_load_from_path(GTK_CSS_PROVIDER(plot->priv->provider),"styles.css",NULL);
	gtk_style_context_add_provider_for_screen (screen, GTK_STYLE_PROVIDER(plot->priv->provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	style_context = gtk_widget_get_style_context(GTK_WIDGET(priv->databox));
	gtk_style_context_add_class(style_context,"data_box");
	gtk_widget_set_size_request(table, 320, 240);
	ruler_y = gtk_databox_get_ruler_y(GTK_DATABOX(priv->databox));

	/* Create a Tree Store that holds information about devices */
	tree_store = gtk_tree_store_new(NUM_COL,
					G_TYPE_STRING,    /* ELEMENT_NAME */
					G_TYPE_BOOLEAN,   /* IS_DEVICE */
					G_TYPE_BOOLEAN,   /* IS_CHANNEL */
					G_TYPE_INT,       /* CHANNEL_TYPE */
					G_TYPE_BOOLEAN,   /* DEVICE_SELECTABLE */
					G_TYPE_BOOLEAN,   /* DEVICE_ACTIVE */
					G_TYPE_BOOLEAN,   /* CHANNEL_ACTIVE */
					G_TYPE_POINTER,   /* ELEMENT_REFERENCE */
					G_TYPE_BOOLEAN,   /* EXPANDED */
					G_TYPE_POINTER,   /* CHANNEL_SETTINGS */
					GDK_TYPE_PIXBUF,  /* CHANNEL_COLOR_ICON */
					G_TYPE_INT ,      /* PLOT_TYPE */
					G_TYPE_BOOLEAN);  /* SENSITIVE */
	gtk_tree_view_set_model((GtkTreeView *)priv->channel_list_view, (GtkTreeModel *)tree_store);

	/* Create Device Settings Menu */

	priv->device_settings_menu = gtk_menu_new();

	priv->device_trigger_menuitem =
		create_menuitem_with_label_and_icon("Impulse Generator", "preferences-system");
	gtk_menu_shell_append(GTK_MENU_SHELL(priv->device_settings_menu),
		priv->device_trigger_menuitem);

	priv->plot_trigger_menuitem =
		create_menuitem_with_label_and_icon("Trigger settings", "preferences-system");
	gtk_menu_shell_append(GTK_MENU_SHELL(priv->device_settings_menu),
		priv->plot_trigger_menuitem);

	gtk_widget_show_all(priv->device_settings_menu);

	/* Create Settings Menu for 'Math' */

	priv->math_settings_menu = gtk_menu_new();

	priv->math_menuitem =
		create_menuitem_with_label_and_icon("New Channel", "list-add");
	gtk_menu_shell_append(GTK_MENU_SHELL(priv->math_settings_menu),
		priv->math_menuitem);

	gtk_widget_show_all(priv->math_settings_menu);

	/* Create Channel Settings Menu */

	priv->channel_settings_menu = gtk_menu_new();

	priv->channel_iio_color_menuitem =
		create_menuitem_with_label_and_icon("Color Selection", "gtk-select-color");
	gtk_menu_shell_append(GTK_MENU_SHELL(priv->channel_settings_menu),
		priv->channel_iio_color_menuitem);

	priv->channel_math_menuitem =
		create_menuitem_with_label_and_icon("Math Settings", "preferences-system");
	gtk_menu_shell_append(GTK_MENU_SHELL(priv->channel_settings_menu),
		priv->channel_math_menuitem);
	gtk_widget_show_all(priv->channel_settings_menu);

	/* Create Math Channel Settings Menu */
	priv->math_channel_settings_menu = gtk_menu_new();

	priv->channel_expression_edit_menuitem =
		create_menuitem_with_label_and_icon("Edit Expression", "gtk-edit");
	gtk_menu_shell_append(GTK_MENU_SHELL(priv->math_channel_settings_menu),
		priv->channel_expression_edit_menuitem);

	priv->channel_math_color_menuitem =
		create_menuitem_with_label_and_icon("Color Selection", "gtk-select-color");
	gtk_menu_shell_append(GTK_MENU_SHELL(priv->math_channel_settings_menu),
		priv->channel_math_color_menuitem);

	priv->channel_remove_menuitem =
		create_menuitem_with_label_and_icon("Remove", "_Remove");
	gtk_menu_shell_append(GTK_MENU_SHELL(priv->math_channel_settings_menu),
		priv->channel_remove_menuitem);
	gtk_widget_show_all(priv->math_channel_settings_menu);

	gtk_box_pack_start(GTK_BOX(gtk_builder_get_object(builder, "buttons_separator_box")),
			gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, TRUE, 0);

	/* Create application's treeviews */
	device_list_treeview_init(plot);
	saveas_channels_list_fill(plot);

	/* Initialize text view for Devices Info */
	priv->devices_buf = gtk_text_buffer_new(NULL);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(priv->devices_label), priv->devices_buf);

	/* Initialize text view for Phase Info */
	priv->phase_buf = gtk_text_buffer_new(NULL);
	gtk_text_view_set_buffer(GTK_TEXT_VIEW(priv->phase_label), priv->phase_buf);

	/* Initialize Impulse Generators (triggers) dialog */
	trigger_dialog_init(builder);

	/* Add a device chooser to the Math Expression Chooser */
	priv->math_device_select = GTK_WIDGET(gtk_builder_get_object(priv->builder, "cmb_math_device_chooser"));
	comboboxtext_input_devices_fill(priv->ctx, GTK_COMBO_BOX_TEXT(priv->math_device_select));
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->math_device_select), 0);

	notebook_info_set_page_visibility(GTK_NOTEBOOK(
		gtk_builder_get_object(priv->builder, "notebook_info")),
		2, false);

	/* Connect Signals */
	g_signal_connect(G_OBJECT(priv->window), "destroy", G_CALLBACK(plot_destroyed), plot);

	g_signal_connect(G_OBJECT(priv->window), "window-state-event",
		G_CALLBACK(window_state_event_cb), plot);
	g_signal_connect(G_OBJECT(priv->window), "realize",
		G_CALLBACK(capture_window_realize_cb), plot);

	priv->capture_button_hid =
	g_signal_connect(priv->capture_button, "toggled",
		G_CALLBACK(capture_button_clicked_cb), plot);
	g_signal_connect(priv->ss_button, "clicked",
		G_CALLBACK(single_shot_clicked_cb), plot);
	g_signal_connect(priv->plot_domain, "changed",
		G_CALLBACK(plot_domain_changed_cb), plot);
	g_signal_connect(priv->saveas_button, "clicked",
		G_CALLBACK(saveas_dialog_show), plot);
	g_signal_connect(priv->saveas_dialog, "response",
		G_CALLBACK(cb_saveas_response), plot);
	g_signal_connect(priv->saveas_dialog, "delete-event",
		G_CALLBACK(gtk_widget_hide_on_delete), plot);
	g_signal_connect(priv->fullscreen_button, "clicked",
		G_CALLBACK(fullscreen_changed_cb), plot);
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
	g_signal_connect(priv->new_plot_button, "clicked",
		G_CALLBACK(new_plot_button_clicked_cb), plot);

	g_signal_connect(priv->channel_list_view, "button-press-event",
		G_CALLBACK(right_click_on_ch_list_cb), plot);
	g_signal_connect(priv->device_trigger_menuitem, "activate",
		G_CALLBACK(device_trigger_settings_cb), plot);
	g_signal_connect(priv->math_menuitem, "activate",
		G_CALLBACK(new_math_channel_cb), plot);
	g_signal_connect(priv->plot_trigger_menuitem, "activate",
		G_CALLBACK(plot_trigger_settings_cb), plot);
	g_signal_connect(priv->channel_iio_color_menuitem, "activate",
		G_CALLBACK(channel_color_settings_cb), plot);
	g_signal_connect(priv->channel_math_color_menuitem, "activate",
		G_CALLBACK(channel_color_settings_cb), plot);
	g_signal_connect(priv->channel_math_menuitem, "activate",
		G_CALLBACK(channel_math_settings_cb), plot);
	g_signal_connect(priv->channel_remove_menuitem, "activate",
		G_CALLBACK(plot_channel_remove_cb), plot);
	g_signal_connect(priv->channel_expression_edit_menuitem, "activate",
		G_CALLBACK(channel_edit_settings_cb), plot);

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

	g_builder_connect_signal(builder, "menuitem_save_as", "activate",
		G_CALLBACK(saveas_dialog_show), plot);

	g_builder_connect_signal(builder, "menuitem_close", "activate",
		G_CALLBACK(menu_quit_cb), plot);

	g_builder_connect_signal(builder, "menuitem_quit", "activate",
		G_CALLBACK(quit_callback_default_cb), plot);

	g_builder_connect_signal(builder, "menuitem_window_title", "activate",
		G_CALLBACK(menu_title_edit_cb), plot);

	g_builder_connect_signal(builder, "menuitem_show_options", "toggled",
		G_CALLBACK(show_capture_options_toggled_cb), plot);

	g_builder_connect_signal(builder, "menuitem_fullscreen", "activate",
		G_CALLBACK(fullscreen_changed_cb), plot);

	g_builder_connect_signal(builder, "cmb_math_device_chooser", "changed",
		G_CALLBACK(math_device_cmb_changed_cb), plot);

	g_builder_connect_signal(builder, "math_key_clear", "clicked",
		G_CALLBACK(math_chooser_clear_key_pressed_cb), plot);
	g_builder_connect_signal(builder, "math_key_backspace", "clicked",
		G_CALLBACK(math_chooser_backspace_key_pressed_cb), plot);

	GtkWidget *math_table = GTK_WIDGET(gtk_builder_get_object(priv->builder, "table_math_chooser"));
	GtkWidget *key_fullscale = GTK_WIDGET(gtk_builder_get_object(priv->builder, "math_key_full_scale"));
	GList *node;

	for (node = gtk_container_get_children(GTK_CONTAINER(math_table));
		node; node = g_list_next(node)) {
		g_signal_connect(node->data, "clicked",
			G_CALLBACK(math_chooser_key_pressed_cb), plot);
	}
	g_signal_handlers_disconnect_by_func(key_fullscale, math_chooser_key_pressed_cb, plot);
	g_signal_connect(key_fullscale, "clicked",
			G_CALLBACK(math_chooser_fullscale_key_pressed_cb), plot);

	g_builder_connect_signal(builder, "togglebutton_enable_all", "toggled",
		G_CALLBACK(enable_all_button_toggled_cb), plot);

	/* Create Bindings */
	g_object_bind_property_full(priv->capture_button, "active", priv->capture_button,
		"stock-id", 0, capture_button_icon_transform, NULL, plot, NULL);
	g_object_bind_property(priv->capture_button, "tooltip-text",
		priv->ss_button, "tooltip-text", G_BINDING_DEFAULT);
	g_object_bind_property(priv->y_axis_max, "value", ruler_y, "lower", G_BINDING_DEFAULT);
	g_object_bind_property(priv->y_axis_min, "value", ruler_y, "upper", G_BINDING_DEFAULT);

	g_builder_bind_property(builder, "capture_button", "active",
		"channel_list_view", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
		"capture_domain", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
		"fft_size", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
		"fft_win_correction", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
		"fft_win", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
		"plot_type", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
		"sample_count", "sensitive", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "capture_button", "active",
		"plot_units_container", "sensitive", G_BINDING_INVERT_BOOLEAN);

	/* in autoscale mode, don't display the scales */
	g_builder_bind_property(builder, "auto_scale", "active",
		"labelYMax", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "auto_scale", "active",
		"spin_Y_max", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "auto_scale", "active",
		"labelYMin", "visible", G_BINDING_INVERT_BOOLEAN);
	g_builder_bind_property(builder, "auto_scale", "active",
		"spin_Y_min", "visible", G_BINDING_INVERT_BOOLEAN);

	/* Bind the plot domain to the sensitivity of the sample count and
	 * FFT size widgets */
	 tmp = GTK_WIDGET(gtk_builder_get_object(builder, "fft_size_label"));
	 g_object_bind_property_full(priv->plot_domain, "active", tmp, "visible",
		0, domain_is_fft, NULL, plot, NULL);
	 g_object_bind_property_full(priv->plot_domain, "active", priv->fft_size_widget, "visible",
		0, domain_is_fft, NULL, NULL, NULL);

	 tmp = GTK_WIDGET(gtk_builder_get_object(builder, "fft_win_label"));
	 g_object_bind_property_full(priv->plot_domain, "active", tmp, "visible",
		0, domain_is_fft, NULL, plot, NULL);
	 g_object_bind_property_full(priv->plot_domain, "active", priv->fft_win_widget, "visible",
		0, domain_is_fft, NULL, NULL, NULL);

	 tmp = GTK_WIDGET(gtk_builder_get_object(builder, "fft_win_cor_lbl"));
	 g_object_bind_property_full(priv->plot_domain, "active", tmp, "visible",
		0, domain_is_fft, NULL, plot, NULL);
	 g_object_bind_property_full(priv->plot_domain, "active", priv->fft_win_correction, "visible",
		0, domain_is_fft, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "fft_avg_label"));
	 g_object_bind_property_full(priv->plot_domain, "active", tmp, "visible",
		0, domain_is_xcorr_fft, NULL, NULL, NULL);
	 g_object_bind_property_full(priv->plot_domain, "active", priv->fft_avg_widget, "visible",
		0, domain_is_xcorr_fft, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "pwr_offset_label"));
	 g_object_bind_property_full(priv->plot_domain, "active", tmp, "visible",
		0, domain_is_fft, NULL, NULL, NULL);
	 g_object_bind_property_full(priv->plot_domain, "active", priv->fft_pwr_offset_widget, "visible",
		0, domain_is_fft, NULL, NULL, NULL);

	g_object_bind_property_full(priv->plot_domain, "active", priv->hor_units, "visible",
		0, domain_is_time, NULL, NULL, NULL);
	g_signal_connect(priv->hor_units, "changed", G_CALLBACK(units_changed_cb), plot);
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->hor_units), 0);

	 g_object_bind_property_full(priv->plot_domain, "active", priv->sample_count_widget, "visible",
		0, domain_is_time, NULL, NULL, NULL);

	tmp = GTK_WIDGET(gtk_builder_get_object(builder, "plot_type_label"));
	 g_object_bind_property_full(priv->plot_domain, "active", tmp, "visible",
		0, domain_is_time, NULL, NULL, NULL);
	 g_object_bind_property_full(priv->plot_domain, "active", priv->plot_type, "visible",
		0, domain_is_time, NULL, NULL, NULL);

	if (priv->preferences && priv->preferences->sample_count) {
		priv->sample_count = *priv->preferences->sample_count;
	} else {
		priv->sample_count = 400;
	}
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->sample_count_widget), priv->sample_count);
	g_signal_connect(priv->sample_count_widget, "value-changed", G_CALLBACK(count_changed_cb), plot);

	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->fft_size_widget), 2);
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->fft_win_widget), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(priv->plot_type), 0);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->y_axis_max), 1000);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->y_axis_min), -1000);
	tree_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->channel_list_view));
	gtk_tree_selection_set_mode(tree_selection, GTK_SELECTION_SINGLE);
	add_grid(plot);
	check_valid_setup(plot);
	g_mutex_init(&priv->g_marker_copy_lock);
	device_rx_info_update(priv);

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

	priv->line_thickness = 1;

	gtk_window_set_modal(GTK_WINDOW(priv->saveas_dialog), FALSE);
	gtk_widget_show_all(priv->capture_graph);
}
