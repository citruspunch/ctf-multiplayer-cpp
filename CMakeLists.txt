cmake_minimum_required(VERSION 3.20)
project(ctf LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies via vcpkg manifest mode
find_package(nlohmann_json CONFIG REQUIRED)
find_package(raylib CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)

# Main executable
# Raylib UI sources are compiled directly into the executable (not the
# static libraries) so Raylib/GLFW symbols resolve at link time on macOS.
add_executable(ctf MACOSX_BUNDLE
    src/main.cpp
    src/server/server_view.cpp
    src/client/client.cpp
    src/client/input.cpp
    src/client/game_view.cpp
)

# Configure Info.plist file natively if needed
if(APPLE)
    set_target_properties(ctf PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/src/Info.plist"
    )
endif()


# Subdirectories
add_subdirectory(src/common)
add_subdirectory(src/server)
add_subdirectory(src/client)

# Link dependencies
target_link_libraries(ctf PRIVATE
    nlohmann_json::nlohmann_json
    raylib
    glfw
    common
    server
    client
)

# Tests
enable_testing()
add_subdirectory(tests)
