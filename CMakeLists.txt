cmake_minimum_required(VERSION 3.20)
project(ctf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies via vcpkg manifest mode
find_package(nlohmann_json CONFIG REQUIRED)
find_package(raylib CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)

# Main executable
add_executable(ctf
    src/main.cpp
)

# Subdirectories
add_subdirectory(src/common)
add_subdirectory(src/server)
add_subdirectory(src/client)

# Link dependencies
target_link_libraries(ctf PRIVATE
    nlohmann_json::nlohmann_json
    raylib
    common
    server
    client
)

# Tests
enable_testing()
add_subdirectory(tests)
