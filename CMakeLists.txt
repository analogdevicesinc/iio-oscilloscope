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

# needed to use VERSION in 'project()'
cmake_policy(SET CMP0048 NEW)
set(OSC_VERSION_MAJOR 0)
set(OSC_VERSION_MINOR 8)
set(OSC_VERSION "${OSC_VERSION_MAJOR}.${OSC_VERSION_MINOR}")
project(iio-oscilloscope
	VERSION ${OSC_VERSION}
	LANGUAGES C
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	string(TIMESTAMP BUILD_YEAR "%Y")
endif()

include(GNUInstallDirs)

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
		COMMAND ${GIT_EXECUTABLE} show -s --pretty=format:%ct HEAD
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

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	add_compile_options("-F/Library/Frameworks/")
	link_libraries("-F/Library/Frameworks/")
	# Fix linking on 10.14+ with Homebrew. See https://stackoverflow.com/questions/54068035
	LINK_DIRECTORIES(/usr/local/lib)
endif()

add_definitions(-D_GNU_SOURCE
	-DOSC_VERSION="${OSC_VERSION}"
	${GIT_VERSION}
	${GIT_COMMIT_TIMESTAMP}
	-DGTK_DISABLE_DEPRECATED
	-D_POSIX_C_SOURCE=200809L
)

add_compile_options(-Wall -Wextra -Werror
	-Wno-unused-parameter
	-Wno-error=unused-but-set-variable
	-Wno-error=unused-function
	-Wimplicit-fallthrough
	-Wvla
	-std=gnu90
	-g -O2
)

if(UNIX)
	add_definitions(-Dlinux -DFRU_FILES="${CMAKE_PREFIX_PATH}/lib/fmc-tools/")
endif()

set(OSC_SRC osc.c oscplot.c datatypes.c iio_widget.c iio_utils.c
	fru.c dialogs.c trigger_dialog.c xml_utils.c libini/libini.c
        libini2.c phone_home.c plugins/dac_data_manager.c
        plugins/fir_filter.c eeprom.c osc_preferences.c)

find_package(PkgConfig)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GTK REQUIRED gtk+-3.0)
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
if ((NOT LIBAD9361_LIBRARIES) AND (NOT LIBAD9361_INCLUDE_DIRS))
	set(LIBAD9361_LIBRARIES "")
	set(LIBAD9361_INCLUDE_DIRS "")
endif()

find_library(LIBAD9166_LIBRARIES ad9166)
find_path(LIBAD9166_INCLUDE_DIRS ad9166.h)
if ((NOT LIBAD9166_LIBRARIES) AND (NOT LIBAD9166_INCLUDE_DIRS))
	set(LIBAD9166_LIBRARIES "")
	set(LIBAD9166_INCLUDE_DIRS "")
endif()

find_library(LIBSERIALPORT_LIBRARIES serialport)
find_path(LIBSERIALPORT_INCLUDE_DIR libserialport.h)
if (LIBSERIALPORT_LIBRARIES AND LIBSERIALPORT_INCLUDE_DIR)
	option(WITH_SERIAL_BACKEND "Enable the serial backend" ON)

	add_definitions(-DSERIAL_BACKEND=1)
else()
	set(LIBSERIALPORT_LIBRARIES "")
	set(LIBSERIALPORT_INCLUDE_DIR "")
endif()

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
	${LIBAD9166_LIBRARIES}
	dl
)

configure_file(config.h.cmakein ${CMAKE_CURRENT_SOURCE_DIR}/config.h @ONLY)

if(CMAKE_COMPILER_IS_GNUCC)
	# TO DO: remove -Wno-deprecated-declarations -Wno-unused-function -Wno-unused-variable
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Wno-unused-function -Wno-unused-variable")
endif()

# Override on distributions that do not use a global polkit prefix (e.g. NixOS)
if(NOT DEFINED CMAKE_POLKIT_PREFIX)
	set(CMAKE_POLKIT_PREFIX /usr)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	# This is a bit hacky but looks to be the less intrusive way I could think of
	# to handle this. This is only useful when creating the windows installer together
	# with the CI script. In that case, we have a sysroot with all dependecies where
	# we want to install our artifacts. Hence, the first feeling is to just point
	# CMAKE_INSTALL_PREFIX to that sysroot and we should fine. Well, that will
	# break auto genarated files as config.h since the sysroot path won't exist in
	# the target machine. Cpack gives us an easy way to package our artifacts in a tar.z
	# archive with all the installation paths being relative (GNU DIR paths. The only
	# full paths used are specific to linux builds). Hence, the CI script can just call
	# 'make package' and untar it to the right place in the sysroot path. On top of
	# this, it also gives us more flexibility to set CMAKE_INSTALL_PREFIX to a value
	# that will be valid for the auto genarated files (there are some subtleties with
	# this for windows). To finish it up, this also leaves the door open to just use
	# cmake to package our windows installer (together with nsis).
	add_definitions(-D__MINGW__)
	set(CPACK_BINARY_NSIS OFF)
	set(CPACK_GENERATOR TZ)
	include(CPack)

	list(APPEND LIBOSC_LIBS winpthread)
	list(REMOVE_ITEM OSC_SRC eeprom.c)
	set(OSCMAIN_RC oscicon.rc)
	set_source_files_properties(${OSCMAIN_RC} PROPERTIES LANGUAGE RC)
	add_compile_options(-mwindows)
	set(WIN_LINK_FLAGS "-Wl,--export-all-symbols -Wl,--subsystem,windows")
else()
	add_link_options(-rdynamic)
endif()

add_library(osc ${OSC_SRC})
target_include_directories(osc PRIVATE
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
	${LIBAD9166_INCLUDE_DIRS}
)
target_link_libraries(osc PRIVATE ${LIBOSC_LIBS})
set_target_properties(osc PROPERTIES
	LINK_FLAGS "${WIN_LINK_FLAGS}"
	VERSION ${PROJECT_VERSION}
	SOVERSION ${OSC_VERSION_MAJOR}
)

add_executable(oscmain WIN32 oscmain.c ${OSCMAIN_RC})
target_include_directories(oscmain PRIVATE
	${GTK_INCLUDE_DIRS}
	${LIBIIO_INCLUDE_DIRS}
)
target_link_libraries(oscmain PRIVATE
	${GTK_LIBRARIES}
	${LIBIIO_LIBRARIES}
	osc
)
set_target_properties(oscmain PROPERTIES OUTPUT_NAME osc)

install(TARGETS osc
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(TARGETS oscmain RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Set default CMAKE_PREFIX_PATH to CMAKE_SYSTEM_PREFIX_PATH
set(CMAKE_PREFIX_PATH ${CMAKE_SYSTEM_PREFIX_PATH})
# Flatpaks have policies defined in the yml manifest file
if (${CMAKE_INSTALL_PREFIX} MATCHES "/app")
set(IS_FLATPAK_CI ON)
endif()
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux" AND NOT(IS_FLATPAK_CI))
	configure_file(org.adi.pkexec.osc.policy.cmakein ${CMAKE_CURRENT_BINARY_DIR}/org.adi.pkexec.osc.policy @ONLY)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.adi.pkexec.osc.policy
		DESTINATION ${CMAKE_POLKIT_PREFIX}/${CMAKE_INSTALL_DATADIR}/polkit-1/actions/)
	# setup exec script
	configure_file(osc-wrapper.cmakein ${CMAKE_CURRENT_BINARY_DIR}/osc-wrapper @ONLY)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/osc-wrapper
		PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
		DESTINATION bin)
endif()

set(PLIB_DEST ${CMAKE_INSTALL_LIBDIR}/osc)
set(SHARE_DEST ${CMAKE_INSTALL_DATADIR}/osc)
set(GLADE_FILES_DEST ${SHARE_DEST}/glade)

file(GLOB GLADE_FILES glade/*.glade)
file(GLOB ICON_FILES icons/*)

install(FILES ${GLADE_FILES} ${ICON_FILES} DESTINATION ${GLADE_FILES_DEST})

set(GLADE_FILES_DEST ${CMAKE_CURRENT_BINARY_DIR}/glade)

file(COPY ${GLADE_FILES} DESTINATION ${GLADE_FILES_DEST})
file(COPY ${ICON_FILES} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/icons")

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	configure_file(adi-osc.desktop.cmakein ${CMAKE_CURRENT_BINARY_DIR}/adi-osc.desktop @ONLY)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/adi-osc.desktop
		DESTINATION ${CMAKE_INSTALL_DATADIR}/applications/)

	# install theme icons
	install(FILES icons/osc16.png RENAME adi-osc.png
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/16x16/apps)
	install(FILES icons/osc32.png RENAME adi-osc.png
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps)
	install(FILES icons/osc64.png RENAME adi-osc.png
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/64x64/apps)
	install(FILES icons/osc128.png RENAME adi-osc.png
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps)
	install(FILES icons/osc256.png RENAME adi-osc.png
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
	install(FILES icons/osc.svg RENAME adi-osc.svg
		DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
	
	install(FILES ./styles.css DESTINATION ${SHARE_DEST})

	# This is a bit "ugly". It makes sure that the hicolor theme directory updates
	# it's modification date. This makes sure that osc icons are loaded right away.
	# We can also just remove this and assume the user has do this manually or run
	# something like `gtk-update-icon-cache -f /usr/share/icons/hicolor`.
	install(CODE "EXECUTE_PROCESS(COMMAND sh -c \"touch ${CMAKE_INSTALL_FULL_DATADIR}/icons/hicolor\")")
endif()

install(DIRECTORY icons DESTINATION ${SHARE_DEST})

file(GLOB PROFILES profiles/*.cmakein)
foreach(PROFILE IN LISTS PROFILES)
	string(REPLACE ".cmakein" "" PROFILE_TARGET ${PROFILE})
	get_filename_component(FROFILE_TARGET_BASENAME ${PROFILE_TARGET} NAME)
	configure_file(${PROFILE} ${CMAKE_CURRENT_BINARY_DIR}/profiles/${FROFILE_TARGET_BASENAME})
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/profiles/${FROFILE_TARGET_BASENAME}
		DESTINATION ${PLIB_DEST}/profiles/
	)
endforeach()

file(GLOB PROFILES profiles/*.ini)
install(FILES ${PROFILES} DESTINATION ${PLIB_DEST}/profiles/)

foreach(plib_dest xmls filters waveforms block_diagrams)
	install(DIRECTORY ${plib_dest} DESTINATION ${PLIB_DEST})
endforeach()

set(MODEL_FILE "styles.css")
set(TARGET_PATH "${CMAKE_CURRENT_BINARY_DIR}/${MODEL_FILE}")
set(MODEL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${MODEL_FILE}")
if(NOT EXISTS "${TARGET_PATH}")
    file(COPY "${MODEL_FILE}"  DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endif()

add_subdirectory(plugins)
