# The qmake project file used to build the Mac-specific Objective-C++ code into
# a framework to be loaded by Armory for tasks that can performed only by using
# Objective-C++ code. Parameters have been culled from multiple sources.
# Wherever appropriate, comments will be included to indicate where the
# parameters come from in case things change after software upgrades.

# Note that you MUST run SIP before executing the Makefile! SIP generates some
# CPP/H files that have to be compiled. qmake really can't handle SIP properly,
# so we must run SIP separately. (The best that qmake can do is to have
# QMAKE_EXTRA_COMPILERS build a phony file that's actually a SIP proxy. This
# generates warnings when qmake is run because the files generated by SIP don't
# exist yet. This will work but is bad form, so we won't do it.)

# When Qt 4.8.7+ is added, search this file for 4.8.7 mentions to find lines
# that need to be activated. They may be next to lines to be commented out.

# After a long back-and-forth, it was decided to basically copy what PyQt does
# whenever it uses qmake to build and install shared libraries. QMAKE_CXXFLAGS
# also has extra flags to enable C++11 support, as SIP doesn't have a
# macx-clang-libc++ option like Qt. (macx-g++ is all it can muster for now.)
# NB: The "version" values must be updated alongside build-app.py!!!
QTVER = 4.8.7
SIPVER = 4.19.13
PYVER = 2.7.17
QT_UNPACK_BASE = ../workspace/unpackandbuild/qt-everywhere-opensource-src-$${QTVER}
SIP_UNPACK_BASE = ../workspace/unpackandbuild/sip-$${SIPVER}
PYTHON_UNPACK_BASE = ../workspace/unpackandbuild/Python-$${PYVER}
TEMPLATE = lib
# 4.8.7 upgrade
#CONFIG += plugin warn_on release c++11
CONFIG += plugin warn_on release
PY_MODULE = ArmoryMac.so
target.files = ArmoryMac.so
target.CONFIG = no_check_exist
target.path = .
INSTALLS += target
sip.path = .
sip.files = ./ArmoryMac.sip
INSTALLS += sip
QMAKE_CXXFLAGS += -fno-exceptions
# Add some more flags to properly handle C++11, since SIP can't autoconfig them.
# 4.8.7 upgrade
#QMAKE_CXXFLAGS += -std=c++11 -stdlib=libc++
DEFINES += SIP_PROTECTED_IS_PUBLIC protected=public
# 4.8.7 upgrade
#INCLUDEPATH = $$QT_UNPACK_BASE/mkspecs/unsupported/macx-clang-libc++ . $$QT_UNPACK_BASE/include/QtCore $$QT_UNPACK_BASE/include $$QT_UNPACK_BASE/include/QtGui .rcc/release-shared .moc $$SIP_UNPACK_BASE/siplib $$PYTHON_UNPACK_BASE/Include $$PYTHON_UNPACK_BASE/Mac/Include $$PYTHON_UNPACK_BASE
INCLUDEPATH = $$QT_UNPACK_BASE/mkspecs/unsupported/macx-clang . $$QT_UNPACK_BASE/include/QtCore $$QT_UNPACK_BASE/include $$QT_UNPACK_BASE/include/QtGui .rcc/release-shared .moc $$SIP_UNPACK_BASE/siplib $$PYTHON_UNPACK_BASE/Include $$PYTHON_UNPACK_BASE/Mac/Include $$PYTHON_UNPACK_BASE
QMAKE_LFLAGS += -lc++
QMAKE_LFLAGS += "-undefined dynamic_lookup"

# Handle the Objective-C++ files. This will include executing moc on the
# ArmoryMac.h file, creating a resultant CPP file, and compiling the CPP file.
# (Such a step is critical for getting the shared library to work properly.)
# Because we support 10.8+, it's safe to assume we can use SSSE3 instructions.
# NB: -std=c++11 -stdlib=libc++ will be required for Qt 4.8.7+.
# Source: http://stackoverflow.com/questions/2355056/how-to-mix-qt-c-and-obj-c-cocoa
# Source: http://el-tramo.be/blog/mixing-cocoa-and-qt
# Source: http://stackoverflow.com/questions/18768219/sdl-framework-include-on-macos-qt
QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.9
QMAKE_OBJECTIVE_CFLAGS += -std=c++11 -stdlib=libc++ -Xarch_x86_64 -mmacosx-version-min=$${QMAKE_MACOSX_DEPLOYMENT_TARGET} -O2 -arch x86_64 -mssse3 -Wall -W -fPIC
OBJECTIVE_SOURCES += macdockiconhandler.mm macnotificationhandler.mm macutils.mm
HEADERS += ./ArmoryMac.h
LIBS += -framework Foundation

# General flags and files, some of which were borrowed from the Qt Makefiles.
# This also includes moving the resultant library (.dylib) to DESTDIR and
# and renaming it (.so) via QMAKE_POST_LINK.
# Source: http://qt-project.org/doc/qt-4.8/qmake-variable-reference.html
# Source: http://qtcenter.org/wiki/index.php?title=Undocumented_qmake
# Source: src/gui/Makefile.release for Qt
# Source: QtCore/QtCore.pro for PyQt
ARMORYNAME = ArmoryMac
DESTDIR = ../workspace/Armory.app/Contents/MacOS/py/usr/local/lib/armory
SOURCES = ./sip$${ARMORYNAME}DockIconClickEventHandler.cpp ./sip$${ARMORYNAME}MacDockIconHandler.cpp ./sip$${ARMORYNAME}MacNotificationHandler.cpp ./sip$${ARMORYNAME}MacUtils.cpp ./sip$${ARMORYNAME}cmodule.cpp
LIBS += -L$$QT_UNPACK_BASE/lib -framework QtCore -framework QtGui -L$$QT_UNPACK_BASE/lib -framework Carbon -lz -framework AppKit -framework Python
QMAKE_CXXFLAGS += $$QMAKE_OBJECTIVE_CFLAGS
DEFINES += QT_SHARED QT_BUILD_GUI_LIB QT_NO_USING_NAMESPACE QT_NO_CAST_TO_ASCII QT_ASCII_CAST_WARNINGS QT_MOC_COMPAT QT_USE_QSTRINGBUILDER QT_USE_BUNDLED_LIBPNG PNG_NO_ASSEMBLER_CODE QT_NO_CUPS QT_NO_LPR QT_NO_OPENTYPE QT_NO_STYLE_WINDOWSVISTA QT_NO_STYLE_WINDOWSXP QT_NO_STYLE_GTK QT_NO_STYLE_WINDOWSCE QT_NO_STYLE_WINDOWSMOBILE QT_NO_STYLE_S60 Q_INTERNAL_QAPP_SRC QT_NO_DEBUG QT_CORE_LIB QT_HAVE_MMX QT_HAVE_SSE QT_HAVE_MMXEXT QT_HAVE_SSE2 QT_HAVE_SSE3 QT_HAVE_SSSE3 _LARGEFILE64_SOURCE _LARGEFILE_SOURCE
QMAKE_CLEAN += ./sip* ./moc_* $$DESTDIR/$${ARMORYNAME}.so
QMAKE_POST_LINK = mv $$DESTDIR/$(TARGET) $$DESTDIR/$${ARMORYNAME}.so
