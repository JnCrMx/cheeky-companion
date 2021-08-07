cmake_minimum_required (VERSION 3.4)

project(cheeky_companion)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE sources src/**.cpp)

add_library(cheeky_companion SHARED ${sources})
target_include_directories(cheeky_companion PRIVATE include/)

add_subdirectory(external/cheeky-imp/vulkan_layer)
target_link_libraries(cheeky_companion PRIVATE cheeky_layer)

set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(external/json)
target_link_libraries(cheeky_companion PUBLIC nlohmann_json::nlohmann_json)
