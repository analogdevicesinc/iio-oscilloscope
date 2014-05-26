/**
 * Copyright 2012-2013(c) Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef __CONFIG_H__
#define __CONFIG_H__

#ifndef PREFIX
#	define PREFIX "/usr/local/"
#endif

#define OSC_GLADE_FILE_PATH PREFIX "/share/multiosc/"
#define OSC_PLUGIN_PATH PREFIX "/lib/multiosc/"
#define OSC_XML_PATH PREFIX "/lib/multiosc/xmls"
#define OSC_FILTER_FILE_PATH PREFIX "/lib/multiosc/filters"
#define OSC_WAVEFORM_FILE_PATH PREFIX "/lib/multiosc/waveforms"
#define OSC_PROFILES_FILE_PATH PREFIX "/lib/multiosc/profiles"

#endif
