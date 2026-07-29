# Common library — shared protocol, networking, domain logic
add_library(common STATIC
    json.cpp
    framing.cpp
    messages.cpp
    gui_helpers.cpp
    net/platform.cpp
    net/socket.cpp
    net/tcp_socket.cpp
    net/udp_socket.cpp
    net/poller.cpp
    udp_discovery.cpp
    app_activation.mm
)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(common PUBLIC nlohmann_json::nlohmann_json)

if(APPLE OR UNIX)
    target_link_libraries(common PRIVATE pthread)
endif()

# TransformProcessType / GetCurrentProcess (used by app_activation.hpp on
# macOS to promote a terminal-launched binary to a foreground GUI app)
# live in ApplicationServices.framework, not in the Cocoa umbrella.
if(APPLE)
    target_link_libraries(common PUBLIC "-framework ApplicationServices")
endif()
