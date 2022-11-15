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
  OPTIONS "ASSIMP_NO_EXPORT ON" "ASSIMP_BUILD_TESTS OFF" "ASSIMP_INSTALL OFF" "ASSIMP_BUILD_ASSIMP_VIEW OFF" "BUILD_SHARED_LIBS ON"
)
CPMAddPackage(
  NAME gfxminimath
  GITHUB_REPOSITORY "jimbi-o/gfxminimath"
  GIT_TAG "e5b289ae4f4ac97755b053c5b2a97da6e4538c56"
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
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog assimp gfxminimath)

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
  "${gfxminimath_SOURCE_DIR}/include"
)
add_subdirectory(src)
