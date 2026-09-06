add_library(backend_runtime STATIC
	src/config.cpp
	src/database.cpp
	src/security.cpp
)

target_include_directories(backend_runtime PUBLIC include)
target_link_libraries(backend_runtime PUBLIC Qt6::Core Qt6::Network Qt6::Sql)

add_executable(ev-charger-backend src/main.cpp)
target_link_libraries(ev-charger-backend PRIVATE backend_runtime)

install(TARGETS ev-charger-backend
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BUILD_TESTING)
	find_package(Qt6 6.2.4 EXACT REQUIRED COMPONENTS Test)
	add_executable(backend_config_tests tests/config_tests.cpp)
	target_link_libraries(backend_config_tests PRIVATE backend_runtime Qt6::Test)
	add_test(NAME backend_config_tests COMMAND backend_config_tests)

	add_executable(backend_database_tests tests/database_tests.cpp)
	target_link_libraries(backend_database_tests PRIVATE backend_runtime Qt6::Test)
	add_test(NAME backend_database_tests COMMAND backend_database_tests)
endif()
