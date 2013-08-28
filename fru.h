/*
 * fru.h
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


/* 
 * These structures/data are based from:
 * Platform Management FRU Information
 * Storage Definition
 * Document Revision 1.1, September 27, 1999
 * http://download.intel.com/design/servers/ipmi/FRU1011.pdf
 * 
 * ANSI/VITA 57.1
 * FPGA Mezzanine Card (FMC) Standard
 * Approved July 2008 (Revised February 2010)
 * Used with permission
 */

#define CUSTOM_FIELDS 10

/* Defines from section 13 "TYPE/LENGTH BYTE FORMAT"  of FRU spec) */
#define FRU_STRING_BINARY  0
#define FRU_STRING_BCD     1
#define FRU_STRING_SIXBIT  2
#define FRU_STRING_ASCII   3

struct BOARD_INFO {
	char language_code;
	unsigned int mfg_date;
	char *manufacturer;
	char *product_name;
	char *serial_number;
	char *part_number;
	char *FRU_file_ID;
	char *custom[CUSTOM_FIELDS];
};

#define NUM_MULTI     3
#define NUM_SUPPLIES 12

struct MULTIRECORD_INFO {
	unsigned char *supplies[NUM_SUPPLIES];
	unsigned char *connector;
	unsigned char *i2c_devices;
};

#define MULTIRECORD_I2C 1
#define MULTIRECORD_CONNECTOR 0

#define MULTIRECORD_DC_OUTPUT 1
#define MULTIRECORD_DC_INPUT  2
/* 0xfa is the FMC-specific MultiRecords, see Rule Rule 5.77 in the FMC spec */
#define MULTIRECORD_FMC       0xFA
/* VITA’s Organizationally Unique Identifier - see rule 5.77 in the FMC spec */
#define VITA_OUI 0x0012A2

struct FRU_DATA {
	char *Internal_Area;
	char *Chassis_Info;
	struct BOARD_INFO *Board_Area;
	char *Product_Info;
	struct MULTIRECORD_INFO *MultiRecord_Area;
};

extern void printf_err (const char *, ...);
extern void printf_warn (const char *, ...);
extern void printf_info (const char *, ...);
extern struct FRU_DATA * parse_FRU (unsigned char *);
extern void free_FRU (struct FRU_DATA * fru);
extern unsigned char * build_FRU_blob (struct FRU_DATA *, size_t *, int);
extern time_t min2date(unsigned int mins);
extern void * x_calloc (size_t, size_t);
