cmake_minimum_required(VERSION 3.16)
project(combo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Header-only combinator library.
add_library(combo INTERFACE)
target_include_directories(combo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(combo INTERFACE cxx_std_20)
