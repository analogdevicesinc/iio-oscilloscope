/*
 * fru.c
 * Copyright (C) 2012 Analog Devices
 * Author : Robin Getz <robin.getz@analog.com>
 *
 * fru-dump is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, (v2 only) as
 * published by the Free Software Foundation.
 *
 * fru-dump is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see http://www.gnu.org/licenses/gpl-2.0.html
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <limits.h>

#include "fru.h"

/*
 * This code is based from:
 * Platform Management FRU Information
 * Storage Definition
 * Document Revision 1.1, September 27, 1999
 * Available at zero cost at:
 * http://download.intel.com/design/servers/ipmi/FRU1011.pdf
 *
 * ANSI/VITA 57.1
 * FPGA Mezzanine Card (FMC) Standard
 * Approved July 2008 (Revised February 2010)
 * http://www.vita.com/fmc.html
 * Available for a small fee
 * https://vita.com/secure/online-store.html
 * used with permission
 * Most info is in section 5.5.1: IPMI Support
 */

#if CHAR_BIT != 8
#error "unsupported char size"
#endif

/*
 * FRU information uses a zero checksum everywhere.
 * The modulo 256 sum of the preceding bytes (starting with the first byte
 * of the header) plus the checksum byte equals zero.
 * Platform Management FRU Information Storage Definition : section 16.2.[56]
 */
unsigned char calc_zero_checksum (unsigned char *data, unsigned int len)
{
	int i;
	unsigned char tmp = 0;
	for (i = 0; i <= len; i++)
		tmp += data[i];

	return tmp;
}

/*
 * FRU Board Area Mfg. Date / Time is the
 * number of _minutes_ from 0:00 hrs 01Jan1996
 * Max is 0xFFFFFF (3 bytes worth) or 
 * 16777215 minutes; or 
 *   279620 hours, 15 minutes; or
 *   11650 days, 20 hours, 15 minutes; or
 *   31 years, 328 days, 7 hours, 56 minutes (assuming 525949 minutes in a year); or
 *   up to : Wed Nov 24 07:56 2027
 * See:
 *   section 11, Platform Management FRU Information Storage Definition
 */
time_t min2date(unsigned int mins)
{
	struct tm tm;
	time_t tmp;

	/* Set up 01-Jan-1996 , and add the number of minutes to it */
	memset(&tm, 0, sizeof(struct tm));
	tm.tm_year = 96;
	tm.tm_mday = 1;
	tm.tm_min += mins;
	tmp = mktime(&tm);
	return tmp;
}

#ifdef DEBUG
/*
 * Used for debugging
 */
void dump_str(unsigned char * p, unsigned int size, unsigned int space)
{
	size_t i, j = 0, k, m, shift;
	unsigned char *t;

	t = p;
	k = 8 - space;
	for (i = 0; i < size; i++) {
		m = 0;
		printf("%02zi: %02x : ", i, *t);
		for (shift = 0x80; shift > 0; shift >>= 1) {
			printf("%s", ((*t & shift) == shift) ? "1" : "0");
			j++, m++;
			if(k == m) {
				printf(" ");
				k = 8 - space + m;
				if (k >= 8)
					k -= space;
				m = space + 100;
			}
		}
		if (space == 8)
			printf(" (%c) %02x", *t, *t - 0x20);
		++t;
		printf("\n");
	}
}
#endif
/*
 * 6-bit ASCII Packing
 * Platform Management FRU Information Storage Definition:  Section 13.[23]
 */
unsigned int ascii2six(unsigned char *buf, size_t size, unsigned char **dest)
{
	size_t i = 0, j;
	unsigned int k, m = 0;
	unsigned char *p;

	if (!buf || !size)
		return 0;

	p = buf;
	/*
	 * 6-bit requires uppercase chars, between 0 and 0x3f
	 */
	while (*p != '\0' && i <= size) {
		j = toupper(*p) - 0x20;
		if (j < 0 || j >= 0x40)
			printf_err("%s trying to convert a string, which is not possible to be converted\n", __func__);
		*p = j;
		++p, i++;
	}
	/* dump_str(buf, size, 4); */

	/* the length of dest, should be 3/4 of size, we just leave it, so it's zero padded at the end */
	*dest = x_calloc(1, size);
	p = *dest;

	for (i = 0; i <= size ; i+= 4) {
		k = buf[i];
		m++;
		if ((i + 1) < size) {
			k |= buf[i + 1] << 6;
			m++;
		}
		if ((i + 2) < size) {
			k |= buf[i + 2] << 12;
			m++;
		}
		if ((i + 3) < size) {
			k |= buf[i + 3] << 18;
		}
#ifndef __MINGW32__
#if __BYTE_ORDER == __BIG_ENDIAN
		k = __bswap_32(k);
#endif
#endif
		memcpy(p, &k, 3);
		p += 3;
	}

	/* dump_str(*dest, m, 6); */

	return  m;
}

/*
 *  * 6-bit ASCII Unpacking
 *   * Platform Management FRU Information Storage Definition:  Section 13.[23]
 */
unsigned char * six2ascii(unsigned char *buf, size_t size)
{
	unsigned char *p, *dest;
	size_t i;

	/* the length of dest, should be 4/3 of size + 1 for null termination char*/
	dest = x_calloc(1, ((size * 4) / 3) + 2);
	p = dest;

	for (i = 0; i <= size; i += 3) {
		*dest = (buf[i] & 0x3F) + 0x20;
		/* printf("%i: 0x%x (%c)\n", i, *dest, *dest); */
		dest++;
		if ((i + 1) <= size) {
			*dest = ((buf[i] & 0xC0) >> 6 | (buf[i+1] & 0x0F) << 2) + 0x20;
			/* printf("%i: 0x%x (%c)\n", i, *dest, *dest); */
			dest++;
		}
		if ((i + 2) <= size) {
			*dest = ((buf[i+1] & 0xF0) >> 4 | (buf[i+2] & 0x03) << 4) + 0x20;
			/* printf("%i: 0x%x (%c)\n", i, *dest, *dest); */
			dest++;
		}
		if ((i + 3) <= size) {
			*dest = ((buf[i+2] & 0xFC) >> 2) + 0x20;
			/* printf("%i: 0x%x (%c)\n", i, *dest, *dest); */
			dest++;
		}
	}
	*dest = 0;

	/* Drop trailing spaces */
	dest--;
	while (*dest == ' ' && size) {
		*dest = 0;
		dest--;
		size--;
	}
	/* printf("%s\n",p); */
	return p;
}

/*
 * Extract strings from fields
 * Section 13 TYPE/LENGTH BYTE FORMAT
 * Platform Management FRU Information Storage Definition
 */
unsigned int parse_string(unsigned char *p, char **str, const char * field)
{
	size_t len = -1, i, j;

	len = p[0] & 0x3F;

	switch((p[0] >> 6) & 0x3 ) {
		case FRU_STRING_BINARY:
			/* binary or unspecified */
			*str = x_calloc(1, len + 2);
			memcpy(*str, p, len + 1);
			break;
		case FRU_STRING_BCD:
			/* BCD plus */
			printf_err("BCD - sorry\n");
			break;
		case FRU_STRING_SIXBIT:
			/* 6-bit ASCII, packed */
			{
				unsigned char *foo, bar;
				char *tmp;

				foo = six2ascii(&p[1], p[0]& 0x3F);
				/* printf("str: %s\n", foo); */
				bar = strlen((char *)foo);

				tmp = x_calloc(1, bar + 2);
				memcpy(tmp, &bar, 1);
				memcpy(&tmp[1], foo, bar);
				*str = x_calloc(1, bar + 2);
				memcpy(*str, tmp, bar + 2);

			}
			break;
		case FRU_STRING_ASCII:
			/* 8-bit ASCII */
			*str = x_calloc(1, len + 2);
			memcpy(*str, p, len + 1);
			for (i = 1; i < len; i++) {
				if (p[i] < 0x20 || p[i] == 0x7F) {
					printf_warn("Field '%s' marked as ASCII, but contains non-printable "
							"characters:\n", field);
					printf_warn("  Length/Type : 0x%x (length:%i; type:ASCII(%i)\n",
							p[0], p[0] & 0x3F, FRU_STRING_ASCII);
					printf_warn("  Contents : ");
					for (j = 1; j < len + 1; j++)
						printf_warn("0x%02x ", p[j]);
					printf_warn(" |");
					for (j = 0; j < len + 1; j++)
						printf_warn("%c", ((p[j] < 32) || (p[j] >= 127)) ? '.': p[j]);
					printf_warn("|\n");
				}
			}
			break;
	}
	return len + 1;
}

/*
 * Board Info Area Format
 * Platform Management FRU Information Storage Definition: Section 11
 */
struct BOARD_INFO * parse_board_area(unsigned char *data)
{

	struct BOARD_INFO *fru;
	unsigned char *p;
	unsigned int len, i, j;

	fru = x_calloc(1, sizeof(struct BOARD_INFO));

	if (data[0] != 0x01) {
		printf_err("Board Area Format Version mismatch: 0x%02x should be 0x01\n", data [0]);
		goto err;
	}

	len = (data[1] * 8) - 1;
	if (calc_zero_checksum(data, len)) {
		printf_err("Board Area Checksum failed");
		goto err;
	}

	if (data[2] != 0 && data[2] != 25) {
		printf_err("Board Area is non-English - sorry: Lang code = %i\n", data[2]);
		goto err;
	}

	len--;
	while ((data[len] == 0x00) && (len != 0))
		len--;
	if (len == 0 || data[len] != 0xC1) {
		printf_err("BOARD INFO not terminated properly, walking backwards len: "
				"%i:0x%02x should be 0xC1\n", len, data[len]);
		goto err;
	}

	fru->mfg_date = data[3] | (data[4] << 8) | (data[5] << 16);

	p = &data[6];
	len -= 6;

	i = parse_string(p, &fru->manufacturer, "Manufacture");
	p += i, len -= i;

	i = parse_string(p, &fru->product_name, "Product Name");
	p += i, len -= i;

	i = parse_string(p, &fru->serial_number, "Serial Number");
	p += i, len -= i;

	i = parse_string(p, &fru->part_number, "Part Number");
	p += i, len -= i;

	i = parse_string(p, &fru->FRU_file_ID, "FRU File ID");
	p += i, len -= i;

	j = 0;
	while (len != 0 && j < CUSTOM_FIELDS) {
		i = parse_string(p, &fru->custom[j], "Custom Field");
		p += i, len -= i, j++;
	}

	if (*p != 0xC1) {
		printf_err("BOARD INFO not terminated properly, "
				"offset %02i(0x%02x) : %02i(0x%02x) should be 0xC1\n",
				p - data, p - data, *p, *p);
		goto err;
	}

	return fru;

err:
	free(fru->manufacturer);
	free(fru->product_name);
	free(fru->serial_number);
	free(fru->part_number);
	free(fru->FRU_file_ID);
	for( j = 0; j < CUSTOM_FIELDS; j++)
		free(fru->custom[j]);
	free(fru);
	return NULL;
}

/*
 * Each record in this area begins with a pre-defined header as specified in the
 * section 16 in the Platform Management FRU Information Storage Definition.
 * This header contains a ‘Type’ field that identifies what information is
 * contained in the record.  * There are some FMC specific headers, defined
 * in section 5.5.1 of the FMC specification "IPMI Support". These FMC specific
 * sections have a 1 byte sub-type, and a 3 byte Unique Organization Identifier
 */
struct MULTIRECORD_INFO * parse_multiboard_area(unsigned char *data)
{
	int i = 0, tmp, type;
	unsigned char *p;
	struct MULTIRECORD_INFO *multi;

	multi = x_calloc(1, sizeof(struct MULTIRECORD_INFO));

	p = data;

	do {
		if (i != 0)
			p += 5 + p[2];
		if (p[0] >= 0x06 && p[0] <= 0xBF) {
			printf_err("MultiRecord Area %i: Invalid Record Header\n", i);
			return NULL;
		}
		if (calc_zero_checksum(p, 4)) {
			printf_err("MultiRecord Area %i (Record Type 0x%x): "
					"Header Checksum failed\n", i, p[0]);
			return NULL;
		}

		if (!p[2] || ((calc_zero_checksum(p+5, p[2] - 1) + p[3]) & 0xFF)) {
			printf_err("MultiRecord Area %i (Record Type 0x%x): "
					"Record Checksum failed\n", i, p[0]);
			return NULL;
		}

		/*
		 * Record Type ID
		 */
		switch(p[0]) {
			case MULTIRECORD_DC_OUTPUT:
			case MULTIRECORD_DC_INPUT:
				tmp = p[5] & 0xF;
				if ((tmp) >= NUM_SUPPLIES)
					printf_err("Too many Supplies defined in Multirecords\n");

				multi->supplies[tmp] = x_calloc(1, p[2] + 6);
				memcpy (multi->supplies[tmp], p, p[2] + 6);
				multi->supplies[tmp][1] = multi->supplies[tmp][1] & 0x7F;
				break;
			case MULTIRECORD_FMC:
				/*
				 * Use VITA's OUI: 0x0012a2 is specified in the FMC spec - Rule 5.77
				 */
				if ((p[5] | p[6] << 8 | p[7] << 16) != VITA_OUI) {
					printf_err("OUI Doesn't match : is 0x%06X, "
						"should be 0x%06x\n", p[5] | p[6] << 8 | p[7] << 16, VITA_OUI);
				}
				/* type field is located: Header + Manufacturer ID = 5 + 3 */
				type = p[8] >> 4;

				switch (type) {
					case MULTIRECORD_CONNECTOR:
						/* see table 7 in FMC spec */
						multi->connector = x_calloc(1, p[2] + 6);
						memcpy (multi->connector, p, p[2] + 6);
						/* This isn't the end 'til we re-assemble things */
						multi->connector[1] = multi->connector[1] & 0x7F;
						break;
					case MULTIRECORD_I2C:
						if (p[2] <= 5) {
							printf_warn("I2C MultiRecord is too short (len:%i)\n"
								"	(at least 4 is needed for OUI and subtype)\n",
								p[2]);
						} else {
							/* see table 9 in FMC spec */
							unsigned char *foo2;
							foo2 = six2ascii(&p[9], p[2] - 5);

							multi->i2c_devices = x_calloc(1, strlen((char *)foo2));
							strcpy ((char *)multi->i2c_devices, (char *)foo2);
							free(foo2);
							/* This isn't the end 'til we re-assemble things */
							multi->i2c_devices[1] = multi->i2c_devices[1] & 0x7F;
						}
						break;
					default:
						printf_err("Unknown multirecord type : %i\n", type);
						break;
				}

				if (type == 1) {

				}
				break;
			default:
				printf_err("Unknown MultiRecord Area\n");
		}

		i++;
	} while (!(p[1] & 0x80));

	return multi;
}

/*
 * Common Header Format
 * Section 8 in the Platform Management FRU Information Storage Definition
 */
struct FRU_DATA * parse_FRU (unsigned char *data)
{
	struct FRU_DATA *fru;

	fru = x_calloc (1, sizeof(struct FRU_DATA));

	/* Check FRU version */
	if (data[0] != 0x01) {
		printf_err("FRU Version number mismatch 0x%02x should be 0x01\n", data[0]);
		goto err;
	}

	/* Check Padding */
	if (data[6] != 0x00) {
		printf_err("FRU byte 6 should be PAD, and be zero -- but it's not\n");
		goto err;
	}

	/* Check header checksum */
	if (calc_zero_checksum(data, 7)) {
		printf_err("Common Header Checksum failed\n");
		goto err;
	}

	/* Parse Internal Use Area */
	if (data[1]) {
		printf_err("Internal Use Area not yet implemented - sorry\n");
		goto err;
	}

	/* Parse Chassis Info Area */
	if (data[2]) {
		printf_err("Chassis Info Area not yet implmented - sorry\n");
		goto err;
	}

	/* Parse Board Area */
	if (data[3]) {
		fru->Board_Area = parse_board_area(&data[data[3] * 8]);
		if (!fru->Board_Area)
			goto err;
	}

	/* Parse Chassis Info Area */
	if (data[4]) {
		printf_err("Chassis Info Area parsing not yet implemented - sorry\n");
		goto err;
	}

	/* Parse MultiRecord Area */
	if (data[5])
		fru->MultiRecord_Area = parse_multiboard_area(&data[data[5] * 8]);

	return fru;

err:
	free(fru);
	return NULL;

}

void free_FRU(struct FRU_DATA *fru)
{
	int j;

	free(fru->Board_Area->manufacturer);
	free(fru->Board_Area->product_name);
	free(fru->Board_Area->serial_number);
	free(fru->Board_Area->part_number);
	free(fru->Board_Area->FRU_file_ID);
	for(j = 0; j < CUSTOM_FIELDS; j++)
		free(fru->Board_Area->custom[j]);
	free(fru->Board_Area);

	for(j = 0; j < NUM_SUPPLIES; j++)
		free(fru->MultiRecord_Area->supplies[j]);
	free(fru->MultiRecord_Area->connector);
	free(fru->MultiRecord_Area);

	free(fru);

}

/*
 * take string, and put in into the buffer
 * return the number of bytes copied
 */
unsigned int insert_str(unsigned char *buf, char * str)
{
	int tmp;
	tmp = strlen(&str[1]);

	if (tmp <= 0x3F) {
		/* It fits, so just leave it as ASCII */
		buf[0] = tmp | (FRU_STRING_ASCII << 6);
		memcpy(&buf[1], &str[1], tmp);
	} else {
		/* 6-bit ASCII */
		unsigned char *six;
		/* dump_str((unsigned char*)&str[1], tmp, 8); */
		tmp = ascii2six((unsigned char *)&str[1], tmp, &six);
		/* dump_str(six, tmp, 6); */
		if (tmp > 0x3F)
			printf_err("String too long to fit\n");

		buf[0] = tmp | (FRU_STRING_SIXBIT << 6);
		memcpy(&buf[1], six, tmp);
	}


	return tmp + 1;
}

/*
 * Takes the FRU structure, and builds the binary blob
 */
unsigned char * build_FRU_blob (struct FRU_DATA *fru, size_t *length, int packed)
{
	unsigned char *p, *buf;
	unsigned int st, len, tmp, last = 0, i = 0, j;

	buf = x_calloc(1, 1024);
	len = 256;

	buf[0] = 0x01;
	i = 8;
	if (fru->Internal_Area)
		printf_err("Internal Use Area not yet implemented - sorry\n");

	if (fru->Chassis_Info)
		printf_err("Chassis Info not yet implemented - sorry\n");

	if (fru->Board_Area) {
		len = st = i;
		buf[3] = i / 8;
		buf[i] = 0x1;   /* Magic number */
		/* buf[i+1] = length, which needs to be determined later */
		buf[i+2] = 25;  /* English */
		buf[i+3] = (fru->Board_Area->mfg_date) & 0xFF;
		buf[i+4] = (fru->Board_Area->mfg_date >> 8) & 0xFF;
		buf[i+5] = (fru->Board_Area->mfg_date >> 16) & 0xFF;

		i += 6;
		i += insert_str(&buf[i], fru->Board_Area->manufacturer);
		i += insert_str(&buf[i], fru->Board_Area->product_name);
		i += insert_str(&buf[i], fru->Board_Area->serial_number);
		i += insert_str(&buf[i], fru->Board_Area->part_number);
		i += insert_str(&buf[i], fru->Board_Area->FRU_file_ID);
		for (j = 0; j < CUSTOM_FIELDS; j++) {
			if (fru->Board_Area->custom[j])
				i += insert_str(&buf[i], fru->Board_Area->custom[j]);
		}
		buf[i] = 0xC1;
		i++;
		i = (((i >> 3) + 1) << 3) - 1;
		len = i - st;
		buf[st + 1] = len / 8 + 1;
		buf[i] = 256 - calc_zero_checksum(&buf[st], len);
		i++;
	}
	if (fru->Product_Info) {
	}
	if (fru->MultiRecord_Area) {
		st = i;
		buf[5] = st / 8;
		for (tmp = 0; tmp < NUM_SUPPLIES; tmp++) {
			p = fru->MultiRecord_Area->supplies[tmp];
			if (!p)
				continue;
			memcpy(&buf[i], p, p[2]+ 5);
			last = i + 1;
			i += p[2] + 5;
		}
		p = fru->MultiRecord_Area->connector;
		if (p) {
			memcpy(&buf[i], p, p[2]+ 5);
			last = i + 1;
			i += p[2] + 5;
		}
		p = fru->MultiRecord_Area->i2c_devices;
		if (p) {
			unsigned int len, oui = VITA_OUI;
			unsigned char *six;
			len = ascii2six(p, strlen((char *)p), &six);

			/* Type ID, Record Format version, Length, checksum, checksum */
			sprintf((char *)&buf[i], "%c%c%c%c%c", MULTIRECORD_FMC, 0x02, len + 4, 0, 0);
			/* Store OUI */
#ifndef __MINGW32__
# if __BYTE_ORDER == __BIG_ENDIAN
			oui = __bswap_32(oui);
#endif
#endif
			memcpy(&buf[i+5], &oui, 3);
			/* Subtype & version  - see Table 9 in FMC spec*/
			sprintf((char *)&buf[i+8], "%c", (0x1 << 4) | (0));
			/* finally copy over the string */
			memcpy(&buf[i+9], six, len);
			/* Record Checksum */
			buf[i+3] = 0x100 - calc_zero_checksum(&buf[i+5], len + 3);
			/* Header Checksum */
			buf[i+4] = 0x100 - calc_zero_checksum(&buf[i], 4);

			last = i + 1;
			i += len + 9;
		}
		if (last) {
			buf[last] = buf[last] | 0x80;
			/* redo the header checksum, since we just changed the header :( */
			buf[last + 3] = 0;
			buf[last + 3] = 0x100 - calc_zero_checksum(&buf[last - 1], 4);
		}
	}
	buf[7] = 256 - calc_zero_checksum(buf, 6);

	*length = i;
	return buf;
}
