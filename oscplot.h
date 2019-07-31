/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#ifndef __OSC_PLOT__
#define __OSC_PLOT__

#include "osc_preferences.h"

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkwindow.h>

G_BEGIN_DECLS

#define OSC_PLOT_TYPE              (osc_plot_get_type())
#define OSC_PLOT(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj), OSC_PLOT_TYPE, OscPlot))
#define OSC_PLOT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass), OSC_PLOT_TYPE, OscPlotClass))
#define IS_OSC_PLOT(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj), OSC_PLOT_TYPE))
#define IS_OSC_PLOT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass), OSC_PLOT_TYPE))

typedef struct _OscPlot            OscPlot;
typedef struct _OscPlotPrivate     OscPlotPrivate;
typedef struct _OscPlotClass       OscPlotClass;

struct _OscPlot
{
	GtkWidget widget;
	OscPlotPrivate *priv;
};

struct _OscPlotClass
{
	GtkWidgetClass parent_class;

	void (* capture_event) (OscPlot *plot, gboolean start_event);
	void (* destroy_event) (OscPlot *plot);
	void (* newplot_event) (OscPlot *plot, OscPlot *new_plot);
};

GType         osc_plot_get_type         (void);
GtkWidget*    osc_plot_new              (struct iio_context *ctx);
GtkWidget*    osc_plot_new_with_pref    (struct iio_context *ctx, OscPlotPreferences* pref);
void          osc_plot_destroy          (OscPlot *plot);
void          osc_plot_set_visible      (OscPlot *plot, bool visible);
struct iio_buffer * osc_plot_get_buffer (OscPlot *plot);
void          osc_plot_data_update      (OscPlot *plot);
void          osc_plot_update_rx_lbl    (OscPlot *plot, bool initial_update);
void          osc_plot_restart          (OscPlot *plot);
bool          osc_plot_running_state    (OscPlot *plot);
void          osc_plot_draw_start       (OscPlot *plot);
void          osc_plot_draw_stop        (OscPlot *plot);
void          osc_plot_save_to_ini      (OscPlot *plot, char *filename);
int           osc_plot_ini_read_handler (OscPlot *plot, int line, const char *section, const char *name, const char *value);
void          osc_plot_save_as          (OscPlot *plot, char *filename, int type);
const char *  osc_plot_get_active_device(OscPlot *plot);
int           osc_plot_get_fft_avg      (OscPlot *plot);
int           osc_plot_get_marker_type  (OscPlot *plot);
void          osc_plot_set_marker_type  (OscPlot *plot, int mtype);
void *        osc_plot_get_markers_copy (OscPlot *plot);
void          osc_plot_set_markers_copy (OscPlot *plot, void *value);
void          osc_plot_set_domain       (OscPlot *plot, int domain);
int           osc_plot_get_plot_domain  (OscPlot *plot);
GMutex *      osc_plot_get_marker_lock  (OscPlot *plot);
bool          osc_plot_set_sample_count (OscPlot *plot, gdouble count);
double        osc_plot_get_sample_count (OscPlot *plot);
void          osc_plot_set_channel_state(OscPlot *plot, const char *dev, unsigned int channel, bool state);
void          osc_plot_xcorr_revert     (OscPlot *plot, int revert);
void          osc_plot_set_quit_callback(OscPlot *plot, void (*qcallback)(void *user_data), void *user_data);
void          osc_plot_reset_numbering  (void);
int           osc_plot_get_id           (OscPlot *plot);
void          osc_plot_set_id           (OscPlot *plot, int id);
void          osc_plot_spect_mode       (OscPlot *plot, bool enable);
void          osc_plot_spect_set_start_f(OscPlot *plot, double freq_mhz);
void          osc_plot_spect_set_len    (OscPlot *plot, unsigned fft_count);
void          osc_plot_spect_set_filter_bw(OscPlot *plot, double bw);

G_END_DECLS

#endif /* __OSC_PLOT__ */
