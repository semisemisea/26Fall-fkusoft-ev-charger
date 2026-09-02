cmake_minimum_required(VERSION 3.16)

project(user-app VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
include(GNUInstallDirs)

qt_add_executable(user-app
    src/main.cpp
    src/common/Format.h
    src/app/Session.h
    src/app/Session.cpp
    src/api/ApiClient.h
    src/api/ApiClient.cpp
    src/models/User.h
    src/models/User.cpp
    src/models/Station.h
    src/models/Station.cpp
    src/models/Charger.h
    src/models/Charger.cpp
    src/widgets/StationCard.h
    src/widgets/StationCard.cpp
    src/views/mainwindow.cpp
    src/views/mainwindow.h
    src/views/mainwindow.ui
    src/views/LoginView.cpp
    src/views/LoginView.h
    src/views/StationListView.cpp
    src/views/StationListView.h
    src/views/StationDetailView.cpp
    src/views/StationDetailView.h
)

target_include_directories(user-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(user-app PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
)

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
