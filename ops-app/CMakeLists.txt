cmake_minimum_required(VERSION 3.16)

project(ops-app VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 项目统一使用 Qt 6.2.4,显式锁死版本避免误用更高版本的 API
find_package(Qt6 6.2.4 EXACT REQUIRED COMPONENTS Widgets Network Charts)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
include(GNUInstallDirs)

qt_add_executable(ops-app
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
    pages/chargermanagepage.cpp
    pages/chargermanagepage.h
)

target_link_libraries(ops-app PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Charts
)

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
