/**
 * Copyright (C) 2015 Analog Devices, Inc.
 *
 */

#include "ad9361_multichip_sync.h"

int ad9361_multichip_sync(struct iio_device **devices, int num, unsigned flags)
{
	char ensm_mode[MAX_AD9361_SYNC_DEVS][20];
	int i, step;

	if (num > MAX_AD9361_SYNC_DEVS || num < 2)
		return -EINVAL;

	if (flags & CHECK_SAMPLE_RATES) {
		struct iio_channel *tx_sample_master, *tx_sample_slave;
		long long tx_sample_master_freq, tx_sample_slave_freq;

		tx_sample_master = iio_device_find_channel(devices[0], "voltage0", true);
		iio_channel_attr_read_longlong(tx_sample_master, "sampling_frequency", &tx_sample_master_freq);

		for (i = 1; i < num; i++) {
			tx_sample_slave = iio_device_find_channel(devices[i], "voltage0", true);
			if (tx_sample_slave == NULL)
				return -ENODEV;
			iio_channel_attr_read_longlong(tx_sample_slave, "sampling_frequency", &tx_sample_slave_freq);

			if (tx_sample_master_freq != tx_sample_slave_freq) {
				fprintf(stderr, "tx_sample_master_freq != tx_sample_slave_freq\nUpdating...\n");
				iio_channel_attr_write_longlong(tx_sample_slave, "sampling_frequency", tx_sample_master_freq);
			}
		}
	}

	if (flags & FIXUP_INTERFACE_TIMING) {
		unsigned tmp, tmp2;
		iio_device_reg_read(devices[0], 0x6, &tmp);
		iio_device_reg_read(devices[0], 0x7, &tmp2);

		for (i = 1; i < num; i++) {
			iio_device_reg_write(devices[i], 0x6, tmp);
			iio_device_reg_write(devices[i], 0x7, tmp2);
		}
	}

	/* Move the parts int ALERT for MCS */
	for (i = 0; i < num; i++) {
		iio_device_attr_read(devices[i], "ensm_mode", ensm_mode[i], sizeof(ensm_mode));
		iio_device_attr_write(devices[i], "ensm_mode", "alert");
	}

	for (step = 0; step <= 5; step++) {
		char temp[20];
		sprintf(temp, "%d", step);
		/* Don't change the order here - the master controls the SYNC GPIO */
		for (i = num; i > 0; i--) {
			if (flags & MCS_IS_DEBUG_ATTR)
				iio_device_debug_attr_write(devices[i - 1], "multichip_sync", temp);
			else
				iio_device_attr_write(devices[i - 1], "multichip_sync", temp);
		}
		sleep(0.1);
	}

	for (i = 0; i < num; i++)
		iio_device_attr_write(devices[i], "ensm_mode", ensm_mode[i]);

	return 0;
}
