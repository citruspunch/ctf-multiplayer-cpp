add_executable(ctf_tests
    main_test.cpp
    test_discovery.cpp
    test_framing.cpp
    test_messages.cpp
    test_domain.cpp
    test_integration.cpp
    server_view_stub.cpp
)
target_link_libraries(ctf_tests PRIVATE
    Catch2::Catch2WithMain
    common
    server
    client
)
add_test(NAME ctf_tests COMMAND ctf_tests)
