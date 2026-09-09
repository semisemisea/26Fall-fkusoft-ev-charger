cmake_minimum_required(VERSION 3.16)

project(fkusoft-ev-charger VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.2.4 EXACT REQUIRED COMPONENTS
    Core
    Network
    Sql
    Svg
    Widgets
    OPTIONAL_COMPONENTS Charts WebEngineWidgets
)

include(GNUInstallDirs)
include(CTest)

add_subdirectory(common/logging)
# All applications and their test targets share the same logging support.
link_libraries(evcharger-logging)

add_subdirectory(common/refresh)
add_subdirectory(common/map-picker)

add_subdirectory(biz-core)
add_subdirectory(user-app)
add_subdirectory(ops-app)
