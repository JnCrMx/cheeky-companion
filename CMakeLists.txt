cmake_minimum_required (VERSION 3.4)

project(cheeky_companion)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CHEEKY_COMPANION_DISCORD "Support networking over Discord's Game SDK" OFF)

find_package(glm REQUIRED)

file(GLOB_RECURSE sources src/**.cpp)

add_library(cheeky_companion SHARED ${sources})
target_include_directories(cheeky_companion PUBLIC include/)

set(CHEEKY_LAYER_DOCS OFF CACHE BOOL "")
add_subdirectory(external/cheeky-imp/vulkan_layer)
target_link_libraries(cheeky_companion PUBLIC cheeky_layer)

set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(external/json)
target_link_libraries(cheeky_companion PUBLIC nlohmann_json::nlohmann_json)

target_include_directories(cheeky_companion PRIVATE ${GLM_INCLUDE_DIRS})
target_include_directories(cheeky_companion PRIVATE external/stb)

if(CHEEKY_COMPANION_DISCORD)
	if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/discord_game_sdk.so)
		file(DOWNLOAD https://dl-game-sdk.discordapp.net/2.5.6/discord_game_sdk.zip ${CMAKE_CURRENT_BINARY_DIR}/discord_game_sdk.zip
			SHOW_PROGRESS EXPECTED_HASH SHA256=426eb5fa70647d884f461c63825b63668349efb4bc68a16e70bc4a24e119b92e)
		file(ARCHIVE_EXTRACT INPUT ${CMAKE_CURRENT_BINARY_DIR}/discord_game_sdk.zip DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/discord_game_sdk/)
		file(CREATE_LINK ${CMAKE_CURRENT_BINARY_DIR}/discord_game_sdk/lib/x86_64/discord_game_sdk.so ${CMAKE_CURRENT_BINARY_DIR}/discord_game_sdk.so SYMBOLIC COPY_ON_ERROR)
	endif()

	add_library(discord_game_sdk SHARED IMPORTED)
	set_target_properties(discord_game_sdk PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/discord_game_sdk.so)

	file(GLOB wrapper-sources ${CMAKE_CURRENT_BINARY_DIR}/discord_game_sdk/cpp/*.cpp)
	add_library(discord_game_sdk_cpp ${wrapper-sources})
	target_include_directories(discord_game_sdk_cpp PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/discord_game_sdk/cpp/)
	target_link_libraries(discord_game_sdk_cpp PUBLIC discord_game_sdk)
	target_compile_options(discord_game_sdk_cpp PRIVATE -include cstdint)

	target_link_libraries(cheeky_companion PRIVATE discord_game_sdk_cpp)
endif()

add_subdirectory(test)
add_subdirectory(clients)
