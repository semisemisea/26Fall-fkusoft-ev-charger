add_executable(biz-core
    main.cpp
)

target_link_libraries(biz-core PRIVATE Qt6::Core)

install(TARGETS biz-core
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
