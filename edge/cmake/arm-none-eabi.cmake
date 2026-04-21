# CMake toolchain file for ARM Cortex-M4F (STM32F407G-DISC1).
#
# Invoked with: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake ...
# Tells CMake: "don't use host gcc, use arm-none-eabi-gcc, target is bare-metal M4F."

set(CMAKE_SYSTEM_NAME      Generic)    # Bare-metal, no OS.
set(CMAKE_SYSTEM_PROCESSOR arm)

# --- Toolchain binaries (must be on PATH) -----------------------------------
set(TOOLCHAIN_PREFIX arm-none-eabi-)

set(CMAKE_C_COMPILER    ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER  ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER  ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_AR            ${TOOLCHAIN_PREFIX}ar)
set(CMAKE_OBJCOPY       ${TOOLCHAIN_PREFIX}objcopy  CACHE INTERNAL "")
set(CMAKE_OBJDUMP       ${TOOLCHAIN_PREFIX}objdump  CACHE INTERNAL "")
set(CMAKE_SIZE          ${TOOLCHAIN_PREFIX}size     CACHE INTERNAL "")

# CMake's default compiler sanity check tries to link+run an executable.
# Bare-metal can't run on host → switch test mode to STATIC_LIBRARY (compile only).
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# --- CPU / FPU flags — applied to all languages -----------------------------
# -mcpu=cortex-m4 : Cortex-M4 core.
# -mthumb         : Thumb instruction set (Cortex-M only supports Thumb).
# -mfpu=fpv4-sp-d16: single-precision FPU on F4 (16 double-word regs).
# -mfloat-abi=hard: pass floats in FPU registers (fastest; requires FPU).
set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS_INIT   "${CPU_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CPU_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS} -x assembler-with-cpp")

# Don't let find_package / find_library poke at host system paths.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
