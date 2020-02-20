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

cmake_minimum_required(VERSION 2.8.7)

set(OSC_VERSION_MAJOR 0)
set(OSC_VERSION_MINOR 8)
set(OSC_VERSION "${OSC_VERSION_MAJOR}.${OSC_VERSION_MINOR}")
project(iio-oscilloscope C)

if (WIN32)
	string(TIMESTAMP BUILD_YEAR "%Y")
endif()

# Get the GIT hash of the latest commit
include(FindGit OPTIONAL)
if (GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE
		)

	execute_process(
		COMMAND ${GIT_EXECUTABLE} show -s --pretty=format:"%ct" HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_COMMIT_TIMESTAMP
		OUTPUT_STRIP_TRAILING_WHITESPACE
		)

	execute_process(
		COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_BRANCH
		OUTPUT_STRIP_TRAILING_WHITESPACE
		)

	execute_process(
		COMMAND ${GIT_EXECUTABLE} describe --abbrev=7 --dirty --always
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE GIT_HASH
		OUTPUT_STRIP_TRAILING_WHITESPACE
		)

	set(OSC_VERSION "${GIT_BRANCH}-g${GIT_HASH}")
	set(GIT_VERSION "-DGIT_VERSION=\"${GIT_VERSION}\"")
	set(GIT_COMMIT_TIMESTAMP "-DGIT_COMMIT_TIMESTAMP=${GIT_COMMIT_TIMESTAMP}")
else()
	set(OSC_VERSION v${PROJECT_VERSION})
endif()

set(BUILD_SHARED_LIBS ON CACHE BOOL "Build shared libraries")

link_directories(${CMAKE_PREFIX_PATH}/lib)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	add_compile_options("-F/Library/Frameworks/")
	link_libraries("-F/Library/Frameworks/")
endif()

add_definitions(-Dlinux -D_GNU_SOURCE 
	-DOSC_VERSION="${OSC_VERSION}"
	${GIT_VERSION}
	${GIT_COMMIT_TIMESTAMP}
	-Wall -Werror
	-DGTK_DISABLE_DEPRECATED
)

if(UNIX)
	add_definitions(-DFRU_FILES="${CMAKE_PREFIX_PATH}/lib/fmc-tools/")
endif()

set(OSC_SRC osc.c oscplot.c datatypes.c iio_widget.c iio_utils.c
	fru.c dialogs.c trigger_dialog.c xml_utils.c libini/libini.c
	libini2.c phone_home.c plugins/dac_data_manager.c
	plugins/fir_filter.c eeprom.c osc_preferences.c)

find_package(PkgConfig)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GTK REQUIRED gtk+-2.0)
pkg_check_modules(GTHREAD REQUIRED gthread-2.0)
pkg_check_modules(GTKDATABOX REQUIRED gtkdatabox)
pkg_check_modules(FFTW3 REQUIRED fftw3)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)
pkg_check_modules(LIBCURL REQUIRED libcurl)
pkg_check_modules(JANSSON REQUIRED jansson)
pkg_check_modules(MATIO REQUIRED matio)

find_library(LIBIIO_LIBRARIES iio)
find_path(LIBIIO_INCLUDE_DIRS iio.h)

find_library(LIBAD9361_LIBRARIES ad9361)
find_path(LIBAD9361_INCLUDE_DIRS ad9361.h)

find_library(LIBSERIALPORT_LIBRARIES serialport)
find_path(LIBSERIALPORT_INCLUDE_DIR libserialport.h)
if (LIBSERIALPORT_LIBRARIES AND LIBSERIALPORT_INCLUDE_DIR)
	option(WITH_SERIAL_BACKEND "Enable the serial backend" ON)

	add_definitions(-DSERIAL_BACKEND=1)
else()
	set(LIBSERIALPORT_LIBRARIES "")
	set(LIBSERIALPORT_INCLUDE_DIR "")
endif()

include_directories(
	${GLIB_INCLUDE_DIRS}
	${GLIBCONFIG_INCLUDE_DIRS}
	${GTK_INCLUDE_DIRS}
	${GTHREAD_INCLUDE_DIRS}
	${GTKDATABOX_INCLUDE_DIRS}
	${FFTW3_INCLUDE_DIRS}
	${LIBIIO_INCLUDE_DIRS}
	${LIBXML2_INCLUDE_DIRS}
	${LIBCURL_INCLUDE_DIRS}
	${LIBSERIALPORT_INCLUDE_DIR}
	${JANSSON_INCLUDE_DIRS}
	${MATIO_INCLUDE_DIRS}
	${HDF5_INCLUDE_DIRS}
	${LIBAD9361_INCLUDE_DIRS}
	${CMAKE_SOURCE_DIR}/src
)

# On some newer systems (Ubuntu) it tries to link with libhdf5.so,
# which doesn't seem to exist; but we only need -lmatio, so force it here
set(MATIO_LIBRARIES -lmatio)

set(LIBOSC_LIBS
	${GLIB_LIBRARIES}
	${GLIBCONFIG_LIBRARIES}
	${GTK_LIBRARIES}
	${GTHREAD_LIBRARIES}
	${GTKDATABOX_LIBRARIES}
	${FFTW3_LIBRARIES}
	${LIBIIO_LIBRARIES}
	${LIBXML2_LIBRARIES}
	${LIBCURL_LIBRARIES}
	${LIBSERIALPORT_LIBRARIES}
	${JANSSON_LIBRARIES}
	${MATIO_LIBRARIES}
	${LIBAD9361_LIBRARIES}
)

configure_file(config.h.cmakein ${CMAKE_CURRENT_SOURCE_DIR}/config.h @ONLY)

add_library(osc ${OSC_SRC})
target_link_libraries(osc ${LIBOSC_LIBS})
set_target_properties(osc PROPERTIES
	VERSION ${OSC_VERSION}
	SOVERSION ${OSC_VERSION_MAJOR}
)

add_executable(oscmain WIN32 oscmain.c)
set_target_properties(oscmain PROPERTIES
	VERSION ${OSC_VERSION}
	OUTPUT_NAME osc
)
target_link_libraries(oscmain LINK_PRIVATE ${LIBOSC_LIBS} osc)

install(TARGETS osc LIBRARY DESTINATION lib)
install(TARGETS oscmain RUNTIME DESTINATION bin)

# Set default CMAKE_PREFIX_PATH to CMAKE_SYSTEM_PREFIX_PATH
set(CMAKE_PREFIX_PATH ${CMAKE_SYSTEM_PREFIX_PATH})

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	configure_file(org.adi.pkexec.osc.policy.cmakein ${CMAKE_CURRENT_BINARY_DIR}/org.adi.pkexec.osc.policy @ONLY)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.adi.pkexec.osc.policy
		DESTINATION /usr/share/polkit-1/actions/)
	# setup exec script
	configure_file(osc-wrapper.cmakein ${CMAKE_CURRENT_SOURCE_DIR}/osc-wrapper @ONLY)
	install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/osc-wrapper
		PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
		DESTINATION bin)
endif()

set(PLIB_DEST lib/osc)
set(SHARE_DEST share/osc)
set(GLADE_FILES_DEST ${SHARE_DEST}/glade)

set(PLIB_DEST lib/osc)
set(SHARE_DEST share/osc)

file(GLOB GLADE_FILES glade/*.glade)
file(GLOB ICON_FILES icons/*)

install(FILES ${GLADE_FILES} ${ICON_FILES} DESTINATION ${SHARE_DEST})

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	configure_file(adi-osc.desktop.cmakein ${CMAKE_CURRENT_BINARY_DIR}/adi-osc.desktop @ONLY)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/adi-osc.desktop
		DESTINATION /usr/share/applications/)

	# install theme icons
	install(FILES icons/osc16.png RENAME adi-osc.png
		DESTINATION /usr/share/icons/hicolor/16x16/apps)
	install(FILES icons/osc32.png RENAME adi-osc.png
		DESTINATION /usr/share/icons/hicolor/32x32/apps)
	install(FILES icons/osc64.png RENAME adi-osc.png
		DESTINATION /usr/share/icons/hicolor/64x64/apps)
	install(FILES icons/osc128.png RENAME adi-osc.png
		DESTINATION /usr/share/icons/hicolor/128x128/apps)
	install(FILES icons/osc256.png RENAME adi-osc.png
		DESTINATION /usr/share/icons/hicolor/256x256/apps)
	install(FILES icons/osc.svg RENAME adi-osc.svg
		DESTINATION /usr/share/icons/hicolor/scalable/apps)

	# This is a bit "ugly". It makes sure that the hicolor theme directory updates
	# it's modification date. This makes sure that osc icons are loaded right away.
	# We can also just remove this and assume the user has do this manually or run
	# something like `gtk-update-icon-cache -f /usr/share/icons/hicolor`.
	install(CODE "EXECUTE_PROCESS(COMMAND sh -c \"touch /usr/share/icons/hicolor\")")
endif()

install(DIRECTORY icons DESTINATION ${SHARE_DEST})

foreach(plib_dest xmls filters waveforms profiles block_diagrams)
	install(DIRECTORY ${plib_dest} DESTINATION ${PLIB_DEST})
endforeach()

add_subdirectory(plugins)
