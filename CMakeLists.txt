cmake_minimum_required(VERSION 3.26)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_COMPILER "clang++")

project(SHU_OS_DIS_2 LANGUAGES CXX)

add_executable(${PROJECT_NAME} main.cc)
target_include_directories(${PROJECT_NAME}
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
target_compile_options(
  ${PROJECT_NAME}
  PRIVATE -Wall
          -Wextra
          -Werror
          -Wpedantic
          -Wno-unused-parameter
          -Wno-unused-variable
          -Wno-unused-function
          -Wno-unused-private-field)
