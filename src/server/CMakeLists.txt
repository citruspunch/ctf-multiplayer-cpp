# Server library — authoritative game server
add_library(server INTERFACE)
target_include_directories(server INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(server INTERFACE common)
