#--------------------------------------------------------
#
# Pro file for Android and Windows builds with Qt Creator
#
#--------------------------------------------------------

QT += core gui multimedia opengl

TEMPLATE = lib
TARGET = devices

DEFINES += USE_SSE2=1
QMAKE_CXXFLAGS += -msse2
DEFINES += USE_SSE4_1=1
QMAKE_CXXFLAGS += -msse4.1

CONFIG(MINGW32):LIBBLADERFSRC = "D:\softs\bladeRF\host\libraries\libbladeRF\include"
CONFIG(MINGW64):LIBBLADERFSRC = "D:\softs\bladeRF\host\libraries\libbladeRF\include"
CONFIG(MINGW32):LIBHACKRFSRC = "D:\softs\hackrf\host"
CONFIG(MINGW64):LIBHACKRFSRC = "D:\softs\hackrf\host"
CONFIG(MINGW32):LIBLIMESUITESRC = "D:\softs\LimeSuite"
CONFIG(MINGW64):LIBLIMESUITESRC = "D:\softs\LimeSuite"

INCLUDEPATH += $$PWD
INCLUDEPATH += ../sdrbase
INCLUDEPATH += $$LIBBLADERFSRC
INCLUDEPATH += $$LIBHACKRFSRC
INCLUDEPATH += ../liblimesuite/srcmw
INCLUDEPATH += $$LIBLIMESUITESRC/src
INCLUDEPATH += $$LIBLIMESUITESRC/src/ADF4002
INCLUDEPATH += $$LIBLIMESUITESRC/src/ConnectionRegistry
INCLUDEPATH += $$LIBLIMESUITESRC/src/FPGA_common
INCLUDEPATH += $$LIBLIMESUITESRC/src/GFIR
INCLUDEPATH += $$LIBLIMESUITESRC/src/lms7002m
INCLUDEPATH += $$LIBLIMESUITESRC/src/lms7002m_mcu
INCLUDEPATH += $$LIBLIMESUITESRC/src/Si5351C
INCLUDEPATH += $$LIBLIMESUITESRC/src/protocols
INCLUDEPATH += $$LIBLIMESUITESRC/external/cpp-feather-ini-parser

CONFIG(Release):build_subdir = release
CONFIG(Debug):build_subdir = debug

SOURCES += bladerf/devicebladerf.cpp\
        bladerf/devicebladerfvalues.cpp\
        hackrf/devicehackrf.cpp\
        hackrf/devicehackrfvalues.cpp\
        limesdr/devicelimesdr.cpp\
        limesdr/devicelimesdrparam.cpp\
        limesdr/devicelimesdrshared.cpp

HEADERS  += bladerf/devicebladerf.h\
        bladerf/devicebladerfparam.h\
        bladerf/devicebladerfvalues.h\
        hackrf/devicehackrf.h\
        hackrf/devicehackrfparam.h\
        hackrf/devicehackrfvalues.h\
        limesdr/devicelimesdr.h\
        limesdr/devicelimesdrparam.h\
        limesdr/devicelimesdrshared.h

LIBS += -L../sdrbase/$${build_subdir} -lsdrbase
LIBS += -L../libbladerf/$${build_subdir} -llibbladerf
LIBS += -L../libhackrf/$${build_subdir} -llibhackrf
LIBS += -L../liblimesuite/$${build_subdir} -lliblimesuite
