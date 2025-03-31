#!/bin/bash
set -xe

SRC_DIR=$(git rev-parse --show-toplevel 2>/dev/null ) || \
SRC_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && cd ../../ && pwd )
SRC_SCRIPT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

export NO_STRIP=1
BUILD_FOLDER=$SRC_DIR/build
STAGING_AREA=$SRC_SCRIPT/staging
APP_DIR=$STAGING_AREA/OscAppDir


get_tools() {
	mkdir -p $STAGING_AREA
	pushd $STAGING_AREA

	# download tools for creating the AppDir and the AppImage
	if [ ! -f linuxdeploy-x86_64.AppImage ];then
		wget "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20240109-1/linuxdeploy-x86_64.AppImage"
		chmod +x linuxdeploy-x86_64.AppImage
	fi

	if [ ! -f linuxdeploy-plugin-gtk.sh ];then
		wget "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh"
		chmod +x linuxdeploy-plugin-gtk.sh
	fi

	if [ ! -f linuxdeploy-plugin-appimage-x86_64.AppImage ];then
		wget https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/1-alpha-20230713-1/linuxdeploy-plugin-appimage-x86_64.AppImage
		chmod +x linuxdeploy-plugin-appimage-x86_64.AppImage
	fi

	popd
}


create_appdir()
{
	mkdir -p $STAGING_AREA
	pushd $STAGING_AREA
	rm -rf $APP_DIR

	sudo ldconfig

	# inside a docker image you can't run an appimage executable without privileges
	# so the solution is to extract the appimage first and only then to run it
	export APPIMAGE_EXTRACT_AND_RUN=1
	$STAGING_AREA/linuxdeploy-x86_64.AppImage \
		--appdir $APP_DIR \
		--executable $SRC_DIR/build/osc \
		--custom-apprun $SRC_DIR/CI/appimage_x86_64/AppRun \
		--desktop-file $SRC_DIR/CI/appimage_x86_64/osc.desktop \
		--icon-file $SRC_DIR/build/icons/osc.svg \
		--plugin gtk

	cp -R $SRC_DIR/build/plugins $APP_DIR/usr/bin
	cp -R $SRC_DIR/build/profiles $APP_DIR/usr/bin
	cp -R $SRC_DIR/xmls $APP_DIR/usr/bin
	cp -R $SRC_DIR/waveforms $APP_DIR/usr/bin
	cp -R $SRC_DIR/filters $APP_DIR/usr/bin
	cp -R $SRC_DIR/block_diagrams $APP_DIR/usr/bin
	cp -R $SRC_DIR/build/glade $APP_DIR/usr/bin
	cp $SRC_DIR/build/icons/* $APP_DIR/usr/bin/glade
	cp -R $APP_DIR/usr/share/icons $APP_DIR/usr/bin
	cp $SRC_DIR/build/styles.css $APP_DIR/usr/bin

	cp /usr/local/lib/libad9361.so $APP_DIR/usr/lib
	cp /usr/local/lib/libad9166.so $APP_DIR/usr/lib

	popd
}

create_appimage(){
	pushd $STAGING_AREA
	$STAGING_AREA/linuxdeploy-plugin-appimage-x86_64.AppImage --appdir $APP_DIR
	popd
}

move_appimage(){
	mv $STAGING_AREA/ADI_IIO_Oscilloscope-x86_64.AppImage $SRC_DIR/
	chmod +x $SRC_DIR/ADI_IIO_Oscilloscope-x86_64.AppImage
}

for arg in $@; do
	$arg
done
