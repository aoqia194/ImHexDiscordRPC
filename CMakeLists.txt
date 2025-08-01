cmake_minimum_required(VERSION 3.30)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

project(ImHexDiscordRPC LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

include(ImHexSDK)
add_subdirectory("${CMAKE_MODULE_PATH}/DiscordGameSDK")

# Source file globs

set(SRC_DIR "${PROJECT_SOURCE_DIR}/src")

file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.hpp")
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")

add_imhex_plugin(
    NAME
        ${CMAKE_PROJECT_NAME}
    SOURCES
        ${SOURCE_FILES}
#    INCLUDES
    LIBRARIES
        discord_game_sdk
)

if (CMAKE_COMPILER_IS_GNUCXX)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Werror)
elseif (MSVC)
    target_compile_options(${PROJECT_NAME} PROJECT /W4 /WX)
endif ()

# I only do this because I saw some other plugins do this???
if (APPLE)
    target_link_options(imhex_discordrpc PRIVATE -undefined dynamic_lookup -flat_namespace)
endif ()
