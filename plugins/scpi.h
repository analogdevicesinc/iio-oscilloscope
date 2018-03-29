/**
 * Copyright (C) 2012-2013 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __OSC_SCPI_H__
#define __OSC_SCPI_H__

static bool scpi_rx_connected_flag = false;
bool (*scpi_rx_connected)(void) = NULL;
void (*scpi_rx_trigger_sweep)(void) = NULL;
void (*scpi_rx_set_center_frequency)(unsigned long long) = NULL;
void (*scpi_rx_set_span_frequency)(unsigned long long) = NULL;
void (*scpi_rx_set_bandwith)(unsigned int, unsigned int) = NULL;
void (*scpi_rx_set_bandwith_auto)(double) = NULL;
void (*scpi_rx_setup)(void) = NULL;
void (*scpi_rx_set_averaging)(int average) = NULL;
int (*scpi_rx_set_marker_freq)(unsigned int, unsigned long long) = NULL;
int (*scpi_rx_get_marker_level)(unsigned int, bool, double *) = NULL;
int (*scpi_rx_get_marker_freq)(unsigned int, bool, double *) = NULL;
int (*scpi_connect_counter)(void) = NULL;
int (*scpi_counter_get_freq)(double *freq, double *target_freq) = NULL;


static bool scpi_connect_functions(void)
{
	/* Already installed ? */
	if (scpi_rx_connected_flag)
		return true;

	/* Plugin installed ? */
	if (!plugin_installed("SCPI"))
		return false;

	*(void **)(&scpi_rx_connected) = plugin_dlsym("SCPI", "scpi_rx_connected");
	if (*scpi_rx_connected == NULL)
		return false;

	*(void **)(&scpi_rx_trigger_sweep) = plugin_dlsym("SCPI", "scpi_rx_trigger_sweep");
	if (*scpi_rx_trigger_sweep == NULL)
		return false;

	*(void **)(&scpi_rx_set_center_frequency) = plugin_dlsym("SCPI", "scpi_rx_set_center_frequency");
	if (*scpi_rx_set_center_frequency == NULL)
		return false;

	*(void **)(&scpi_rx_set_span_frequency) = plugin_dlsym("SCPI", "scpi_rx_set_span_frequency");
	if (*scpi_rx_set_span_frequency == NULL)
		return false;

	*(void **)(&scpi_rx_set_bandwith) = plugin_dlsym("SCPI", "scpi_rx_set_bandwith");
	if (*scpi_rx_set_bandwith == NULL)
		return false;

	*(void **)(&scpi_rx_set_bandwith_auto) = plugin_dlsym("SCPI", "scpi_rx_set_bandwith_auto");
	if (*scpi_rx_set_bandwith_auto == NULL)
		return false;

	*(void **)(&scpi_rx_setup) = plugin_dlsym("SCPI", "scpi_rx_setup");
	if (*scpi_rx_setup == NULL)
		return false;

	*(void **)(&scpi_rx_set_averaging) = plugin_dlsym("SCPI", "scpi_rx_set_averaging");
	if (*scpi_rx_set_averaging == NULL)
		return false;

	*(void **)(&scpi_rx_set_marker_freq) = plugin_dlsym("SCPI", "scpi_rx_set_marker_freq");
	if (*scpi_rx_set_marker_freq == NULL)
		return false;

	*(void **)(&scpi_rx_get_marker_level) = plugin_dlsym("SCPI", "scpi_rx_get_marker_level");
	if (*scpi_rx_get_marker_level == NULL)
		return false;

	*(void **)(&scpi_rx_get_marker_freq) = plugin_dlsym("SCPI", "scpi_rx_get_marker_freq");
	if (*scpi_rx_get_marker_freq == NULL)
		return false;

	*(void **)(&scpi_connect_counter) = plugin_dlsym("SCPI", "scpi_connect_counter");
	if (*scpi_connect_counter == NULL)
		return false;

	*(void **)(&scpi_counter_get_freq) = plugin_dlsym("SCPI", "scpi_counter_get_freq");
	if (*scpi_counter_get_freq == NULL)
		return false;

	scpi_rx_connected_flag = true;
	return true;
}

#endif
