#--------------------------------------------------------
#
# Pro file for Windows builds with Qt Creator
#
#--------------------------------------------------------

TEMPLATE = lib
CONFIG += plugin

QT += core gui widgets multimedia opengl network

TARGET = udpsink

DEFINES += USE_SSE2=1
QMAKE_CXXFLAGS += -msse2
DEFINES += USE_SSE4_1=1
QMAKE_CXXFLAGS += -msse4.1

INCLUDEPATH += $$PWD
INCLUDEPATH += ../../../sdrbase

CONFIG(Release):build_subdir = release
CONFIG(Debug):build_subdir = debug

SOURCES += udpsink.cpp\
    udpsinkgui.cpp\
    udpsinkplugin.cpp\
    udpsinkmsg.cpp\
    udpsinkudphandler.cpp

HEADERS += udpsink.h\
    udpsinkgui.h\
    udpsinkplugin.h\
    udpsinkmsg.h\
    udpsinkudphandler.h

FORMS += udpsinkgui.ui

LIBS += -L../../../sdrbase/$${build_subdir} -lsdrbase

RESOURCES = ../../../sdrbase/resources/res.qrc
