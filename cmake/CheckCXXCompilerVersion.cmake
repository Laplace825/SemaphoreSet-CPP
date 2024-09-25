include(CheckCXXCompilerFlag)

check_cxx_compiler_flag("-std=c++20" COMPILER_SUPPORTS_CXX20)

if(COMPILER_SUPPORTS_CXX20)
    log_info("The compiler ${CMAKE_CXX_COMPILER} has C++20 support. Enabling C++20")
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
else()
    log_warning("The compiler ${CMAKE_CXX_COMPILER} has no C++20 support. Change to use C++11")
endif()