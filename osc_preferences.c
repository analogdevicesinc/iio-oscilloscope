#include "osc_preferences.h"

/* IioChnPreferences */

 IioChnPreferences* iio_chn_preferences_new(const char *chn_name)
{
        IioChnPreferences *chn_pref = g_new0(IioChnPreferences, 1);

        chn_pref->chn_name = g_strdup(chn_name);

        return chn_pref;
}

 void iio_chn_preferences_free(IioChnPreferences *pref)
{
        g_free(pref->chn_name);
        pref->chn_name = NULL;
}

/* IioDevPreferences */

 IioDevPreferences* iio_dev_preferences_new(const char *dev_name)
{
        IioDevPreferences *dev_pref = g_new0(IioDevPreferences, 1);

        dev_pref->dev_name = g_strdup(dev_name);

        return dev_pref;
}

 void iio_dev_preferences_free(IioDevPreferences *pref)
{
        g_free(pref->dev_name);
        pref->dev_name = NULL;

        g_free(pref->fft_correction);
        pref->fft_correction = NULL;

        g_list_free_full(pref->channels_pref_list,
                (GDestroyNotify)iio_chn_preferences_free);
}

/* OscPlotPreferences */

 OscPlotPreferences* osc_plot_preferences_new()
{
        return g_new0(OscPlotPreferences, 1);
}

 void osc_plot_preferences_free(OscPlotPreferences *pref)
{
        g_free(pref->sample_count);
        pref->sample_count = NULL;

        g_list_free_full(pref->devices_pref_list,
                (GDestroyNotify)iio_dev_preferences_free);
}

/* OscPreferences */

 OscPreferences* osc_preferences_new()
{
        return g_new0(OscPreferences, 1);
}

 void osc_preferences_delete(OscPreferences *pref)
{
        if (pref->plot_preferences) {
                osc_plot_preferences_free(pref->plot_preferences);
        }
}
