#ifndef __OSC_PLOT__
#define __OSC_PLOT__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkwindow.h>

G_BEGIN_DECLS

#define OSC_PLOT_TYPE              (osc_plot_get_type())
#define OSC_PLOT(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj), OSC_PLOT_TYPE, OscPlot))
#define OSC_PLOT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass), OSC_PLOT_TYPE, OscPlotClass))
#define IS_OSC_PLOT(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj), OSC_PLOT_TYPE))
#define IS_OSC_PLOT_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass), OSC_PLOT_TYPE))

typedef struct _OscPlot            OscPlot;
typedef struct _OscPlotPrivate     OscPlotPrivate;
typedef struct _OscPlotClass       OscPlotClass;

struct _OscPlot
{
	GtkBin bin;
	
	OscPlotPrivate *priv;

	gfloat *x_axis_source;
};

struct _OscPlotClass
{
	GtkBinClass parent_class;
	
	void (* capture_event) (OscPlot *plot, gboolean start_event);
};

GType         osc_plot_get_type      (void);
GtkWidget*    osc_plot_new           (void);
void          osc_plot_update        (OscPlot *plot);

G_END_DECLS

#endif /* __OSC_PLOT__ */
