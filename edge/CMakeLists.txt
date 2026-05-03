cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# AegisCore Edge Node — firmware for STM32F407G-DISC1.
# Build:   cmake -S edge -B edge/build -G Ninja \
#                -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
#          cmake --build edge/build
# Flash:   st-flash write edge/build/aegiscore-edge.bin 0x8000000
# ---------------------------------------------------------------------------

project(aegiscore-edge LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD             11)
set(CMAKE_C_STANDARD_REQUIRED    ON)
set(CMAKE_CXX_STANDARD           20)
set(CMAKE_CXX_STANDARD_REQUIRED  ON)
set(CMAKE_CXX_EXTENSIONS         OFF)

# ---------------------------------------------------------------------------
# Paths to vendor sources (STM32CubeF4 has nested submodules).
# ---------------------------------------------------------------------------
set(CUBE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/STM32CubeF4)
set(CMSIS_DEVICE_DIR ${CUBE_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx)
set(CMSIS_CORE_DIR   ${CUBE_DIR}/Drivers/CMSIS/Include)
set(HAL_DIR          ${CUBE_DIR}/Drivers/STM32F4xx_HAL_Driver)
set(FREERTOS_DIR     ${CUBE_DIR}/Middlewares/Third_Party/FreeRTOS/Source)
set(FREERTOS_PORT_DIR ${FREERTOS_DIR}/portable/GCC/ARM_CM4F)

# ---------------------------------------------------------------------------
# HAL sources — enumerate explicitly to keep binary auditable (ARCH-05).
# ---------------------------------------------------------------------------
set(HAL_SOURCES
    ${HAL_DIR}/Src/stm32f4xx_hal.c
    ${HAL_DIR}/Src/stm32f4xx_hal_cortex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_rcc.c
    ${HAL_DIR}/Src/stm32f4xx_hal_rcc_ex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_gpio.c
    ${HAL_DIR}/Src/stm32f4xx_hal_pwr.c
    ${HAL_DIR}/Src/stm32f4xx_hal_pwr_ex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_flash.c
    ${HAL_DIR}/Src/stm32f4xx_hal_flash_ex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_tim.c
    ${HAL_DIR}/Src/stm32f4xx_hal_uart.c
    ${HAL_DIR}/Src/stm32f4xx_hal_dma.c
    ${HAL_DIR}/Src/stm32f4xx_hal_dma_ex.c
    ${HAL_DIR}/Src/stm32f4xx_hal_iwdg.c
)

# ---------------------------------------------------------------------------
# FreeRTOS kernel subset for the Phase 2 state core.
# ---------------------------------------------------------------------------
set(FREERTOS_SOURCES
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_PORT_DIR}/port.c
)

# ---------------------------------------------------------------------------
# First-party application sources.
# ---------------------------------------------------------------------------
set(APP_SOURCES
    app/comms/ac2_framer.cpp
    app/comms/command_dispatch.cpp
    app/comms/crc16.cpp
    app/comms/heartbeat_task.cpp
    app/comms/hmac_sha256.cpp
    app/comms/rate_limiter.cpp
    app/comms/replay_guard.cpp
    app/comms/telemetry_task.cpp
    app/comms/uart_rx_task.cpp
    app/control/button_classifier.cpp
    app/control/simulation_engine.cpp
    app/control/state_machine.cpp
    app/drivers/lcd_driver.cpp
    app/drivers/platform_io.cpp
    app/main.cpp
    app/runtime/edge_runtime.cpp
    app/safety/fail_safe_supervisor.cpp
    app/safety/freertos_hooks.cpp
    app/safety/post.cpp
    app/tasks/joystick_task.cpp
    app/tasks/lcd_status_task.cpp
    app/tasks/state_machine_task.cpp
    app/tasks/user_tasks.cpp
)

set(BSP_SOURCES
    bsp/fault_stubs.c
    bsp/mpu_config.cpp
    bsp/panic.cpp
    bsp/uart_driver.cpp
    bsp/watchdog.cpp
)

# ---------------------------------------------------------------------------
# Firmware executable.
# ---------------------------------------------------------------------------
set(ELF_TARGET aegiscore-edge.elf)

add_executable(${ELF_TARGET}
    startup/startup_stm32f407xx.s
    bsp/system_stm32f4xx.c
    bsp/stm32f4xx_it.c
    ${APP_SOURCES}
    ${BSP_SOURCES}
    ${HAL_SOURCES}
    ${FREERTOS_SOURCES}
)

target_include_directories(${ELF_TARGET} PRIVATE
    app
    app/comms
    app/control
    app/drivers
    app/runtime
    app/safety
    app/tasks
    bsp                 # stm32f4xx_hal_conf.h lives here
    ${CMSIS_DEVICE_DIR}/Include
    ${CMSIS_CORE_DIR}
    ${FREERTOS_DIR}/include
    ${FREERTOS_PORT_DIR}
    ${HAL_DIR}/Inc
)

# Git SHA + build timestamp for version.hpp (DEP-02).
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE AEGIS_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT AEGIS_GIT_SHA)
    set(AEGIS_GIT_SHA "00000000")
endif()
string(TIMESTAMP AEGIS_BUILD_TS "%s" UTC)

target_compile_definitions(${ELF_TARGET} PRIVATE
    STM32F407xx
    USE_HAL_DRIVER
    HSE_VALUE=8000000U
    AEGIS_GIT_SHA="${AEGIS_GIT_SHA}"
    AEGIS_BUILD_TS=${AEGIS_BUILD_TS}U
)

# ---------------------------------------------------------------------------
# Strict warning discipline (NFR-MAINT-01: zero warnings, warnings-as-errors).
# ---------------------------------------------------------------------------
target_compile_options(${ELF_TARGET} PRIVATE
    # Language-wide
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wall -Wextra -Wpedantic>
    # HAL (vendor code) is NOT treated as error — we only police our own code.
    # Per-file suppression happens below.

    # Code-size / link-time dead-code elimination
    -ffunction-sections
    -fdata-sections

    # C++ tidy-ups for bare-metal
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>   # ARCH-02: no dynamic alloc / unwind
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>         # size + determinism
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
)

target_compile_options(${ELF_TARGET} PRIVATE $<$<CONFIG:Debug>:-Og -g3>)
target_compile_options(${ELF_TARGET} PRIVATE $<$<CONFIG:Release>:-Os -g>)

# Treat only our own sources as warnings-as-errors — vendor HAL may warn.
set_source_files_properties(
    app/comms/ac2_framer.cpp
    app/comms/command_dispatch.cpp
    app/comms/crc16.cpp
    app/comms/heartbeat_task.cpp
    app/comms/hmac_sha256.cpp
    app/comms/rate_limiter.cpp
    app/comms/replay_guard.cpp
    app/comms/telemetry_task.cpp
    app/comms/uart_rx_task.cpp
    app/control/button_classifier.cpp
    app/control/simulation_engine.cpp
    app/control/state_machine.cpp
    app/drivers/lcd_driver.cpp
    app/drivers/platform_io.cpp
    app/main.cpp
    app/runtime/edge_runtime.cpp
    app/safety/fail_safe_supervisor.cpp
    app/safety/freertos_hooks.cpp
    app/safety/post.cpp
    app/tasks/joystick_task.cpp
    app/tasks/lcd_status_task.cpp
    app/tasks/state_machine_task.cpp
    app/tasks/user_tasks.cpp
    bsp/mpu_config.cpp
    bsp/panic.cpp
    bsp/stm32f4xx_it.c
    bsp/uart_driver.cpp
    bsp/watchdog.cpp
    PROPERTIES COMPILE_OPTIONS "-Werror"
)

# ---------------------------------------------------------------------------
# Linker
# ---------------------------------------------------------------------------
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F407VGTx_FLASH.ld)

target_link_options(${ELF_TARGET} PRIVATE
    -T${LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/aegiscore-edge.map
    --specs=nano.specs       # newlib-nano (smaller libc)
    --specs=nosys.specs      # no host syscalls (bare-metal stubs)
)

# Re-link when the linker script changes.
set_target_properties(${ELF_TARGET} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

# ---------------------------------------------------------------------------
# Post-build: produce .bin (for st-flash) and print section sizes.
# ---------------------------------------------------------------------------
set(BIN_FILE $<TARGET_FILE_DIR:${ELF_TARGET}>/aegiscore-edge.bin)
set(HEX_FILE $<TARGET_FILE_DIR:${ELF_TARGET}>/aegiscore-edge.hex)

add_custom_command(TARGET ${ELF_TARGET} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${ELF_TARGET}> ${BIN_FILE}
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${ELF_TARGET}> ${HEX_FILE}
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${ELF_TARGET}>
    COMMENT "Generating .bin/.hex and section sizes"
)
