set(PROJECT_SOURCES
    src/main.cpp
    src/common/format.h
    src/common/demo.h
    src/common/theme.h
    src/app/session.h
    src/app/session.cpp
    src/app/charge_poll_thread.h
    src/app/charge_poll_thread.cpp
    src/api/api_client.h
    src/api/api_client.cpp
    src/models/route_playback.h
    src/models/route_playback.cpp
    src/models/user.h
    src/models/user.cpp
    src/models/station.h
    src/models/station.cpp
    src/models/charger.h
    src/models/charger.cpp
    src/models/order.h
    src/models/order.cpp
    src/models/reservation.h
    src/models/reservation.cpp
    src/widgets/station_card.h
    src/widgets/station_card.cpp
    src/widgets/back_button.h
    src/widgets/back_button.cpp
    src/widgets/combo_box.h
    src/widgets/combo_box.cpp
    src/widgets/app_icons.h
    src/widgets/app_icons.cpp
    src/widgets/battery_wave_widget.h
    src/widgets/battery_wave_widget.cpp
    src/widgets/charging_spinner_widget.h
    src/widgets/charging_spinner_widget.cpp
    src/widgets/scale_button.h
    src/widgets/scale_button.cpp
    src/widgets/spinner.h
    src/widgets/spinner.cpp
    src/widgets/charge_ring_widget.h
    src/widgets/charge_ring_widget.cpp
    src/widgets/recharge_dialog.h
    src/widgets/recharge_dialog.cpp
    src/widgets/toast.h
    src/widgets/toast.cpp
    src/views/main_window.cpp
    src/views/main_window.h
    src/views/main_window.ui
    src/views/login_view.cpp
    src/views/login_view.h
    src/views/station_list_view.cpp
    src/views/station_list_view.h
    src/views/station_detail_view.cpp
    src/views/station_detail_view.h
    src/views/charging_view.cpp
    src/views/charging_view.h
    src/views/charging_tab.cpp
    src/views/charging_tab.h
    src/views/settle_view.cpp
    src/views/settle_view.h
    src/views/profile_view.cpp
    src/views/profile_view.h
    src/views/navigation_view.cpp
    src/views/navigation_view.h
    src/views/info_pages.cpp
    src/views/info_pages.h
    resources/resources.qrc
)

qt_add_executable(user-app
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_include_directories(user-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(user-app PRIVATE
    evcharger-map-picker
    Qt6::Network
    Qt6::Svg
    Qt6::Widgets
)

if(TARGET Qt6::WebEngineWidgets)
    target_link_libraries(user-app PRIVATE Qt6::WebEngineWidgets)
    target_compile_definitions(user-app PRIVATE USER_APP_HAS_WEBENGINE)
endif()

set_target_properties(user-app PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

install(TARGETS user-app
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(user-app)

if(BUILD_TESTING)
    find_package(Qt6 6.2.4 EXACT REQUIRED COMPONENTS Test)
    add_executable(user-app-contract-tests
        tests/contract_tests.cpp
        src/api/api_client.cpp
        src/app/charge_poll_thread.cpp
        src/models/charger.cpp
        src/models/order.cpp
        src/models/reservation.cpp
        src/models/station.cpp
        src/models/user.cpp
        src/views/charging_view.cpp
        src/widgets/charging_spinner_widget.cpp
        src/widgets/app_icons.cpp
        src/widgets/toast.cpp
        resources/resources.qrc
    )
    target_include_directories(user-app-contract-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(user-app-contract-tests PRIVATE Qt6::Test Qt6::Network Qt6::Widgets Qt6::Svg)
    add_test(NAME user-app-contract-tests COMMAND user-app-contract-tests)
    set_tests_properties(user-app-contract-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(BUILD_TESTING)
    add_executable(user-app-station-search-tests
        tests/station_search_tests.cpp
        src/api/api_client.cpp
        src/app/session.cpp
        src/models/user.cpp
        src/models/station.cpp
        src/views/station_list_view.cpp
        src/widgets/station_card.cpp
        src/widgets/spinner.cpp
        src/widgets/app_icons.cpp
    )
    target_include_directories(user-app-station-search-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(user-app-station-search-tests PRIVATE evcharger-map-picker Qt6::Test Qt6::Network Qt6::Widgets Qt6::Svg)
    if(TARGET Qt6::WebEngineWidgets)
        target_link_libraries(user-app-station-search-tests PRIVATE Qt6::WebEngineWidgets)
        target_compile_definitions(user-app-station-search-tests PRIVATE USER_APP_HAS_WEBENGINE)
    endif()
    add_test(NAME user-app-station-search-tests COMMAND user-app-station-search-tests)
    set_tests_properties(user-app-station-search-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(BUILD_TESTING)
    add_executable(user-app-profile-tests
        tests/profile_tests.cpp
        src/api/api_client.cpp
        src/app/session.cpp
        src/models/user.cpp
        src/views/profile_view.cpp
        src/widgets/recharge_dialog.cpp
        src/widgets/scale_button.cpp
        src/widgets/app_icons.cpp
        src/widgets/toast.cpp
        resources/resources.qrc
    )
    target_include_directories(user-app-profile-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(user-app-profile-tests PRIVATE Qt6::Test Qt6::Network Qt6::Widgets Qt6::Svg)
    add_test(NAME user-app-profile-tests COMMAND user-app-profile-tests)
    set_tests_properties(user-app-profile-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(BUILD_TESTING)
    add_executable(user-app-route-playback-tests tests/route_playback_tests.cpp src/models/route_playback.cpp)
    target_include_directories(user-app-route-playback-tests PRIVATE src)
    target_link_libraries(user-app-route-playback-tests PRIVATE Qt6::Test)
    add_test(NAME user-app-route-playback-tests COMMAND user-app-route-playback-tests)

    add_executable(user-app-navigation-tests
        tests/navigation_tests.cpp src/views/navigation_view.cpp src/models/route_playback.cpp
        src/api/api_client.cpp src/app/session.cpp src/models/user.cpp src/widgets/back_button.cpp
        resources/resources.qrc)
    target_include_directories(user-app-navigation-tests PRIVATE src)
    target_link_libraries(user-app-navigation-tests PRIVATE Qt6::Test Qt6::Widgets Qt6::Network)
    if(TARGET Qt6::WebEngineWidgets)
        target_link_libraries(user-app-navigation-tests PRIVATE Qt6::WebEngineWidgets)
        target_compile_definitions(user-app-navigation-tests PRIVATE USER_APP_HAS_WEBENGINE)
    endif()
    add_test(NAME user-app-navigation-tests COMMAND user-app-navigation-tests)
endif()
