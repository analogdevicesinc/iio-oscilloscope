#include "iio_utils.h"
#include <string.h>

/* Gets all devices that have their name starting with the given sequence.
 * Returns an array of 'struct iio_device *' elements.
 */
GArray * get_iio_devices_starting_with(struct iio_context *ctx, const char *sequence)
{
        GArray *devices = g_array_new(FALSE, FALSE, sizeof(struct iio_devices *));
        size_t i = 0;

        for (; i < iio_context_get_devices_count(ctx); i++) {
                struct iio_device *dev = iio_context_get_device(ctx, i);
                const char *dev_name = iio_device_get_name(dev);
                if (dev_name && !strncmp(sequence, dev_name, strlen(sequence))) {
                        g_array_append_val(devices, dev);
                }
        }

        return devices;
}
