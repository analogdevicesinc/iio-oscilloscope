/*
 * XCOMM on-board calibration EEPROM definitions.
 *
 * Copyright 2012-2014 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef XCOMM_EEPROM_H_
#define XCOMM_EEPROM_H_

#define FAB_SIZE_FRU_EEPROM 256

#define CURRENT_VERSION 1
#define MAX_SIZE_CAL_EEPROM	254
#define FAB_SIZE_CAL_EEPROM	256
#define NEXT_TERMINATION	0

#define ADI_MAGIC_0	'A'
#define ADI_MAGIC_1	'D'
#define ADI_VERSION(v)	('0' + (v))

const char *find_eeprom(const char *path);

/* Version 0 */

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

/* Version 1 */

struct fmcomms1_calib_header_v1 {
		char adi_magic0;
		char adi_magic1;
		char version;
		unsigned char num_entries;
		unsigned short temp_calibbias;
} __attribute__((packed));

struct fmcomms1_calib_data_v1 {
		unsigned short cal_frequency_MHz;

		/* DAC Calibration Data */
                short i_phase_adj;		/* 10-bit */
                short q_phase_adj;		/* 10-bit */
                short i_dac_offset;		/* 16-bit */
                short q_dac_offset;		/* 16-bit */
                unsigned short i_dac_fs_adj;	/* 10-bit */
                unsigned short q_dac_fs_adj;	/* 10-bit */
                /* ADC Calibration Data */
                short i_adc_offset_adj;		/* 16-bit signed */
                unsigned short i_adc_gain_adj;	/* 16-bit fract 1.1.14 */
                unsigned short i_adc_phase_adj;	/* 16-bit fract 1.1.14 */
                short q_adc_offset_adj;		/* 16-bit signed */
                unsigned short q_adc_gain_adj;	/* 16-bit fract 1.1.14 */
} __attribute__((packed));

static inline unsigned short float_to_fract1_15(double val)
{
	unsigned long long llval;

	if (val < 0.0)
		val = 0.0;
	else if (val >= 2.0)
		val = 1.999999;

	val *= 1000000;
	llval = (unsigned long long)val * 0x8000UL;

	return (llval / 1000000);
}

static inline double fract1_15_to_float(unsigned short val)
{
	return (double)val / 0x8000;
}

static inline unsigned short float_to_fract1_1_14(double val)
{
	unsigned short fract;
	unsigned long long llval;

	if (val < 0.000000) {
		fract = 0x8000;
		val *= -1.0;
	} else {
		fract = 0;
	}

	if (val >= 2.0)
		val = 1.999999;

	val *= 1000000;

	llval = (unsigned long long)val * 0x4000UL;

	fract |= (llval / 1000000);

	return fract;
}

static inline double fract1_1_14_to_float(unsigned short val)
{
	double ret = (double)(val & 0x7FFF) / 0x4000UL;

	if (val & 0x8000)
		return -ret;

	return ret;
}

#endif /* XCOMM_EEPROM_H_ */
