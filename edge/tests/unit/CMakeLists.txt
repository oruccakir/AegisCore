cmake_minimum_required(VERSION 3.22)
project(aegiscore-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ---------------------------------------------------------------------------
# GoogleTest via FetchContent.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(BUILD_GMOCK   OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# Paths back to firmware sources.
# ---------------------------------------------------------------------------
set(APP_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../../app)
set(BSP_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../../bsp)
set(MOCK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mocks)

# ---------------------------------------------------------------------------
# Pure-logic sources under test (no HAL / no FreeRTOS).
# ---------------------------------------------------------------------------
set(UNDER_TEST_SOURCES
    ${APP_DIR}/ac2_framer.cpp
    ${APP_DIR}/button_classifier.cpp
    ${APP_DIR}/crc16.cpp
    ${APP_DIR}/fail_safe_supervisor.cpp
    ${APP_DIR}/hmac_sha256.cpp
    ${APP_DIR}/rate_limiter.cpp
    ${APP_DIR}/replay_guard.cpp
    ${APP_DIR}/simulation_engine.cpp
    ${APP_DIR}/state_machine.cpp
)

add_executable(aegiscore-tests
    test_ac2_framer.cpp
    test_button_classifier.cpp
    test_crc16.cpp
    test_hmac_sha256.cpp
    test_rate_limiter.cpp
    test_replay_guard.cpp
    test_state_machine.cpp
    ${UNDER_TEST_SOURCES}
)

target_include_directories(aegiscore-tests PRIVATE
    ${APP_DIR}
    ${BSP_DIR}
    ${MOCK_DIR}
)

target_compile_definitions(aegiscore-tests PRIVATE HOST_BUILD=1)

target_compile_options(aegiscore-tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -fno-exceptions -fno-rtti
)

target_link_libraries(aegiscore-tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(aegiscore-tests)
