qt_ver=5.7.1
qt_dir=qt-everywhere-opensource-src-${qt_ver}
qt_tar=${qt_dir}.tar.gz
qt_url=http://download.qt.io/official_releases/qt/5.7/${qt_ver}/single/${qt_tar}

qt_pfx=$PWD/qt-${qt_ver}-armhf

test -f ${qt_tar} || curl -L ${qt_url} -o ${qt_tar}

tar -zxf ${qt_tar}

cp -a patches/linux-arm-gnueabihf-g++ ${qt_dir}/qtbase/mkspecs/

cd ${qt_dir}

./configure -release -static -largefile -opensource -confirm-license -prefix ${qt_pfx} -xplatform linux-arm-gnueabihf-g++ -qt-pcre -qt-zlib -no-audio-backend -no-cups -no-nis -no-dbus -no-xcb -no-linuxfb -no-fontconfig -no-evdev -no-iconv -no-gif -no-libpng -no-libjpeg -no-compile-examples -no-gui -no-widgets -no-opengl -no-icu -no-eglfs -no-sql-sqlite -no-sql-sqlite2 -no-freetype -no-harfbuzz -no-openssl -no-xkbcommon -no-pulseaudio -no-alsa -no-gtkstyle -no-qml-debug -skip qtquick1 -skip qtdeclarative -nomake examples -nomake tests

make module-qtwebsockets
make module-qtwebsockets-install_subtargets
