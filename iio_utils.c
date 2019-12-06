#include "iio_utils.h"

#include <string.h>
#include <ctype.h>

static gint iio_chn_cmp_by_name(gconstpointer ptr_a, gconstpointer ptr_b)
{
	struct iio_channel *ch_a = *(struct iio_channel **)ptr_a;
	struct iio_channel *ch_b = *(struct iio_channel **)ptr_b;

	g_return_val_if_fail(ch_a, 0);
	g_return_val_if_fail(ch_b, 0);

	const char *name_a = iio_channel_get_name(ch_a) ?: iio_channel_get_id(ch_a);
	const char *name_b = iio_channel_get_name(ch_b) ?: iio_channel_get_id(ch_b);

	g_return_val_if_fail(name_a, 0);
	g_return_val_if_fail(name_b, 0);

	return str_natural_cmp(name_a, name_b);
}

/*
 * Gets all devices that have their name starting with the given sequence.
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
/*
 * Gets all channels of the specified device sorted in a natural order.
 * Returns an array of 'struct iio_channel *' elements.
 */
GArray * get_iio_channels_naturally_sorted(struct iio_device *dev)
{
	GArray *channels = g_array_new(FALSE, TRUE, sizeof(struct iio_channel *));
	unsigned int i, nb_channels = iio_device_get_channels_count(dev);

	for (i = 0; i < nb_channels; ++i) {
		struct iio_channel *ch = iio_device_get_channel(dev, i);
		g_array_append_val(channels, ch);
	}

	g_array_sort(channels,	iio_chn_cmp_by_name);

	return channels;
}

/*
 * Compares strings naturally
 *
 * This function will compare strings naturally, this means when a number is
 * encountered in both strings at the same offset it is compared as a number
 * rather than comparing the individual digits.
 * This functions works with strings that they must be NULL terminated. Also the
 * stings must be valid (both s1 and s2 must not be NULL).
 *
 * This makes sure that e.g. in_voltage9 is placed before in_voltage10
 */
int str_natural_cmp(const char *s1, const char *s2)
{
	unsigned int n1, n2;
	unsigned int i1 = 0, i2 = 0;

	while (s1[i1] && s2[i2]) {
		if (isdigit(s1[i1]) && isdigit(s2[i2])) {
			n1 = 0;
			do {
				n1 = n1 * 10 + s1[i1] - '0';
				i1++;
			} while (isdigit(s1[i1]));

			n2 = 0;
			do {
				n2 = n2 * 10 + s2[i2] - '0';
				i2++;
			} while (isdigit(s2[i2]));

			if (n1 != n2)
				return n1 - n2;
		} else {
			if (s1[i1] != s2[i2])
				return s1[i1] - s2[i2];
			i1++;
			i2++;
		}
	}

	return 0;
}
