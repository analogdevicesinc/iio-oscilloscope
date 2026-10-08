# Linux packaging for iio-oscilloscope via CPack -- produces a .deb
# (Debian/Ubuntu) or .rpm (Fedora/openSUSE/CentOS) alongside a .tar.gz, driven
# by `make package`. Modeled on libiio's cmake/LinuxPackaging.cmake.
#
# Scope: CI build artifact. osc depends on gtkdatabox and the ADI libraries
# (libiio, libad9361-iio, libad9166-iio), which are NOT in the Fedora/openSUSE
# repos and are built from source into /usr/local during CI
# (see CI/build_osc_rpm.sh). We therefore deliberately do NOT reproduce libiio's
# package-dependency auto-detection -- it queries dpkg/rpm for every dependency
# and hard-fails (FATAL_ERROR) on anything not installed as a distro package,
# which these source-built libraries are not. Nor do we bundle them. The
# resulting package carries osc's own files only; runtime dependencies are left
# to CPack/rpmbuild defaults. CPACK_SYSTEM_NAME is supplied on the cmake command
# line so the artifact filename carries the distro (e.g. Linux-Fedora-42).

set(CPACK_SET_DESTDIR ON)
set(CPACK_GENERATOR TGZ)

set(CPACK_PACKAGE_NAME iio-oscilloscope)
set(CPACK_PACKAGE_VENDOR "Analog Devices, Inc.")
set(CPACK_PACKAGE_VERSION_MAJOR ${OSC_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${OSC_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${OSC_VERSION_PATCH})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ADI IIO Oscilloscope")
set(CPACK_PACKAGE_CONTACT "Engineerzone <https://ez.analog.com/linux-software-drivers>")

# Determine the distribution so we pick the matching generator.
file(STRINGS /etc/os-release distro REGEX "^NAME=")
string(REGEX REPLACE "NAME=\"(.*)\"" "\\1" distro "${distro}")
file(STRINGS /etc/os-release disversion REGEX "^VERSION_ID=")
string(REGEX REPLACE "VERSION_ID=\"?(.*)\"?" "\\1" disversion "${disversion}")

if(distro MATCHES ".*Ubuntu.*" OR distro MATCHES ".*Debian.*")
	set(CPACK_GENERATOR ${CPACK_GENERATOR};DEB)
elseif(distro MATCHES ".*Fedora.*" OR distro MATCHES ".*SUSE.*")
	set(CPACK_GENERATOR ${CPACK_GENERATOR};RPM)
else()
	message(STATUS "found unknown distribution ${distro}, version ${disversion}")
	message(FATAL_ERROR "unsupported distribution for packaging; build with -DENABLE_PACKAGING=OFF")
endif()

if(CPACK_GENERATOR MATCHES "RPM")
	find_program(RPMBUILD rpmbuild)
	if(NOT RPMBUILD)
		message(FATAL_ERROR "To build RPM packages, please install rpmbuild")
	endif()
	set(CPACK_PACKAGE_RELOCATABLE OFF)
	# osc installs into standard system dirs already owned by other packages
	# (filesystem, hicolor-icon-theme, polkit); don't let the rpm claim them.
	set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
		/usr/share/applications
		/usr/share/icons
		/usr/share/icons/hicolor
		/usr/share/polkit-1
		/usr/share/polkit-1/actions
		/usr/lib/pkgconfig
		/usr/lib64/pkgconfig
	)
elseif(CPACK_GENERATOR MATCHES "DEB")
	find_program(DPKG_CMD dpkg)
	if(NOT DPKG_CMD)
		message(FATAL_ERROR "To build DEB packages, please install dpkg")
	endif()
endif()

include(CPack)
