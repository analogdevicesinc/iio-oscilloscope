#ifndef __OSC_PREFERENCES_H__
#define __OSC_PREFERENCES_H__

#include <glib.h>

typedef struct osc_preferences OscPreferences;
typedef struct oscplot_preferences OscPlotPreferences;
typedef struct iio_device_preferences IioDevPreferences;
typedef struct iio_channel_preferences IioChnPreferences;

struct oscplot_preferences {
        unsigned int *sample_count;
        GList *devices_pref_list;
};

struct iio_channel_preferences {
        char *chn_name;
        unsigned int *constraints;
};

struct iio_device_preferences {
        char *dev_name;
        double *fft_correction;
        GList *channels_pref_list;
};

struct osc_preferences {
        struct oscplot_preferences *plot_preferences;
};


/* Construct/Destruct methods */

IioChnPreferences* iio_chn_preferences_new(const char *chn_name);
void iio_chn_preferences_free(IioChnPreferences *pref);

IioDevPreferences* iio_dev_preferences_new(const char *dev_name);
void iio_dev_preferences_free(IioDevPreferences *pref);

OscPlotPreferences* osc_plot_preferences_new();
void osc_plot_preferences_free(OscPlotPreferences *pref);

OscPreferences* osc_preferences_new();
void osc_preferences_delete(OscPreferences *pref);

#endif /* __OSC_PREFERENCES_H__ */
