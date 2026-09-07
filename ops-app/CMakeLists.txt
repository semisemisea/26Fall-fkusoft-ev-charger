set(PROJECT_SOURCES
    main.cpp
    api/apiclient.cpp
    api/apiclient.h
    api/types.h
    mainwindow.cpp
    mainwindow.h
    logindialog.cpp
    logindialog.h
    logindialog.ui
    pages/salespage.cpp
    pages/salespage.h
    pages/chargerstatuspage.cpp
    pages/chargerstatuspage.h
    pages/chargerdialog.cpp
    pages/chargerdialog.h
    pages/chargermanagepage.cpp
    pages/chargermanagepage.h
    pages/stationpage.cpp
    pages/stationpage.h
    pages/userpage.cpp
    pages/userpage.h
)

qt_add_executable(ops-app
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_link_libraries(ops-app PRIVATE
    Qt6::Network
    Qt6::Widgets
)

if(TARGET Qt6::Charts)
    target_link_libraries(ops-app PRIVATE Qt6::Charts)
    target_compile_definitions(ops-app PRIVATE OPS_APP_HAS_CHARTS)
endif()

set_target_properties(ops-app PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

# 源码根目录作为 include 基准,子目录头文件以 "api/xxx.h" 形式引用
target_include_directories(ops-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS ops-app
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(ops-app)

if(BUILD_TESTING)
    find_package(Qt6 6.2.4 EXACT REQUIRED COMPONENTS Test)
    qt_add_executable(ops-app-charger-state-tests
        tests/charger_state_tests.cpp
        api/types.h
    )
    target_include_directories(ops-app-charger-state-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ops-app-charger-state-tests PRIVATE Qt6::Test)
    add_test(NAME ops-app-charger-state-tests COMMAND ops-app-charger-state-tests)

    qt_add_executable(ops-app-charger-api-tests
        tests/charger_api_tests.cpp
        api/apiclient.cpp
        api/apiclient.h
        api/types.h
    )
    target_include_directories(ops-app-charger-api-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ops-app-charger-api-tests PRIVATE Qt6::Network Qt6::Test)
    add_test(NAME ops-app-charger-api-tests COMMAND ops-app-charger-api-tests)

    qt_add_executable(ops-app-station-api-tests
        tests/station_api_tests.cpp
        api/apiclient.cpp
        api/apiclient.h
        api/types.h
    )
    target_include_directories(ops-app-station-api-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(ops-app-station-api-tests PRIVATE Qt6::Network Qt6::Test)
    add_test(NAME ops-app-station-api-tests COMMAND ops-app-station-api-tests)
endif()
