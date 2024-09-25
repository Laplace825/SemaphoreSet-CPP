cmake_minimum_required(VERSION 3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Logging.cmake")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_COMPILER "clang++")

project(SemaphoreSet LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckCXXCompilerVersion.cmake")
set(EXECUTABLE_OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(LIBRARY_OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lib")

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")
add_library(semaphore_set_lib STATIC ${SOURCES})
target_include_directories(semaphore_set_lib
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_executable(${PROJECT_NAME} main.cc)
target_include_directories(${PROJECT_NAME}
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PROJECT_NAME} semaphore_set_lib)
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
