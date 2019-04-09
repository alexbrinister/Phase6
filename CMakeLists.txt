# Minimum CMake is 3.3
cmake_minimum_required(VERSION 3.3)

# Set project name
project(EECE4830Phase6)

# Find include files
include_directories(inc)

# Find source files
set(SRC_DIR src)
file(GLOB SOURCES ${SRC_DIR}/*.cpp)

# Name for the binary
set(BIN phase6)

# Set build to 64-bit
set(CMAKE_GENERATOR_PLATFORM x64)

# Set up regular, Release and Debug C flags
if(MSVC)
    add_compile_options(/W3 /WX)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Werror -Wextra)
    add_compile_options("$<$<CONFIG:Debug>:-O0;-g;>")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_executable(${BIN} ${SOURCES})

if(MSVC)
    target_link_libraries(${BIN} ws2_32)
endif()

# Set up debug postfix
set_property(TARGET ${BIN} PROPERTY CXX_STANDARD 17)

set(CMAKE_DEBUG_POSTFIX _debug.bin)
set(CMAKE_RELEASE_POSTFIX _release.bin)
set_target_properties(${BIN} 
    PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
set_target_properties(${BIN} 
    PROPERTIES RELEASE_POSTFIX ${CMAKE_RELEASE_POSTFIX})

# vim: set expandtab ts=4 sw=4:
