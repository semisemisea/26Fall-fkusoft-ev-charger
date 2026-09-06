add_library(ev_charger_core STATIC
	src/charging.cpp
	src/clock.cpp
	src/geo.cpp
	src/validation.cpp
)

target_include_directories(ev_charger_core PUBLIC include)
target_link_libraries(ev_charger_core PUBLIC Qt6::Core)

if(BUILD_TESTING)
	find_package(Qt6 6.2.4 EXACT REQUIRED COMPONENTS Test)
	add_executable(biz_core_tests tests/core_tests.cpp)
	target_link_libraries(biz_core_tests PRIVATE ev_charger_core Qt6::Test)
	add_test(NAME biz_core_tests COMMAND biz_core_tests)
endif()
