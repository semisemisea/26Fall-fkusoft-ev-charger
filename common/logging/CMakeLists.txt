add_library(evcharger-logging STATIC logging.cpp)
target_include_directories(evcharger-logging PUBLIC include)
target_link_libraries(evcharger-logging PUBLIC Qt6::Core)
# Preserve function/file/line metadata in release builds as well.
target_compile_definitions(evcharger-logging PUBLIC QT_MESSAGELOGCONTEXT)

if(BUILD_TESTING)
    find_package(Qt6 6.2.4 EXACT REQUIRED COMPONENTS Test)
    add_executable(evcharger-logging-tests tests/logging_tests.cpp)
    target_link_libraries(evcharger-logging-tests PRIVATE evcharger-logging Qt6::Test)
    add_test(NAME evcharger-logging-tests COMMAND evcharger-logging-tests)
endif()
