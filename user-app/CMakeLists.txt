## @file CMakeLists.txt
# @brief 定义普通用户桌面程序与离屏契约测试目标。
# @details Qt 版本与 C++ 标准继承根工程；此处只列出前端源文件和 Qt 依赖。

## @brief 用户端编译单元、自动处理的 Qt 头文件、Designer 界面和资源集合。
set(PROJECT_SOURCES
    src/main.cpp
    src/common/Format.h
    src/common/Demo.h
    src/common/Theme.h
    src/app/Session.h
    src/app/Session.cpp
    src/app/ChargePollThread.h
    src/app/ChargePollThread.cpp
    src/api/ApiClient.h
    src/api/ApiClient.cpp
    src/models/User.h
    src/models/User.cpp
    src/models/Station.h
    src/models/Station.cpp
    src/models/Charger.h
    src/models/Charger.cpp
    src/models/Order.h
    src/models/Order.cpp
    src/models/Reservation.h
    src/models/Reservation.cpp
    src/widgets/StationCard.h
    src/widgets/StationCard.cpp
    src/widgets/BackButton.h
    src/widgets/BackButton.cpp
    src/widgets/ComboBox.h
    src/widgets/ComboBox.cpp
    src/widgets/AppIcons.h
    src/widgets/AppIcons.cpp
    src/widgets/BatteryWaveWidget.h
    src/widgets/BatteryWaveWidget.cpp
    src/widgets/ChargingSpinnerWidget.h
    src/widgets/ChargingSpinnerWidget.cpp
    src/widgets/ScaleButton.h
    src/widgets/ScaleButton.cpp
    src/widgets/Spinner.h
    src/widgets/Spinner.cpp
    src/widgets/ChargeRingWidget.h
    src/widgets/ChargeRingWidget.cpp
    src/widgets/RechargeDialog.h
    src/widgets/RechargeDialog.cpp
    src/widgets/Toast.h
    src/widgets/Toast.cpp
    src/views/mainwindow.cpp
    src/views/mainwindow.h
    src/views/mainwindow.ui
    src/views/LoginView.cpp
    src/views/LoginView.h
    src/views/StationListView.cpp
    src/views/StationListView.h
    src/views/StationDetailView.cpp
    src/views/StationDetailView.h
    src/views/ChargingView.cpp
    src/views/ChargingView.h
    src/views/ChargingTab.cpp
    src/views/ChargingTab.h
    src/views/SettleView.cpp
    src/views/SettleView.h
    src/views/ProfileView.cpp
    src/views/ProfileView.h
    src/views/NavigationView.cpp
    src/views/NavigationView.h
    src/views/InfoPages.cpp
    src/views/InfoPages.h
    resources/resources.qrc
)

qt_add_executable(user-app
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_include_directories(user-app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(user-app PRIVATE
    Qt6::Network
    Qt6::Svg
    Qt6::Widgets
)

## @brief 仅在根工程发现 WebEngine 时启用嵌入地图；缺省构建仍保留路线页面提示。
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

## @brief 契约测试复用真实解析器和充电页，以本机 HTTP 替身验证显示行为。
# @details offscreen 平台避免持续集成依赖桌面显示服务；测试仍严格使用 Qt 6.2.4。
if(BUILD_TESTING)
    find_package(Qt6 6.2.4 EXACT REQUIRED COMPONENTS Test)
    add_executable(user-app-contract-tests
        tests/contract_tests.cpp
        src/api/ApiClient.cpp
        src/app/ChargePollThread.cpp
        src/models/Charger.cpp
        src/models/Order.cpp
        src/models/Reservation.cpp
        src/models/Station.cpp
        src/models/User.cpp
        src/views/ChargingView.cpp
        src/widgets/ChargingSpinnerWidget.cpp
        src/widgets/AppIcons.cpp
        src/widgets/Toast.cpp
        resources/resources.qrc
    )
    target_include_directories(user-app-contract-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(user-app-contract-tests PRIVATE Qt6::Test Qt6::Network Qt6::Widgets Qt6::Svg)
    add_test(NAME user-app-contract-tests COMMAND user-app-contract-tests)
    set_tests_properties(user-app-contract-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
