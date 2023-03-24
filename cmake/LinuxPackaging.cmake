# support creating some basic binpkgs via `make package`
set(CPACK_SET_DESTDIR ON)
set(CPACK_GENERATOR TGZ)

FIND_PROGRAM(RPMBUILD_CMD rpmbuild)
if (RPMBUILD_CMD)
	# Check if optional dependency is included
	if(LIBAD9361_LIBRARIES)
			set(LIBAD9361_RPM ", libad9361 >= 0.2")
	endif()
	if(LIBAD9166_LIBRARIES)
			set(LIBAD9166_RPM ", libad9166 >= 0.2")
	endif()
	# Manual setup of rpm requires, Fedora >= 36 centric
	set(CPACK_PACKAGE_RELOCATABLE OFF)
	set(CPACK_GENERATOR ${CPACK_GENERATOR};RPM)
	set(CPACK_RPM_PACKAGE_REQUIRES "libiio >= 0.19, gtk2 >= 2.24.32, gtkdatabox >= 0.9.3, jansson >= 2.12, matio >= 1.5.17, fftw >= 3.3.8, curl >= 7.68.0 ${LIBAD9361_RPM} ${LIBAD9166_RPM}")
	message(STATUS "Package dependencies (.rpm): " ${CPACK_RPM_PACKAGE_REQUIRES})
endif()

FIND_PROGRAM(DEBBUILD_CMD dpkg)
if (DEBBUILD_CMD)
	# Check if optional dependency is included
	if(LIBAD9361_LIBRARIES)
			set(LIBAD9361_DEB ", libad9361-0 (>= 0.2) | libad9361 (>= 0.2)")
	endif()
	if(LIBAD9166_LIBRARIES)
			set(LIBAD9166_DEB ", libad9166 (>= 0.2)")
	endif()
	set(CPACK_GENERATOR ${CPACK_GENERATOR};DEB)

	set(CPACK_DEBIAN_PACKAGE_DEPENDS "libiio0 (>= 0.19) | libiio (>= 0.19), libgtk2.0-0 (>= 2.24.32), libgtkdatabox0 (>= 0.9.3), libjansson4 (>= 2.12), libmatio9 (>= 1.5.17), libfftw3-3 (>= 3.3.8), libcurl4 (>= 7.68.0) ${LIBAD9361_DEB} ${LIBAD9166_DEB}")
	message(STATUS "Package dependencies (.deb): " ${CPACK_DEBIAN_PACKAGE_DEPENDS})
endif()

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)
set(CPACK_PACKAGE_VERSION_MAJOR ${OSC_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${OSC_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${OSC_VERSION_GIT})
set(CPACK_BUNDLE_NAME osc)
set(CPACK_PACKAGE_VERSION ${OSCIO_VERSION})
if (DEBBUILD_CMD)
	# debian specific package settings
	set(CPACK_PACKAGE_CONTACT "Engineerzone <https://ez.analog.com/sw-interface-tools>")
endif()

include(CPack)
