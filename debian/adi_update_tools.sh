#!/bin/sh

if [ "$(id -u)" != "0" ] ; then
	echo "This script must be run as root"
	exit 1
fi

wget --spider -nv http://github.com/analogdevicesinc
EC=$?
if [ $EC -ne 0 ];then
   ifconfig
   echo "\n\nNetwork Connection: FAILED\n"
   exit $EC
fi

#find md5of this file
md5_self=`md5sum $0`

# Keeps the scripts as the first thing, so we can check for updated
# scripts ...
# repository:branch:make_target

BUILDS_DEV="linux_image_ADI-scripts:origin/master \
	fmcomms1-eeprom-cal:origin/master \
	libiio:origin/master \
	libad9361-iio:origin/master \
	iio-cmdsrv:origin/master \
	iio-oscilloscope:origin/master \
	fru_tools:origin/master \
	iio-fm-radio:origin/master \
	jesd-eye-scan-gtk:origin/master \
	diagnostic_report:origin/master \
	colorimeter:origin/master \
	mathworks_tools:origin/master"

BUILDS_2015_R1="linux_image_ADI-scripts:origin/master \
	fmcomms1-eeprom-cal:origin/2015_R1 \
	libiio:origin/2015_R1 \
	libad9361-iio:origin/master \
	iio-oscilloscope:origin/2015_R1\
	fru_tools:origin/2015_R1 \
	iio-fm-radio:origin/2015_R1 \
	jesd-eye-scan-gtk:origin/2015_R1 \
	diagnostic_report:origin/master \
	colorimeter:origin/2015_R1 \
	mathworks_tools:origin/2015_R1"

BUILDS_2014_R2="linux_image_ADI-scripts:origin/master \
	fmcomms1-eeprom-cal:origin/2014_R2 \
	libiio:origin/2014_R2 \
	iio-oscilloscope:origin/2014_R2 \
	fru_tools:origin/2014_R2 \
	iio-fm-radio:origin/2014_R2 \
	jesd-eye-scan-gtk:origin/2014_R2 \
	diagnostic_report:origin/master \
	colorimeter:origin/2015_R1 \
	mathworks_tools:origin/2014_R2"

do_build ()
{
  local prj=$1
  local target=$2
  make clean;
  make -j3 $target && make install && echo "\n Building $prj target $target finished Successfully\n" ||
	echo "Building $prj Failed\n"
}

# Allow selective builds by default build the latest release branches
if [ "$1" = "dev" ]
then
  BUILDS=$BUILDS_DEV
elif [ "$1" = "2014_R2" ]
then
  BUILDS=$BUILDS_2014_R2
elif [ -n "$1" ]
then
  BUILDS=$1
else
  BUILDS=$BUILDS_2015_R1
fi

for i in $BUILDS
do
  REPO=`echo $i | cut -d':' -f1`
  BRANCH=`echo $i | cut -s -d':' -f2`
  TARGET=`echo $i | cut -s -d':' -f3`

# selective build without branch? use master
  if [ -z $BRANCH ]
  then
    echo HERE
    BRANCH=origin/master
    TARGET=""
  fi

  cd /usr/local/src

  if [ -d $REPO ]
  then
    cd ./$REPO
    echo "\n *** Updating $REPO BRANCH $BRANCH ***"
    dirty=`git diff --shortstat 2> /dev/null | tail -n1`
    if [ "$dirty" != "" ]
    then
      echo "Tree is dirty - generating branch" `date +"%F"`
      git branch `date +"%F"`
    fi
    git checkout -f $BRANCH
    make uninstall 2>/dev/null
    git fetch
    git checkout -f $BRANCH 2>/dev/null
    cd ..
  else
    echo "\n *** Cloning $REPO ***"
    git clone https://github.com/analogdevicesinc/$REPO.git || continue
  fi

  echo "\n *** Building $REPO ***"
  cd ./$REPO

# Handle some specialties here
  if [ $REPO = "linux_image_ADI-scripts" ]
  then
    new=`md5sum ./adi_update_tools.sh`
    if [ "$new" = "$md5_self" ]
    then
      echo ./adi_update_tools.sh script is the same, continuing
      # Now we are sure we are using the latest, make sure the pre-reqs
      # are installed. If someone reports an error, fix the list.
      apt-get -y install libgtk2.0-dev libgtkdatabox-dev libmatio-dev \
        libfftw3-dev libxml2 libxml2-dev bison flex libavahi-common-dev \
        libavahi-client-dev libcurl4-openssl-dev libjansson-dev cmake
      if [ "$?" -ne "0" ] ; then
        echo Catastrophic error in prerequisite packages,  please report error to:
        echo https://ez.analog.com/community/linux-device-drivers/linux-software-drivers
        exit
      fi
      #Misc fixup:
      sed -i 's/wiki.analog.org/wiki.analog.com/g'  /etc/update-motd.d/10-help-text
    else
      # run the new one instead, and then just quit
      echo ./adi_update_tools.sh has been updated, switching to new one
      ./adi_update_tools.sh $@
      exit
    fi
  elif [ $REPO = "iio-cmdsrv" ]
  then
    cd ./server
  elif [ $REPO = "libiio" ]
  then
    # Just in case an old version is still under /usr/local
    rm -f /usr/local/lib/libiio.so* /usr/local/sbin/iiod \
        /usr/local/bin/iio_* /usr/local/include/iio.h \
        /usr/local/lib/pkgconfig/libiio.pc

    # Remove old init.d links
    rm -f /etc/init.d/iiod.sh /etc/init.d/iiod
    update-rc.d -f iiod remove
    update-rc.d -f iiod.sh remove

    # Install the startup script of iiod here, as cmake won't do it
    install -m 0755 debian/iiod.init /etc/init.d/iiod
    update-rc.d iiod defaults 99 01

    rm -rf build

    # Apparently, under undetermined circumstances CMake will output the build
    # files to the source directory instead of the current directory.
    # Here we use the undocumented -B and -H options to force the directory
    # where the build files are generated.
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_COLOR_MAKEFILE=OFF -Bbuild -H.
    cd build
  elif [ $REPO = "libad9361-iio" ]
  then
	  rm -rf build

	  # Same as above
	  cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DCMAKE_COLOR_MAKEFILE=OFF -Bbuild -H.
	  cd build
  elif [ $REPO = "thttpd" ]
  then
    ./configure
  elif [ $REPO = "mathworks_tools" ]
  then
    cd ./motor_control/linux_utils/
  fi

  do_build $REPO $TARGET
done
