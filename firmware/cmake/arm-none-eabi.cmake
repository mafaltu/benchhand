# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Martin Faltus (mafaltu)
# Toolchain file for the STM32C071 (Cortex-M0+). Use: -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake
# ARM_TOOLCHAIN_DIR (env or -D) = Arm GNU Toolchain root (the directory containing bin/).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT ARM_TOOLCHAIN_DIR)
  if(DEFINED ENV{ARM_TOOLCHAIN_DIR})
    set(ARM_TOOLCHAIN_DIR "$ENV{ARM_TOOLCHAIN_DIR}")
  else()
    set(ARM_TOOLCHAIN_DIR "$ENV{HOME}/.pico-sdk/toolchain/15_2_Rel1")
  endif()
endif()

set(_tc_bin "${ARM_TOOLCHAIN_DIR}/bin")
set(CMAKE_C_COMPILER   "${_tc_bin}/arm-none-eabi-gcc")
set(CMAKE_ASM_COMPILER "${_tc_bin}/arm-none-eabi-gcc")
set(CMAKE_OBJCOPY      "${_tc_bin}/arm-none-eabi-objcopy")
set(CMAKE_SIZE         "${_tc_bin}/arm-none-eabi-size")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
