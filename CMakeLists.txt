cmake_minimum_required(VERSION 3.15)
project(modelconv
  VERSION 1.0
  DESCRIPTION "model converter for illuminate renderer"
  LANGUAGES CXX
)

include(cmake/CPM.cmake)
CPMAddPackage("gh:doctest/doctest@2.4.9")
CPMAddPackage("gh:gabime/spdlog@1.10.0")
CPMAddPackage(
  NAME assimp
  GITHUB_REPOSITORY assimp/assimp
  VERSION 5.2.5
  OPTIONS "ASSIMP_NO_EXPORT ON" "ASSIMP_BUILD_TESTS OFF" "ASSIMP_INSTALL OFF" "ASSIMP_BUILD_ASSIMP_VIEW OFF" "BUILD_SHARED_LIBS OFF"
)
CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY "nlohmann/json"
  VERSION 3.10.5
  OPTIONS
  "JSON_BuildTests OFF"
)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  add_executable(${CMAKE_PROJECT_NAME})
	target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DOCTEST_CONFIG_SUPER_FAST_ASSERTS)
	target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC "${doctest_SOURCE_DIR}")
	add_subdirectory(tests)
else()
  add_library(${PROJECT_NAME})
endif()
target_include_directories(${PROJECT_NAME} SYSTEM INTERFACE spdlog)
set_target_properties(spdlog PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:spdlog,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog assimp)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
  $<$<CXX_COMPILER_ID:Clang>:-Weverything -Wno-c++98-c++11-c++14-compat -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-c++20-compat>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /MP>
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>)
target_include_directories(${PROJECT_NAME}
  PUBLIC
  "include"
  PRIVATE
  "${assimp_SOURCE_DIR}/include"
  "${assimp_BINARY_DIR}/include"
  "${nlohmann_json_SOURCE_DIR}/include"
)
target_precompile_headers(${CMAKE_PROJECT_NAME}
  PRIVATE
  <cstdint>
  <cstdio>
  <cstring>
  <filesystem>
  <fstream>
  <iostream>
  <string>
  <string_view>
  <type_traits>
  <utility>
  <vector>
  "${assimp_SOURCE_DIR}/include/assimp/Importer.hpp"
  "${assimp_SOURCE_DIR}/include/assimp/postprocess.h"
  "${assimp_SOURCE_DIR}/include/assimp/scene.h"
  "${nlohmann_json_SOURCE_DIR}/include/nlohmann/json.hpp"
  "${spdlog_SOURCE_DIR}/include/spdlog/spdlog.h"
)
add_subdirectory(src)

set_directory_properties(PROPERTIES VS_STARTUP_PROJECT modelconv)
set_target_properties(modelconv PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources")
