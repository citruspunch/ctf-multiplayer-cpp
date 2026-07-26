# Common library — shared protocol, networking, domain logic
add_library(common STATIC
    json.cpp
    framing.cpp
    messages.cpp
    net/platform.cpp
    net/socket.cpp
    net/tcp_socket.cpp
    net/udp_socket.cpp
    net/poller.cpp
)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(common PUBLIC nlohmann_json::nlohmann_json)
