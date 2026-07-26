# Common library — shared protocol, networking, domain logic
add_library(common STATIC
    json.cpp
)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(common PUBLIC nlohmann_json::nlohmann_json)
