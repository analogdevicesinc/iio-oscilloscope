#/bin/bash
set -xe

export WORKDIR=/home/$USER/

init_env() {

export ARCH=x86_64
export MINGW_VERSION=ucrt64
export CC=/${MINGW_VERSION}/bin/${ARCH}-w64-mingw32-gcc.exe
export CXX=/${MINGW_VERSION}/bin/${ARCH}-w64-mingw32-g++.exe

export JOBS=-j9
export STAGING_DIR="/ucrt64"
export MAKE_BIN="/bin/make"
export MAKE="$MAKE_BIN $JOBS"
export CMAKE_OPTS="-DCMAKE_C_COMPILER:FILEPATH=${CC} \
-DCMAKE_CXX_COMPILER:FILEPATH=${CXX} \
-DCMAKE_MAKE_PROGRAM:FILEPATH=${MAKE_BIN} \
-DPKG_CONFIG_EXECUTABLE=/$MINGW_VERSION/bin/pkg-config.exe \
-DCMAKE_MODULE_PATH=$STAGING_DIR \
-DCMAKE_PREFIX_PATH=$STAGING_DIR/lib/cmake \
-DCMAKE_BUILD_TYPE=RelWithDebInfo \
-DCMAKE_STAGING_PREFIX=$STAGING_DIR \
-DCMAKE_INSTALL_PREFIX=$STAGING_DIR"

export CMAKE="/$MINGW_VERSION/bin/cmake"
export HOST=${MINGW_VERSION}-w64-mingw32
export AUTOCONF_OPTS="--prefix=/ucrt64 \
        --host=${ARCH}-w64-mingw32 \
        --enable-shared \
        --disable-static"
}

install_pacman_deps() {
WINDEPS="mingw-w64-ucrt-x86_64-gdb \
mingw-w64-ucrt-x86_64-dlfcn \
mingw-w64-ucrt-x86_64-libserialport \
mingw-w64-ucrt-x86_64-libusb \
mingw-w64-ucrt-x86_64-matio \
mingw-w64-ucrt-x86_64-fftw \
mingw-w64-ucrt-x86_64-autotools \
mingw-w64-ucrt-x86_64-gtk3 \
mingw-w64-ucrt-x86_64-glib2 \
mingw-w64-ucrt-x86_64-libxml2 \
mingw-w64-ucrt-x86_64-curl \
mingw-w64-ucrt-x86_64-cmake \
mingw-w64-ucrt-x86_64-gcc \
mingw-w64-ucrt-x86_64-python3 \
mingw-w64-ucrt-x86_64-make \
mingw-w64-ucrt-x86_64-doxygen \
mingw-w64-ucrt-x86_64-jansson \
cmake 
"
pacman -S --noconfirm --needed $WINDEPS
}

get_innosetup() {
	pushd /home/
	wget https://jrsoftware.org/download.php/is.exe 
	popd
}


build_gtkdatabox () {
	pushd $WORKDIR
	wget https://downloads.sourceforge.net/project/gtkdatabox/gtkdatabox-1/gtkdatabox-1.0.0.tar.gz
	tar xvf gtkdatabox-1.0.0.tar.gz
	cd gtkdatabox-1.0.0
	./configure $AUTOCONF_OPTS
	$MAKE install
	popd
}

build_deps() {
	build_gtkdatabox
}

build_osc() {
	mkdir -p build
	pushd build
	$CMAKE $CMAKE_OPTS -G"Unix Makefiles" ../
	$MAKE
	popd
}

package_osc() {
	pushd build
	cmake --install . --prefix .
	popd

	REPODIR="$(pwd)" STAGING_DIR="/$MINGW_VERSION" ./CI/docker/install_prep.sh

	local version
	version=$(git describe --tags --always HEAD | sed 's/^v//')
	MSYS_NO_PATHCONV=1 "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" "/DMyAppVersion=${version}" osc.iss
}

CLOUDSMITH_HELPER_URL="https://raw.githubusercontent.com/analogdevicesinc/wiki-scripts/8d7a96779dd034005279ed1c7d052514df695d8c/utils/cloudsmith_utils/cloudsmith_helper.py"

get_adi_deps() {
	local artifact="Windows-MinGW-W64.zip"
	pushd "$WORKDIR"
	pacman -S --noconfirm --needed mingw-w64-ucrt-x86_64-python-requests
	wget -O cloudsmith_helper.py "$CLOUDSMITH_HELPER_URL"
	mkdir -p "$STAGING_DIR"/bin "$STAGING_DIR"/include "$STAGING_DIR"/lib/pkgconfig
	for spec in \
		external:libiio-v0~latest:libiio:iio:1 \
		external:libad9361-v0~latest:libad9361:ad9361:1 \
		external:libad9166-v0~latest:libad9166:ad9166:1; do
		IFS=: read -r repo version pcname linkname required <<<"$spec"
		rm -f "$artifact"
		python cloudsmith_helper.py \
			--method get_artifacts_from_location \
			--repo "$repo" \
			--package_version "$version" \
			--package_name "$artifact"
		# The helper exits 0 even when it matches nothing, so verify the download.
		if [ ! -f "$artifact" ]; then
			if [ "$required" = 1 ]; then
				echo "ERROR: Cloudsmith returned no '$artifact' for '$repo' version '$version'" >&2
				exit 1
			fi
			echo "WARNING: no '$artifact' in '$repo' ($version); skipping optional $pcname" >&2
			continue
		fi

		rm -rf unpack && mkdir unpack && unzip -oq "$artifact" -d unpack
		find unpack -name '*.h'                  -exec cp {} "$STAGING_DIR"/include/ \;
		find unpack -name "lib${linkname}.dll.a" -exec cp {} "$STAGING_DIR"/lib/ \;
		find unpack -name "lib${linkname}.dll"   -exec cp {} "$STAGING_DIR"/bin/ \;

		if [ -f "$STAGING_DIR/lib/lib${linkname}.dll.a" ]; then
			cat > "$STAGING_DIR/lib/pkgconfig/${pcname}.pc" <<EOF
prefix=$STAGING_DIR
exec_prefix=$STAGING_DIR
libdir=$STAGING_DIR/lib
includedir=$STAGING_DIR/include

Name: $pcname
Description: $pcname (prebuilt, from Cloudsmith)
Version: 0
Libs: -L$STAGING_DIR/lib -l$linkname
Cflags: -I$STAGING_DIR/include
EOF
		elif [ "$required" = 1 ]; then
			echo "ERROR: lib${linkname}.dll.a missing after extracting '$artifact' from '$repo'" >&2
			exit 1
		else
			echo "WARNING: lib${linkname}.dll.a not in '$artifact' ($repo); skipping optional $pcname" >&2
		fi
	done
	popd
}
build_osc_github() {
	install_pacman_deps
	get_adi_deps
	build_gtkdatabox
	build_osc
	package_osc
}


init_env
$@
