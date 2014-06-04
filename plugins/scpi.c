/*
 * Some of this code is taken from:
 * http://www.home.agilent.com/upload/cmc_upload/All/sockets.c
 * and is copyright Copyright (C) 2007 Agilent Technologies
 * and was heavily modified.
 *
 * The modifications and remaining code is:
 * Copyright (C) 2012-2013 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * The GNU General Public License is available at
 * http://www.gnu.org/copyleft/gpl.html.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/dir.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>

#include "../osc.h"
#include "../osc_plugin.h"
#include "../config.h"

struct scpi_instrument {
	/* selection */
	bool         serial;
	bool         network;
	char        *id_regex;
	char	    *response;

	/* network based instrument */
	char        *ip_address;
	in_port_t    main_port;
	int          main_socket;
	in_port_t    control_port;
	int          control_socket;

	/* serial based instrument */
	char         *tty_path;
	int          ttyfd;
	int          gpib_addr;

};

static struct scpi_instrument signal_generator;
static struct scpi_instrument spectrum_analyzer;

static char *supported_spectrum_analyzers[] = {
	"Rohde&Schwarz,FSEA 20,839161/004,3.40.2",
	NULL
	};

#define SOCKETS_BUFFER_SIZE  1024
#define SOCKETS_TIMEOUT      2

#define DEFAULT_SCPI_IP_ADDR      "192.168.0.1"
#define DEFAULT_SCPI_IP_PORT      5025
#define DEFAULT_SCPI_TTY          "/dev/ttyUSB0"
#define DEFAULT_SCPI_GPIB         16

#define TTY_RAW_MODE

#if 1
#define print_output_sys fprintf
#define print_output_scpi(x) fprintf(stdout, "SCPI: %s\n", x)
#else
#define print_output_sys {do { } while (0);}
#define print_output_scpi(x) {do { } while (0);}
#endif

/*
 * Network communications functions
 */

/* Wait for data to become available */
static int network_waitfordata(int MySocket)
{
	fd_set MyFDSet;
	struct timeval tv;
	int retval;

	/* Initialize fd_set structure */
	FD_ZERO(&MyFDSet);

	/* Add socket to "watch list" */
	FD_SET(MySocket,&MyFDSet);

	/* Set Timeout */
	tv.tv_sec=SOCKETS_TIMEOUT;
	tv.tv_usec=0;

	/* Wait for change */
	retval=select(MySocket+1,&MyFDSet,NULL,NULL,&tv);

	/* Interpret return value */
	if(retval==-1) {
		printf("Error: Problem with select (%i)...\n",errno);
		perror(__func__);
		exit(1);
	}

	/* 0 = timeout, 1 = socket status has changed */
	return retval;
}

static int scpi_network_read(struct scpi_instrument *scpi)
{
	int actual;

	/* Wait for data to become available */
	if (network_waitfordata(scpi->control_socket) == 0) {
		scpi->response[0] = 0;
		return 0;
	}

	/* Read data */

	actual = recv(scpi->control_socket, scpi->response,
			SOCKETS_BUFFER_SIZE, 0);
	if (actual == -1) {
		printf("Error: Unable to receive data (%i)...\n",errno);
		perror(__func__);
		exit(1);
	} else {
		scpi->response[actual]=0;
	}

	return actual;
}

/* Turn NOdelay on */
static void network_setnodelay(int MySocket)
{
	int StateNODELAY = 1;
	int ret;

	ret = setsockopt(MySocket, IPPROTO_TCP, TCP_NODELAY,
			(void *)&StateNODELAY, sizeof StateNODELAY);

	if (ret == -1) {
		printf("Error: Unable to set NODELAY option (%i)...\n",errno);
		perror("sockets");
		exit(1);
	}
	return;
}

static int __attribute__ ((warn_unused_result))
network_connect(struct scpi_instrument *scpi)
{
	struct sockaddr_in MyAddress, MyControlAddress;
	int status;
	struct timeval timeout;
	char buf[128];

	timeout.tv_sec = SOCKETS_TIMEOUT;
	timeout.tv_usec = 0;

	/* Create socket (allocate resources) - IPv4, TCP */
	scpi->main_socket = socket(PF_INET, SOCK_STREAM, 0);

	if (scpi->main_socket == -1) {
		printf("Error: Unable to create socket (%i)...\n",errno);
		return -1;
	}

	/* set Recieve and Transmit Timeout, so connect doesn't take so long to fail */
	status = setsockopt(scpi->main_socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
	if (status < 0)
		perror("setsockopt failed\n");

	status = setsockopt(scpi->main_socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,sizeof(timeout));
	if (status < 0)
		perror("setsockopt failed\n");

	/* Establish TCP connection */
	memset(&MyAddress,0,sizeof(struct sockaddr_in));
	MyAddress.sin_family = PF_INET;
	MyAddress.sin_port = htons(scpi->main_port);
	MyAddress.sin_addr.s_addr = inet_addr(scpi->ip_address);

	status = connect(scpi->main_socket, (struct sockaddr *)&MyAddress, sizeof(struct sockaddr_in));
	if(status == -1) {
		printf("Error: Unable to establish connection to ip:%s (%i)...\n",
				scpi->ip_address, errno);
		return -1;
	}

	/* Minimize latency by setting TCP_NODELAY option */
	network_setnodelay(scpi->main_socket);

	/* Ask for control port */
	sprintf(buf, "SYST:COMM:TCPIP:CONTROL?\n");
	status = send(scpi->main_socket, buf, strlen(buf), 0);
	if (status == -1)
		return -1;

	if (scpi_network_read((scpi)) == 0) {
		scpi->control_socket = scpi->main_socket;
		return 0;
	}

	sscanf(scpi->response, "%" SCNd16, &scpi->control_port);

	/* Create socket for control port */
	scpi->control_socket = socket(PF_INET, SOCK_STREAM, 0);
	if(scpi->control_socket == -1) {
		printf("Error: Unable to create control port socket (%i)...\n",errno);
		return -1;
	}

	/* Establish TCP connection to control port */
	memset(&MyControlAddress, 0, sizeof(struct sockaddr_in));
	MyControlAddress.sin_family = PF_INET;
	MyControlAddress.sin_port = htons(scpi->control_port);
	MyControlAddress.sin_addr.s_addr = inet_addr(scpi->ip_address);

	status = connect(scpi->control_socket, (struct sockaddr *) &MyControlAddress, sizeof(struct sockaddr_in));
	if(status == -1) {
		printf("Error: Unable to establish connection to control port (%i)...\n",
			errno);
		return -1;
	}

	return 0;
}

/*
 * tty functions
 */

static int tty_read(struct scpi_instrument *scpi)
{
#ifdef TTY_RAW_MODE
	int n, end = 0, i;
	int bc = 0;

	do {
		n = read(scpi->ttyfd, (char *)scpi->response + bc,
				SOCKETS_BUFFER_SIZE - bc);
		if (n > 0) {
			bc += n;
			for (i = 0; i < bc; i++)
				/* Handle LineFeeds */
				if (scpi->response[i] == 0x0A) {
					end = 1;
					scpi->response[i + 1] = 0;
				}
		}
	} while (bc < SOCKETS_BUFFER_SIZE && (end == 0));

	return bc;
#else
	int ret = read(scpi->ttyfd, (char *)scpi->response,
			SOCKETS_BUFFER_SIZE);
	if (ret >= 0)
		scpi->response[ret] = 0;

	return ret;
#endif
}


static int tty_connect(struct scpi_instrument *scpi)
{
	struct termios ti;
	char strbuf[250];
	ssize_t status;

	scpi->ttyfd = open(scpi->tty_path, O_RDWR | O_NOCTTY);
	if (scpi->ttyfd < 0) {
		print_output_sys(stderr, "%s: Can't open serial port: %s %s (%d)\n",
				__func__, scpi->tty_path, strerror(errno), errno);

		return -1;
	}

	tcflush(scpi->ttyfd, TCIOFLUSH);

	if (tcgetattr(scpi->ttyfd, &ti) < 0) {
		print_output_sys(stderr, strbuf, "%s: Can't get port settings: %s (%d)\n", __func__, strerror(errno), errno);
		close(scpi->ttyfd);
		scpi->ttyfd = -1;
		return -1;
	}

#ifdef TTY_RAW_MODE
	cfmakeraw(&ti);
	ti.c_cc[VMIN] = 1;
	ti.c_cc[VTIME] = 0;
#else
	ti.c_cflag |=  ICANON;
#endif

	ti.c_cflag |=  CLOCAL;
	ti.c_cflag &= ~CRTSCTS;
	ti.c_cflag &= ~PARENB;
	ti.c_cflag &= ~PARODD;
	ti.c_cflag &= ~CSIZE;
	ti.c_cflag |=  CS8;
	ti.c_cflag &= ~CSTOPB;

	ti.c_oflag &= ~OPOST;
	ti.c_iflag = ti.c_lflag = 0;

	cfsetospeed(&ti, B19200); /* We don't need that for USB-GPIB */

	if (tcsetattr(scpi->ttyfd, TCSANOW, &ti) < 0) {
		print_output_sys(stderr, strbuf, "%s: Can't change port settings: %s (%d)\n",
			__func__, strerror(errno), errno);
		close(scpi->ttyfd);
		scpi->ttyfd = -1;
		return -1;
	}

	tcflush(scpi->ttyfd, TCIOFLUSH);

#ifdef TTY_RAW_MODE
	if (fcntl(scpi->ttyfd, F_SETFL, fcntl(scpi->ttyfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
		print_output_sys(stderr, "%s: Can't set non blocking mode: %s (%d)\n",
			__func__, strerror(errno), errno);
		close(scpi->ttyfd);
		scpi->ttyfd = -1;
		return -1;
	}
#endif

	print_output_sys(stdout, "%s: GPIB tty: connecting to %s\n", __func__, scpi->tty_path);

	/* set up controller and GPIB address */
	sprintf(strbuf, "++mode 1\n");
	status = write(scpi->ttyfd, strbuf, strlen(strbuf));
	if (status < 0)
		return -1;

	sprintf(strbuf, "++addr %d\n", scpi->gpib_addr);
	status = write(scpi->ttyfd, strbuf, strlen(strbuf));
	if (status < 0)
		return -1;

	sprintf(strbuf, "++auto 1\n");
	status = write(scpi->ttyfd, strbuf, strlen(strbuf));
	if (status < 0)
		return -1;

	return 0;
}


/* Main SCPI functions */

/*
 * writes count bytes from the buffer (buf) to the
 * scpi instrument referred to by the descriptor *scpi.
 *
 * On  success, the number of bytes written is returned
 * (zero indicates nothing was written).
 * On error, -1 is returned
 */
static ssize_t scpi_write(struct scpi_instrument *scpi, const void *buf, size_t count)
{
	int retval = -1;

	if (!scpi->network && !scpi->serial)
		return -ENOENT;

	if (scpi->network && !scpi->control_socket)
		return -ENXIO;
	else if (scpi->network) {
		retval = send(scpi->control_socket, buf, count, 00);
		if (retval == count && memchr(buf, '?', count)) {
			memset(scpi->response, 0, SOCKETS_BUFFER_SIZE);
			scpi_network_read(scpi);
		}
	}

	if (scpi->serial && scpi->ttyfd < 0)
		return -ENXIO;
	else if (scpi->serial) {
		retval  = write(scpi->ttyfd, buf, count);
		if (retval == count) {
			if (memchr(buf, '?', count)) {
				memset(scpi->response, 0, SOCKETS_BUFFER_SIZE);
				tty_read(scpi);
			}
		} else
			fprintf(stderr, "SCPI:%s tty didn't write the entire buffer\n", __func__);

		tcflush(scpi->ttyfd, TCIOFLUSH);
	}

	return retval;

}

#define MAX_STR_SIZE 256

static ssize_t scpi_fprintf(struct scpi_instrument *scpi, const char *str, ...)
{
	va_list args;
	char buf[MAX_STR_SIZE];
	ssize_t retval = -1;
	int len;

	va_start(args, str);
	len = vsnprintf(buf, MAX_STR_SIZE, str, args);
	va_end(args);

	if (len > -1 && len < MAX_STR_SIZE)
		retval = scpi_write(scpi, buf, strlen(buf));

	return retval;
}

/*
static ssize_t scpi_read(struct scpi_instrument *scpi)
{
	ssize_t retval;

	if (!scpi->network && !scpi->serial)
		return -ENOENT;

	if (scpi->network && !scpi->control_socket)
		return -ENXIO;
	else {
		retval = scpi_network_read(scpi);
	}

	return retval;
}
*/

static int scpi_connect(struct scpi_instrument *scpi)
{
	int ret;

	if(scpi->network) {
		ret = network_connect(scpi);
		if (ret != 0)
			return ret;
		if (scpi->control_socket != scpi->main_socket) {
			scpi_fprintf(scpi, "DCL\n");
			if (strlen(scpi->response)) {
				if (!strcmp(scpi->response, "DCL\n"))
					printf("Warning : %s DCL response: %s\n", __func__, scpi->response);
			}
		}
	} else if (scpi->serial) {
		tty_connect(scpi);
	} else {
		printf("misconfigured SCPI data structure\n");
		return -1;
	}

	scpi_fprintf(scpi, "*CLS\n");
	scpi_fprintf(scpi, "*RST\n");
	scpi_fprintf(scpi, "*IDN?\n");
	if (!strstr(scpi->response, scpi->id_regex)) {
		printf("instrument doesn't match regex\n");
		printf("\twanted   : '%s'\n", scpi->id_regex);
		printf("\trecieved : '%s'\n", scpi->response);
		return -1;
	}
	printf("Instrument ID: %s\n", scpi->response);

	return 0;
}

/* Spectrum Analyzer commands */

bool scpi_rx_connected()
{
	return (spectrum_analyzer.ttyfd != 0 || spectrum_analyzer.control_port != 0);
}

void scpi_rx_trigger_sweep()
{
	scpi_fprintf(&spectrum_analyzer, "INIT:IMM;*WAI\n");
}

void scpi_rx_set_center_frequency(unsigned long long fcent_hz)
{
	scpi_fprintf(&spectrum_analyzer, ":FREQ:CENT %llu;*WAI\n", fcent_hz);
}

void scpi_rx_set_span_frequency(unsigned long long fspan_hz)
{
	scpi_fprintf(&spectrum_analyzer, ":FREQ:SPAN %llu;*WAI\n", fspan_hz);
}

void scpi_rx_set_bandwith(unsigned int res_bw_khz, unsigned int vid_bw_khz)
{
	scpi_fprintf(&spectrum_analyzer, ":BAND %dkHz;*WAI\n", res_bw_khz);
	if (!vid_bw_khz)
		scpi_fprintf(&spectrum_analyzer, ":BAND:VID %dkHz;*WAI\n", res_bw_khz);
	else
		scpi_fprintf(&spectrum_analyzer, ":BAND:VID %dkHz;*WAI\n", vid_bw_khz);
}

void scpi_rx_set_bandwith_auto(double ratio)
{
	scpi_fprintf(&spectrum_analyzer, ":BAND:AUTO ON;*WAI\n");
	scpi_fprintf(&spectrum_analyzer, ":BAND:RAT %f;*WAI\n", ratio);

}

void scpi_rx_setup()
{
	static time_t rx_cal_time = 0;

	scpi_fprintf(&spectrum_analyzer, ":DISP:TRACE:Y:RLEVEL %d DBM\n", 10);
	/* Turn averaging off */
	scpi_fprintf(&spectrum_analyzer, ":AVER OFF\n");
	/* Turn off the markers */
	scpi_fprintf(&spectrum_analyzer, ":DISPLAY:MARK: AOFF\n");

	if (rx_cal_time < time(NULL) && 0) {
		scpi_fprintf(&spectrum_analyzer, ":CAL:SHOR?\n");
		/* Wait an hour */
		rx_cal_time = time(NULL) + (60 * 60);
	}

	/* trigger source is external (continuous mode off) */
	scpi_fprintf(&spectrum_analyzer, ":INIT:CONT OFF\n");
	scpi_rx_trigger_sweep();
	/* trigger source is internal (continuous mode on) */
	//scpi_fprintf(&spectrum_analyzer, ":INIT:CONT ON\n");
}

void scpi_rx_set_averaging(int average)
{
	scpi_fprintf(&spectrum_analyzer, ":AVER:TYPE SCAL\n");
	scpi_fprintf(&spectrum_analyzer, ":AVER:COUNT %i\n", average);
}

int scpi_rx_set_marker_freq(unsigned int marker, unsigned long long freq)
{
	scpi_fprintf(&spectrum_analyzer, "CALC:MARK%d:X %llu;*WAI\n", marker, freq);
	return scpi_fprintf(&spectrum_analyzer, "CALC:MARK%d:STAT ON;*WAI\n", marker);
}

int scpi_rx_get_marker_level(unsigned marker, bool wait, double *lvl)
{
	int ret;

	if (wait)
		scpi_fprintf(&spectrum_analyzer, "INIT:IMM;*WAI\n");

	scpi_fprintf(&spectrum_analyzer, "CALC:MARK%d:Y?\n", marker);
	ret = sscanf(spectrum_analyzer.response, "%lf", lvl);

	if (ret == 1)
		return 0;

	return -1;
}

int scpi_rx_get_marker_freq(unsigned int marker, bool wait, double *lvl)
{
	int ret;

	if (wait)
		scpi_fprintf(&spectrum_analyzer, "INIT:IMM;*WAI\n");

	/* scpi_fprintf("CALC:MARK%d:COUNT ON\n", marker); */
	scpi_fprintf(&spectrum_analyzer, "CALC:MARK%d:COUNT:FREQ?\n", marker);
	ret = sscanf(spectrum_analyzer.response, "%lf", lvl);

	if (ret == 1)
		return 0;

	return -1;
}

/* SIGNAL Generator Functions */
static int tx_freq_set_Hz(struct scpi_instrument *scpi, unsigned long long freq)
{
	return scpi_fprintf(scpi, ":FREQ:CW %llu;*WAI\n", freq);
}

static int tx_output_set(struct scpi_instrument *scpi, unsigned on)
{
	return scpi_fprintf(scpi, ":OUTPut %s;*WAI\n", on ? "ON" : "OFF");
}

static int tx_mag_set_dBm(struct scpi_instrument *scpi, double lvl)
{
	return scpi_fprintf(scpi, ":POW %f DBM;*WAI\n", lvl);
}

/*
 * Save/Restore stuff
 */

static struct scpi_instrument *current_instrument;

#define SERIAL_TOK "serial"
#define NET_TOK    "network"
#define REGEX_TOK  "id_regex"
#define IP_TOK     "ip_addr"
#define TTY_TOK    "tty_path"
#define GPIB_TOK   "gpib_addr"
#define CON_TOK    "connect"

static char *scpi_handle_profile(struct osc_plugin *plugin, const char *attrib,
		const char *value)
{
	gchar **elems, **min_max;
	char *buf;
	int i;
	double lvl;
	long long j;
	FILE *fd;

	if (value)
		buf = NULL;
	else {
		buf = malloc(128);
		memset(buf, 0, 128);
	}

	current_instrument = NULL;

	elems = g_strsplit(attrib, ".", 0);
	if (!strncmp(elems[0], "rx", 2))
		current_instrument = &spectrum_analyzer;
	if (!strncmp(elems[0], "tx", 2))
		current_instrument = &signal_generator;

	if (!current_instrument)
		return NULL;

	if (strcmp(elems[1], SERIAL_TOK) == 0) {
		if (value) {
			if (atoi(value) == 1)
				current_instrument->serial = TRUE;
		} else {
			if (current_instrument->serial)
				sprintf(buf, "1");
		}
	} else if (strcmp(elems[1], NET_TOK) == 0) {
		if (value) {
			if (atoi(value) == 1)
				current_instrument->network = TRUE;
		} else {
			if (current_instrument->network)
				sprintf(buf, "1");
		}
	} else if (strcmp(elems[1], REGEX_TOK) == 0) {
		if (value) {
			current_instrument->id_regex = strdup(value);
		} else {
			if (current_instrument->id_regex)
				sprintf(buf, "%s", current_instrument->id_regex);
		}
	} else if (strcmp(elems[1], IP_TOK) == 0) {
		if (value) {
			current_instrument->ip_address = strdup(value);
		} else {
			if (current_instrument->ip_address && current_instrument->network)
				sprintf(buf, "%s", current_instrument->ip_address);
		}
	} else if (strcmp(elems[1], TTY_TOK) == 0) {
		if (value) {
			current_instrument->tty_path = strdup(value);
		} else {
			if (current_instrument->tty_path && current_instrument->serial)
				sprintf(buf, "%s", current_instrument->tty_path);
		}
	} else if (strcmp(elems[1], GPIB_TOK) == 0) {
		if (value) {
			current_instrument->gpib_addr = atoi(value);
		} else {
			if (current_instrument->gpib_addr && current_instrument->serial)
				sprintf(buf, "%i", current_instrument->gpib_addr);
		}
	} else
	/* There are some things testers need to add by hand to do things */
	if (strcmp(elems[1], CON_TOK) == 0) {
		if (value) {
			if (atoi(value) == 1)
				if (scpi_connect(current_instrument) != 0)
					return "FAIL";
		}
		/* We don't save the connect state */
	} else if (MATCH_ATTRIB("tx.freq")) {
		if (value)
			tx_freq_set_Hz(&signal_generator, atoll(value));
		/* We don't save the frequency */
	} else if (MATCH_ATTRIB("tx.mag")) {
		if (value)
			tx_mag_set_dBm(&signal_generator, atof(value));
		/* Don't save the magintude */
	} else if (MATCH_ATTRIB("tx.on")) {
		if (value)
			tx_output_set(&signal_generator, atoi(value));
		/* Don't save the on/off state */
	} else if (MATCH_ATTRIB("rx.setup")) {
		if (value)
			scpi_rx_setup(&spectrum_analyzer);
	} else if (MATCH_ATTRIB("rx.center")) {
		if (value)
			scpi_rx_set_center_frequency(atoll(value));
	} else if (MATCH_ATTRIB("rx.span")) {
		if (value)
			scpi_rx_set_span_frequency(atoll(value));
	} else if (!strncmp(attrib, "rx.marker", strlen("rx.marker"))) {
		if (value) {
			i = atoi(&attrib[strlen("rx.marker")]);
			j = atoll(value);
			if (i && j)
				scpi_rx_set_marker_freq(i, j);
			else
				printf("problems with %s = %s\n", attrib, value);
		}
	} else if (!strncmp(attrib, "rx.log.marker", strlen("rx.log.marker"))) {
		if (value) {
			i = atoi(&attrib[strlen("rx.log.marker")]);
			if (i) {
				scpi_rx_get_marker_level(i, true, &lvl);
				fd = fopen(value, "a");
				if (!fd)
					return NULL;

				fprintf (fd, "%f, ", lvl);
				fclose (fd);
			}
		}
	} else if (!strncmp(attrib, "rx.test.marker", strlen("rx.test.marker"))) {
		if (value) {
			i = atoi(&attrib[strlen("rx.test.marker")]);
			if (i) {
				scpi_rx_get_marker_level(i, true, &lvl);
				min_max = g_strsplit(value, " ", 0);
				if (lvl >= atof(min_max[1]) && lvl <= atof(min_max[0])) {
					printf("Marker%i (%f) didn't match requirements (%f - %f)\n",
						i, lvl, atof(min_max[0]), atof(min_max[1]));
					return "FAIL";
				}
			} else {
				return "FAIL";
			}
		}
	} else {
		printf("Unhandled tokens in ini file,\n"
				"\tSection %s\n\tAtttribute : %s\n\tValue: %s\n",
				"SCPI", attrib, value);
		if (value)
			return "FAIL";
	}

	return buf;
}

static const char *scpi_sr_attribs[] = {
	"rx." SERIAL_TOK,
	"rx." NET_TOK,
	"rx." REGEX_TOK,
	"rx." IP_TOK,
	"rx." TTY_TOK,
	"rx." GPIB_TOK,
	"rx." CON_TOK,
	"rx.setup",
	"rx.center",
	"rx.span",
	"rx.marker",
	"tx." SERIAL_TOK,
	"tx." NET_TOK,
	"tx." REGEX_TOK,
	"tx." IP_TOK,
	"tx." TTY_TOK,
	"tx." GPIB_TOK,
	"tx." CON_TOK,
	"tx.freq",
	"tx.mag",
	"tx.on",
	NULL,
};

/*
 * All the GUI/Glade stuff
 */

static GtkWidget *scpi_radio_conf;
static GtkWidget *scpi_none_radio, *scpi_tty_radio, *scpi_net_radio;
static GtkWidget *scpi_output, *scpi_regex;
static GtkWidget *scpi_ip_addr;
static GtkWidget *scpi_serial_tty, *scpi_gpib_addr;
static GtkWidget *scpi_id;

static char *cmd_to_send = NULL;

#define NO_CONNECT 0
#define NETWORK_CONNECT 1
#define TTY_CONNECT 2

#define SCPI_NONE 0
#define SCPI_TX   1
#define SCPI_RX   2

static void load_instrument (struct scpi_instrument *scpi)
{
	char tmp[128];

	current_instrument = scpi;

	if (!current_instrument->serial && !current_instrument->network) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scpi_none_radio), TRUE);
	} else if (current_instrument->serial) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scpi_tty_radio), TRUE);
	} else if (current_instrument->network) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scpi_net_radio), TRUE);
	}

	if (current_instrument->ip_address && strlen(current_instrument->ip_address)) {
		if (current_instrument->main_port &&
					current_instrument->main_port != DEFAULT_SCPI_IP_PORT)
			sprintf(tmp, "%s:%i", current_instrument->ip_address, current_instrument->main_port);
		else
			sprintf(tmp, "%s", current_instrument->ip_address);

		gtk_entry_set_text(GTK_ENTRY(scpi_ip_addr), (const gchar *)tmp);
	}

	if (current_instrument->tty_path && strlen(current_instrument->tty_path))
		gtk_entry_set_text(GTK_ENTRY(scpi_serial_tty), (const gchar *)current_instrument->tty_path);

	sprintf(tmp, "%i", current_instrument->gpib_addr);
	gtk_entry_set_text(GTK_ENTRY(scpi_gpib_addr), (const gchar *)tmp);

}

static void instrument_type_cb (GtkComboBox *box)
{
	gint item;

	item = gtk_combo_box_get_active(box);
	switch (item) {
		case SCPI_NONE:
			current_instrument = NULL;
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scpi_none_radio), TRUE);
			gtk_widget_hide(scpi_radio_conf);
			gtk_widget_hide(scpi_output);
			break;
		case SCPI_TX:
			load_instrument(&signal_generator);
			gtk_widget_show(scpi_radio_conf);
			if (current_instrument->control_socket)
				gtk_widget_show(scpi_output);
			else
				gtk_widget_hide(scpi_output);
			break;
		case SCPI_RX:
			load_instrument(&spectrum_analyzer);
			gtk_widget_show(scpi_radio_conf);
			if (current_instrument->ttyfd)
				gtk_widget_show(scpi_output);
			else
				gtk_widget_hide(scpi_output);
			break;
		default:
			printf("Unknown selection in %s:%s: %s\n",
				__FILE__, __func__,
				gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(box)));
			break;
	}
}

static void scpi_text_entry_cb (GtkEntry *box, int data)
{
	GdkColor green = {
		.red = 0,
		.green = 0xFFFF,
		.blue = 0,
	};
	GdkColor red = {
		.red = 0xFFFF,
		.green = 0,
		.blue = 0,
	};

	switch (data) {
		case 0:
			if (current_instrument->ip_address)
				free(current_instrument->ip_address);

			current_instrument->ip_address = strdup(gtk_entry_get_text(box));
			break;
		case 1:
			if (current_instrument->tty_path)
				free(current_instrument->tty_path);

			current_instrument->tty_path = strdup (gtk_entry_get_text(box));
			break;
		case 2:
			current_instrument->gpib_addr = atoi(gtk_entry_get_text(box));
			break;
		case 3:
			if (current_instrument->id_regex)
				free(current_instrument->id_regex);

			current_instrument->id_regex = strdup (gtk_entry_get_text(box));

			if (strstr(gtk_label_get_text(GTK_LABEL(scpi_id)), current_instrument->id_regex))
				gtk_widget_modify_text(GTK_WIDGET(box), GTK_STATE_NORMAL, &green);
			else
				gtk_widget_modify_text(GTK_WIDGET(box), GTK_STATE_NORMAL, &red);
			break;
		case 4:
			if (cmd_to_send)
				free(cmd_to_send);
			cmd_to_send = strdup (gtk_entry_get_text(box));
			break;
		default:
			printf("Unknown selection in %s:%s: %i\n",
				__FILE__, __func__, data);
			break;
	}
}

static void init_scpi_device(struct scpi_instrument *device)
{
	memset(device, 0, sizeof(struct scpi_instrument));
	device->ip_address = strdup(DEFAULT_SCPI_IP_ADDR);
	device->main_port = DEFAULT_SCPI_IP_PORT;
	device->tty_path = strdup(DEFAULT_SCPI_TTY);
	device->gpib_addr = DEFAULT_SCPI_GPIB;
	device->response = malloc(SOCKETS_BUFFER_SIZE);
	if (!device->response) {
		printf("%s:%s: malloc fail\n", __FILE__, __func__);
		exit (-1);
	}
	memset(device->response, 0, SOCKETS_BUFFER_SIZE);
}

static void connect_clicked_cb(void)
{
	int i, ret = -1;

	if(current_instrument->network && current_instrument->ip_address) {
		if (!current_instrument->main_port)
			current_instrument->main_port = DEFAULT_SCPI_IP_PORT;

		ret = network_connect(current_instrument);

	}

	if(current_instrument->serial && current_instrument->tty_path) {
		ret = tty_connect(current_instrument);
	}

	if (ret == 0) {
		scpi_fprintf(current_instrument, "*IDN?\n");
		if (strlen(current_instrument->response)) {
			gtk_label_set_text(GTK_LABEL(scpi_id), current_instrument->response);
			for (i = 0; i <= sizeof(supported_spectrum_analyzers); i++) {
printf("%i\n", i);
printf("%s\n", current_instrument->response);
printf("%s\n", supported_spectrum_analyzers[i]);
				if (supported_spectrum_analyzers[i] &&
							!strcmp(supported_spectrum_analyzers[i], current_instrument->response)) {
					gtk_label_set_text(GTK_LABEL(scpi_regex), current_instrument->response);
					break;
				}
			}
		}

		if (current_instrument->id_regex)
			gtk_entry_set_text(GTK_ENTRY(scpi_regex), current_instrument->id_regex);
		else
			gtk_entry_set_text(GTK_ENTRY(scpi_regex), "");
		g_signal_emit_by_name(scpi_regex, "changed");

		gtk_widget_show(scpi_output);
	}

}

static void scpi_radio_cb (GtkRadioButton *button, int data)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
		return;

	switch (data) {
		case NO_CONNECT:
			if (current_instrument) {
				current_instrument->serial = FALSE;
				current_instrument->network = FALSE;
			}
			break;
		case NETWORK_CONNECT:
			current_instrument->serial = FALSE;
			current_instrument->network = TRUE;
			if (current_instrument->control_socket)
				gtk_widget_show(scpi_output);
			else
				gtk_widget_hide(scpi_output);
			break;
		case TTY_CONNECT:
			current_instrument->serial = TRUE;
			current_instrument->network = FALSE;
			if (current_instrument->ttyfd)
				gtk_widget_show(scpi_output);
			else
				gtk_widget_hide(scpi_output);
			break;
		default:
			printf("Unknown selection in %s:%s\n", __FILE__, __func__);
			break;
	}
}

static void scpi_cmd_cb (GtkButton *button, GtkEntry *box)
{
	const char *buf = gtk_entry_get_text(box);

	if (!buf || !strlen(buf))
		return;

	current_instrument->response[0] = 0;
	scpi_fprintf(current_instrument, "%s\n", buf);

	printf("send '%s',\n", buf);
	if (current_instrument->response)
		printf("received '%s'\n", current_instrument->response);
}

/*
 *  Main function
 */
static GtkWidget * scpi_init(GtkWidget *notebook, const char *ini_fn)
{
	GtkBuilder *builder;
	GtkWidget *scpi_panel;
	GtkWidget *instrument_type, *connect;
	GtkWidget *scpi_cmd;
	GtkWidget *scpi_play;
	GtkWidget *tty_conf, *network_conf;

	init_scpi_device(&signal_generator);
	init_scpi_device(&spectrum_analyzer);

	builder = gtk_builder_new();
	if (!gtk_builder_add_from_file(builder, "scpi.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "scpi.glade", NULL);

	scpi_panel = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_panel"));
	network_conf = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_network_conf"));
	tty_conf = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_tty_conf"));

	scpi_none_radio = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_connect_none"));
	scpi_net_radio = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_network"));
	scpi_tty_radio = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_tty"));

	scpi_radio_conf = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_radio_conf"));
	instrument_type = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_type"));
	connect = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_connect"));
	scpi_output = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_output"));
	scpi_ip_addr = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_ip_addr"));
	scpi_gpib_addr = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_gpib_addr"));
	scpi_serial_tty = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_serial_tty"));
	scpi_id =  GTK_WIDGET(gtk_builder_get_object(builder, "scpi_id"));
	scpi_regex = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_regex"));
	scpi_cmd = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_cmd"));
	scpi_play = GTK_WIDGET(gtk_builder_get_object(builder, "scpi_play"));

	gtk_widget_show_all(scpi_panel);

	g_object_bind_property(scpi_net_radio, "active", network_conf, "visible", 0);
	g_signal_connect(scpi_net_radio, "toggled", G_CALLBACK(scpi_radio_cb), (gpointer) 1);

	g_object_bind_property(scpi_tty_radio, "active", tty_conf, "visible", 0);
	g_signal_connect(scpi_tty_radio, "toggled", G_CALLBACK(scpi_radio_cb), (gpointer) 2);

	g_object_bind_property(scpi_none_radio, "active", connect, "visible", G_BINDING_INVERT_BOOLEAN);
	g_object_bind_property(scpi_none_radio, "active", scpi_output, "visible", G_BINDING_INVERT_BOOLEAN);

	g_signal_connect(scpi_none_radio, "toggled", G_CALLBACK(scpi_radio_cb), (gpointer) 0);

	g_signal_connect(instrument_type, "changed",
			G_CALLBACK(instrument_type_cb), (gpointer) instrument_type);

	g_signal_connect(scpi_ip_addr, "changed",
			G_CALLBACK(scpi_text_entry_cb), (gpointer) 0);

	g_signal_connect(scpi_serial_tty, "changed",
			G_CALLBACK(scpi_text_entry_cb), (gpointer) 1);

	g_signal_connect(scpi_gpib_addr, "changed",
			G_CALLBACK(scpi_text_entry_cb), (gpointer) 2);

	g_signal_connect(scpi_regex, "changed",
			G_CALLBACK(scpi_text_entry_cb), (gpointer) 3);

	g_signal_connect(scpi_cmd, "changed",
			G_CALLBACK(scpi_text_entry_cb), (gpointer) 4);

	g_signal_connect(connect, "clicked",
			G_CALLBACK(connect_clicked_cb), NULL);

	g_signal_connect(scpi_play, "clicked",
			G_CALLBACK(scpi_cmd_cb), scpi_cmd);

	gtk_combo_box_set_active(GTK_COMBO_BOX(instrument_type), 0);

	gtk_widget_hide(network_conf);
	gtk_widget_hide(tty_conf);
	gtk_widget_hide(scpi_output);
	gtk_widget_hide(connect);

	return scpi_panel;
}

/* This is normally used for test, and the GUI is used for
 * setting up the test infrastructure
 */
static bool scpi_identify(void)
{
	/*
	 * Always return false
	 * If you want this to load, you are required to set the
	 * 'OSC_FORCE_PLUGIN' environmental variable
	 */
	return false;
}

struct osc_plugin plugin = {
	.name = "SCPI",
	.identify = scpi_identify,
	.init = scpi_init,
	.handle_item = scpi_handle_profile,
	.save_restore_attribs = scpi_sr_attribs,
};
