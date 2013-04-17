/*
 * XCOMM on-board calibration EEPROM definitions.
 *
 * Copyright 2012 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef XCOMM_EEPROM_H_
#define XCOMM_EEPROM_H_

#define FAB_SIZE_FRU_EEPROM 256

#define CURRENT_VERSION 0
#define MAX_SIZE_CAL_EEPROM	254
#define FAB_SIZE_CAL_EEPROM	256
#define NEXT_TERMINATION	0

#define ADI_MAGIC_0	'A'
#define ADI_MAGIC_1	'D'
#define ADI_VERSION(v)	('0' + (v))


struct fmcomms1_calib_data {
		char adi_magic0;
		char adi_magic1;
		char version;
		unsigned char next;
		unsigned short cal_frequency_MHz;

		/* DAC Calibration Data */
                short i_phase_adj;      /* 10-bit */
                short q_phase_adj;     /* 10-bit */
                unsigned short i_dac_offset;      /* 16-bit */
                unsigned short q_dac_offset;    /* 16-bit */
                unsigned short i_dac_fs_adj;      /* 10-bit */
                unsigned short q_dac_fs_adj;    /* 10-bit */
                /* ADC Calibration Data */
                short i_adc_offset_adj;                 /* 16-bit signed */
                unsigned short i_adc_gain_adj; /* 16-bit fract 1.15 */
                short q_adc_offset_adj;                                /* 16-bit signed */
                unsigned short q_adc_gain_adj;               /* 16-bit fract 1.15 */
} __attribute__((packed));

#endif /* XCOMM_EEPROM_H_ */
