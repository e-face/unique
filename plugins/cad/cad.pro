QT = core
TEMPLATE = lib
CONFIG += plugin c++11
CONFIG -= app_bundle
TARGET = cad

CODECFORSRC = UTF-8
win32-g++: QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
win32: QMAKE_LFLAGS += -Wl,--export-all-symbols

DESTDIR = $$PWD/../../bin/plugins
OBJECTS_DIR = $$PWD/../../build/cad/obj
MOC_DIR = $$PWD/../../build/cad/moc
RCC_DIR = $$PWD/../../build/cad/rcc

INCLUDEPATH += $$PWD/../../src

HEADERS += \
    cadmesh.h \
    caddraw.h

SOURCES += \
    cadmesh.cpp \
    caddraw.cpp \
    cadplugin.cpp
