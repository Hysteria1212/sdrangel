project(nfm)

set(nfm_SOURCES
	nfmdemod.cpp
	nfmdemodgui.cpp
	nfmplugin.cpp
)

set(nfm_HEADERS
	nfmdemod.h
	nfmdemodgui.h
	nfmplugin.h
)

set(nfm_FORMS
	nfmdemodgui.ui
)

include_directories(
	.
	${CMAKE_CURRENT_BINARY_DIR}
)

#include(${QT_USE_FILE})
add_definitions(${QT_DEFINITIONS})
add_definitions(-DQT_PLUGIN)
add_definitions(-DQT_SHARED)

#qt5_wrap_cpp(nfm_HEADERS_MOC ${nfm_HEADERS})
qt5_wrap_ui(nfm_FORMS_HEADERS ${nfm_FORMS})

add_library(demodnfm SHARED
	${nfm_SOURCES}
	${nfm_HEADERS_MOC}
	${nfm_FORMS_HEADERS}
)

target_link_libraries(demodnfm
	${QT_LIBRARIES}
	sdrbase
)

qt5_use_modules(demodnfm Core Widgets)

install(TARGETS demodnfm DESTINATION lib/plugins/channelrx)