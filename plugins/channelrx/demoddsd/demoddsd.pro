#--------------------------------------------------------
#
# Pro file for Windows builds with Qt Creator
#
#--------------------------------------------------------

TEMPLATE = lib
CONFIG += plugin

QT += core gui widgets multimedia opengl

TARGET = demoddsd

CONFIG(MINGW32):LIBDSDCCSRC = "D:\softs\dsdcc"
CONFIG(MINGW64):LIBDSDCCSRC = "D:\softs\dsdcc"

CONFIG(MINGW32):LIBMBELIBSRC = "D:\softs\mbelib"
CONFIG(MINGW64):LIBMBELIBSRC = "D:\softs\mbelib"

CONFIG(MINGW32):INCLUDEPATH += "D:\boost_1_58_0"
CONFIG(MINGW64):INCLUDEPATH += "D:\boost_1_58_0"

INCLUDEPATH += $$PWD
INCLUDEPATH += ../../../sdrbase
INCLUDEPATH += $$LIBDSDCCSRC
INCLUDEPATH += $$LIBMBELIBSRC

CONFIG(Release):build_subdir = release
CONFIG(Debug):build_subdir = debug

SOURCES = dsddecoder.cpp\
dsddemod.cpp\
dsddemodgui.cpp\
dsddemodplugin.cpp

HEADERS = dsddecoder.h\
dsddemod.h\
dsddemodgui.h\
dsddemodplugin.h

FORMS = dsddemodgui.ui

LIBS += -L../../../sdrbase/$${build_subdir} -lsdrbase
LIBS += -L../../../dsdcc/$${build_subdir} -ldsdcc

RESOURCES = ../../../sdrbase/resources/res.qrc