add_library(biz-core-domain STATIC
	src/charging.cpp
	src/clock.cpp
	src/geo.cpp
	src/validation.cpp
)

target_include_directories(biz-core-domain PUBLIC include)
target_link_libraries(biz-core-domain PUBLIC Qt6::Core)

add_library(biz-core-runtime STATIC
	src/admin_api.cpp
	src/admin_user_api.cpp
	src/api.cpp
	src/api_support.cpp
	src/auth_api.cpp
	src/charger_api.cpp
	src/config.cpp
	src/database.cpp
	src/http.cpp
	src/location_api.cpp
	src/map_client.cpp
	src/order_api.cpp
	src/order_support.cpp
	src/reservation_api.cpp
	src/resource_support.cpp
	src/security.cpp
	src/station_api.cpp
	src/user_api.cpp
)

target_include_directories(biz-core-runtime PUBLIC include)
target_link_libraries(biz-core-runtime PUBLIC biz-core-domain Qt6::Core Qt6::Network Qt6::Sql)

add_executable(biz-core src/main.cpp)
target_link_libraries(biz-core PRIVATE biz-core-runtime)

install(TARGETS biz-core
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BUILD_TESTING)
	find_package(Qt6 6.2.4 EXACT REQUIRED COMPONENTS Test)
	add_executable(biz-core-tests tests/core_tests.cpp)
	target_link_libraries(biz-core-tests PRIVATE biz-core-domain Qt6::Test)
	add_test(NAME biz-core-tests COMMAND biz-core-tests)

	add_executable(biz-core-config-tests tests/config_tests.cpp)
	target_link_libraries(biz-core-config-tests PRIVATE biz-core-runtime Qt6::Test)
	add_test(NAME biz-core-config-tests COMMAND biz-core-config-tests)

	add_executable(biz-core-database-tests tests/database_tests.cpp)
	target_link_libraries(biz-core-database-tests PRIVATE biz-core-runtime Qt6::Test)
	add_test(NAME biz-core-database-tests COMMAND biz-core-database-tests)

	add_executable(biz-core-http-tests tests/http_tests.cpp)
	target_link_libraries(biz-core-http-tests PRIVATE biz-core-runtime Qt6::Test)
	add_test(NAME biz-core-http-tests COMMAND biz-core-http-tests)

	add_executable(biz-core-auth-tests tests/auth_tests.cpp)
	target_link_libraries(biz-core-auth-tests PRIVATE biz-core-runtime Qt6::Test)
	add_test(NAME biz-core-auth-tests COMMAND biz-core-auth-tests)

	add_executable(biz-core-user-tests tests/user_tests.cpp)
	target_link_libraries(biz-core-user-tests PRIVATE biz-core-runtime Qt6::Test)
	add_test(NAME biz-core-user-tests COMMAND biz-core-user-tests)

	add_executable(biz-core-station-tests tests/station_tests.cpp)
	target_link_libraries(biz-core-station-tests PRIVATE biz-core-runtime Qt6::Test)
	add_test(NAME biz-core-station-tests COMMAND biz-core-station-tests)

	add_executable(biz-core-charger-tests tests/charger_tests.cpp)
	target_link_libraries(biz-core-charger-tests PRIVATE biz-core-runtime Qt6::Test)
	add_test(NAME biz-core-charger-tests COMMAND biz-core-charger-tests)

	add_executable(biz-core-reservation-tests tests/reservation_tests.cpp)
	target_link_libraries(biz-core-reservation-tests PRIVATE biz-core-runtime Qt6::Test)
	add_test(NAME biz-core-reservation-tests COMMAND biz-core-reservation-tests)

	add_executable(biz-core-order-tests tests/order_tests.cpp)
	target_link_libraries(biz-core-order-tests PRIVATE biz-core-runtime Qt6::Test)
	add_test(NAME biz-core-order-tests COMMAND biz-core-order-tests)

	add_executable(biz-core-admin-tests tests/admin_tests.cpp)
	target_link_libraries(biz-core-admin-tests PRIVATE biz-core-runtime Qt6::Test)
	add_test(NAME biz-core-admin-tests COMMAND biz-core-admin-tests)

	add_executable(biz-core-map-tests tests/map_tests.cpp)
	target_link_libraries(biz-core-map-tests PRIVATE biz-core-runtime Qt6::Test)
	add_test(NAME biz-core-map-tests COMMAND biz-core-map-tests)
endif()
