# Client library — game client with Raylib UI
add_library(client INTERFACE)
target_include_directories(client INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client INTERFACE common raylib)
