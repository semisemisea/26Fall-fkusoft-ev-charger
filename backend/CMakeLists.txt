add_library(backend_runtime STATIC
	src/api.cpp
	src/api_support.cpp
	src/auth_api.cpp
	src/config.cpp
	src/database.cpp
	src/http.cpp
	src/security.cpp
	src/user_api.cpp
)

target_include_directories(backend_runtime PUBLIC include)
target_link_libraries(backend_runtime PUBLIC ev_charger_core Qt6::Core Qt6::Network Qt6::Sql)

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

	add_executable(backend_http_tests tests/http_tests.cpp)
	target_link_libraries(backend_http_tests PRIVATE backend_runtime Qt6::Test)
	add_test(NAME backend_http_tests COMMAND backend_http_tests)

	add_executable(backend_auth_tests tests/auth_tests.cpp)
	target_link_libraries(backend_auth_tests PRIVATE backend_runtime Qt6::Test)
	add_test(NAME backend_auth_tests COMMAND backend_auth_tests)

	add_executable(backend_user_tests tests/user_tests.cpp)
	target_link_libraries(backend_user_tests PRIVATE backend_runtime Qt6::Test)
	add_test(NAME backend_user_tests COMMAND backend_user_tests)
endif()
