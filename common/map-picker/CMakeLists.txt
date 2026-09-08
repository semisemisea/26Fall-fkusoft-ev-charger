add_library(evcharger-map-picker STATIC
    map_picker_dialog.cpp
    include/evcharger/map_picker_dialog.h
)
target_include_directories(evcharger-map-picker PUBLIC include)
target_link_libraries(evcharger-map-picker PUBLIC Qt6::Widgets)
if(TARGET Qt6::WebEngineWidgets)
    target_link_libraries(evcharger-map-picker PRIVATE Qt6::WebEngineWidgets)
    target_compile_definitions(evcharger-map-picker PRIVATE EVCHARGER_HAS_WEBENGINE)
endif()
