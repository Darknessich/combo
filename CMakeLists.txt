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

add_executable(json examples/json.cpp)
target_link_libraries(json PRIVATE combo)

add_executable(json_gen examples/json_gen.cpp)
target_link_libraries(json_gen PRIVATE combo)

add_executable(maze examples/maze.cpp)
target_link_libraries(maze PRIVATE combo)

enable_testing()
add_executable(combo_tests tests/tests.cpp)
target_link_libraries(combo_tests PRIVATE combo)
add_test(NAME combo_tests COMMAND combo_tests)
