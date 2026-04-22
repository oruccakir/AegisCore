cmake_minimum_required(VERSION 3.22)

project(aegiscore-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(edge-unit-tests
    unit/edge_logic_tests.cpp
    ../edge/app/button_classifier.cpp
    ../edge/app/simulation_engine.cpp
    ../edge/app/state_machine.cpp
)

target_include_directories(edge-unit-tests PRIVATE
    ../edge/app
)

target_compile_options(edge-unit-tests PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)

add_test(NAME edge-unit-tests COMMAND edge-unit-tests)
