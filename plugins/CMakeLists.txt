# Copyright 2018 Analog Devices, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 3, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with GNU Radio; see the file COPYING.  If not, write to
#  the Free Software Foundation, Inc., 51 Franklin Street,
#  Boston, MA 02110-1301, USA.

set(PLUGINS
	generic_dac
	fmcomms2
	fmcomms2_adv
	fmcomms5
	fmcomms6
	fmcomms11
	ad9371
	ad9371_adv
	adrv9009
	adrv9009_adv
	ad6676
	fmcadc3
	daq2
	ad9739a
	AD5628_1
	#AD7303
	cn0357
	cn0508
	cn0511
	cn0540
	pr_config
	motor_control
	lidar
	dmm
	debug
	spectrum_analyzer
	scpi
	ad9081
	ad9084
	adrv9002
	cf_axi_tdd
	xmw
)

set(LIBAD9166_PLUGINS cn0511)

set(LIBAD9361_PLUGINS
	fmcomms2
	fmcomms2_adv
	spectrum_analyzer
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	list(REMOVE_ITEM PLUGINS spectrum_analyzer scpi)
	set(EXTRA_WIN_LIBRARIES winpthread)
endif()

if("${LIBAD9166_LIBRARIES}" STREQUAL "")
	message(WARNING "libad9166 not found. The following plugins will not be installed: ${LIBAD9166_PLUGINS}")
	list(REMOVE_ITEM PLUGINS ${LIBAD9166_PLUGINS})
endif()

if("${LIBAD9361_LIBRARIES}" STREQUAL "")
	message(WARNING "libad9361 not found. The following plugins will not be installed: ${LIBAD9361_PLUGINS}")
	list(REMOVE_ITEM PLUGINS ${LIBAD9361_PLUGINS})
endif()

set(PLUGIN_GLADE_FILES "")

foreach(plugin ${PLUGINS})

	add_library(${plugin} ${plugin}.c)
	# We just include all the paths that work for all plugins. Keeping it on a per plugins
	# basis seems like unnecessary overhead/work
	target_include_directories(${plugin} PRIVATE
		${GTK_INCLUDE_DIRS}
		${LIBIIO_INCLUDE_DIRS}
		${LIBXML2_INCLUDE_DIRS}
		${LIBAD9361_INCLUDE_DIRS}
		${LIBAD9166_INCLUDE_DIRS}
	)
	target_link_libraries(${plugin} PRIVATE
		${GTK_LIBRARIES}
		${LIBIIO_LIBRARIES}
		${LIBXML2_LIBRARIES}
		${LIBAD9361_LIBRARIES}
		${LIBAD9166_LIBRARIES}
		${EXTRA_WIN_LIBRARIES}
		osc
		m
	)

	set_target_properties(${plugin} PROPERTIES
		PREFIX ""
	)

endforeach()

install(TARGETS ${PLUGINS}
	LIBRARY DESTINATION ${PLIB_DEST}
	RUNTIME DESTINATION ${PLIB_DEST}
)
