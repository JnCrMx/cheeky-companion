project(basic_client)

file(GLOB_RECURSE sources src/**.cpp)

add_executable(basic_client ${sources})
target_include_directories(basic_client PUBLIC include/)

find_package(SDL2)
target_link_libraries(basic_client PUBLIC SDL2::SDL2)

find_package(Boost 1.40 COMPONENTS program_options REQUIRED)
target_include_directories(basic_client PUBLIC ${Boost_INCLUDE_DIR})
target_link_libraries(basic_client PUBLIC ${Boost_PROGRAM_OPTIONS_LIBRARY})

find_package(Threads REQUIRED)
target_link_libraries(basic_client PRIVATE Threads::Threads)
