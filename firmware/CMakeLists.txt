# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Martin Faltus (mafaltu)
cmake_minimum_required(VERSION 3.20)

option(FW_TARGET "Build the STM32C071 target firmware (ON) or the host tests (OFF)" OFF)
# The only place with the default build identity. It reaches the C code through a generated build_config.h
# (cmake/build_config.h.in, included by src/core/build_info.h), not through -D defines: a '#' in REPO_URL makes
# CMake drop a compile definition. Host tests and the PROTOCOL.md generator always get the defaults (reproducible,
# FW-38); only the target build uses the FW_VERSION / REPO_URL cache values.
set(FW_VERSION_DEFAULT "0.1.0")
set(REPO_URL_DEFAULT "https://github.com/mafaltu/benchhand#for-ai-agents")
set(FW_VERSION "${FW_VERSION_DEFAULT}" CACHE STRING "Firmware version")

# fw_generate_build_config(<outdir> <version> <url>): writes <outdir>/build_config.h with C-escaped literals.
function(fw_generate_build_config outdir version url)
  foreach(_v version url)
    string(REPLACE "\\" "\\\\" ${_v} "${${_v}}")
    string(REPLACE "\"" "\\\"" ${_v} "${${_v}}")
  endforeach()
  set(FW_VERSION_C "${version}")
  set(REPO_URL_C "${url}")
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_config.h.in ${outdir}/build_config.h @ONLY)
endfunction()

# FW_TARGET=ON uses the in-tree toolchain file. CMake resolves a relative CMAKE_TOOLCHAIN_FILE against the build
# and source directories only, so `-DCMAKE_TOOLCHAIN_FILE=firmware/cmake/arm-none-eabi.cmake` given from the repo
# root would not be found; an unset or unresolvable value is mapped to the in-tree file here.
if(FW_TARGET)
  set(_fw_toolchain ${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-none-eabi.cmake)
  if(NOT CMAKE_TOOLCHAIN_FILE
     OR (NOT IS_ABSOLUTE "${CMAKE_TOOLCHAIN_FILE}"
         AND NOT EXISTS "${CMAKE_BINARY_DIR}/${CMAKE_TOOLCHAIN_FILE}"
         AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_TOOLCHAIN_FILE}"))
    message(STATUS "FW_TARGET: toolchain file = ${_fw_toolchain}")
    set(CMAKE_TOOLCHAIN_FILE ${_fw_toolchain} CACHE FILEPATH "Toolchain file" FORCE)
  endif()
endif()

if(FW_TARGET)
  project(usb_debug_ctrl C ASM)

  set(REPO_URL "${REPO_URL_DEFAULT}" CACHE STRING "Project repository URL shown by HELP (FW-20)")
  set(FW_TEST_STALL_MS "" CACHE STRING
      "Debug only: stop refreshing the IWDG this many ms after start (watchdog bench check m-12); empty = off")
  option(FW_DEBUG_UART "Debug only: USART1 TX log on PA9 (TC2050 PROG_TX), 115200 8N1 (FW-32)" OFF)

  set(LIB ${CMAKE_CURRENT_SOURCE_DIR}/lib)
  set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  set(LDSCRIPT ${SRC}/target/stm32c071k8_flash.ld)

  set(ARM_FLAGS -mcpu=cortex-m0plus -mthumb)
  set(COMMON_C_FLAGS ${ARM_FLAGS} -std=gnu11 -Os -g -ffunction-sections -fdata-sections -fstack-usage
      -fcallgraph-info=su)
  set(TARGET_INCLUDES
    ${SRC}
    ${SRC}/target
    ${LIB}/stm32c0xx_hal_driver/Inc
    ${LIB}/cmsis_device_c0/Include
    ${LIB}/CMSIS_5/CMSIS/Core/Include)
  set(TARGET_DEFS STM32C071xx USE_HAL_DRIVER)

  # Vendor HAL (only the modules enabled in src/target/stm32c0xx_hal_conf.h); unused code is dropped by
  # --gc-sections. Built as its own library so vendor warnings are kept apart from the -Werror project code.
  add_library(stm32c0_hal STATIC
    ${LIB}/stm32c0xx_hal_driver/Src/stm32c0xx_hal.c
    ${LIB}/stm32c0xx_hal_driver/Src/stm32c0xx_hal_cortex.c
    ${LIB}/stm32c0xx_hal_driver/Src/stm32c0xx_hal_iwdg.c
    ${LIB}/stm32c0xx_hal_driver/Src/stm32c0xx_hal_rcc.c) # PWR not needed: power.c writes PWR_CR1 directly (Task 11)
  target_include_directories(stm32c0_hal PUBLIC ${TARGET_INCLUDES})
  target_compile_definitions(stm32c0_hal PUBLIC ${TARGET_DEFS})
  target_compile_options(stm32c0_hal PRIVATE ${COMMON_C_FLAGS} -Wall -Wextra)

  # TinyUSB 0.21.0 (submodule lib/tinyusb): device stack, CDC class, STM32 FSDEV driver. Configured by
  # src/target/tusb_config.h. Own library so vendor code is kept apart from the -Werror project code.
  set(TUSB ${LIB}/tinyusb/src)
  add_library(tinyusb STATIC
    ${TUSB}/tusb.c
    ${TUSB}/common/tusb_fifo.c
    ${TUSB}/device/usbd.c
    ${TUSB}/class/cdc/cdc_device.c
    ${TUSB}/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
    ${TUSB}/portable/st/stm32_fsdev/fsdev_common.c)
  target_include_directories(tinyusb PUBLIC ${TUSB} ${TARGET_INCLUDES})
  target_compile_definitions(tinyusb PUBLIC ${TARGET_DEFS})
  target_compile_options(tinyusb PRIVATE ${COMMON_C_FLAGS} -Wall -Wextra)

  set(CORE_SRCS app.c bootrec.c buttons.c debounce.c help_text.c inputs.c linebuf.c passthrough.c protocol.c
      relays.c seq.c)
  list(TRANSFORM CORE_SRCS PREPEND ${SRC}/core/)

  add_executable(usb_debug_ctrl.elf
    ${CORE_SRCS}
    ${SRC}/target/board.c
    ${SRC}/target/boot.c
    ${SRC}/target/clock.c
    ${SRC}/target/main.c
    ${SRC}/target/power.c                    # USB suspend -> Stop (Task 11)
    ${SRC}/target/syscalls.c
    ${SRC}/target/usb_descriptors.c
    ${SRC}/target/usb_glue.c                 # TinyUSB CDC glue (Task 10; was usb_stub.c)
    ${SRC}/target/system_stm32c0xx.c         # owned copy (M-7)
    ${SRC}/target/startup_stm32c071xx.s)     # owned copy (M-7)
  fw_generate_build_config(${CMAKE_BINARY_DIR}/generated "${FW_VERSION}" "${REPO_URL}")
  target_include_directories(usb_debug_ctrl.elf PRIVATE ${CMAKE_BINARY_DIR}/generated)
  if(NOT FW_TEST_STALL_MS STREQUAL "")
    target_compile_definitions(usb_debug_ctrl.elf PRIVATE FW_TEST_STALL_MS=${FW_TEST_STALL_MS})
    message(WARNING "FW_TEST_STALL_MS=${FW_TEST_STALL_MS}: DEBUG BUILD, the IWDG resets the board repeatedly")
  endif()
  if(FW_DEBUG_UART)
    # Off (default): dbg_uart.h provides empty stubs, no USART code is linked and PA9/PA10 stay analog.
    target_sources(usb_debug_ctrl.elf PRIVATE ${SRC}/target/dbg_uart.c ${SRC}/target/dbg_ring.c)
    target_compile_definitions(usb_debug_ctrl.elf PRIVATE FW_DEBUG_UART=1)
    message(WARNING "FW_DEBUG_UART=ON: DEBUG BUILD, USART1 log on PA9; not for production, suspend current not "
                    "representative")
  endif()
  target_compile_options(usb_debug_ctrl.elf PRIVATE
    $<$<COMPILE_LANGUAGE:C>:${COMMON_C_FLAGS} -Wall -Wextra -Werror>
    $<$<COMPILE_LANGUAGE:ASM>:${ARM_FLAGS} -x assembler-with-cpp>)
  target_link_libraries(usb_debug_ctrl.elf PRIVATE tinyusb stm32c0_hal)
  target_link_options(usb_debug_ctrl.elf PRIVATE
    ${ARM_FLAGS} -T${LDSCRIPT} -Wl,--gc-sections -specs=nano.specs -specs=nosys.specs
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/usb_debug_ctrl.map -Wl,--print-memory-usage)
  set_target_properties(usb_debug_ctrl.elf PROPERTIES LINK_DEPENDS ${LDSCRIPT})

  add_custom_command(TARGET usb_debug_ctrl.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary usb_debug_ctrl.elf usb_debug_ctrl.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex usb_debug_ctrl.elf usb_debug_ctrl.hex
    COMMAND ${CMAKE_SIZE} usb_debug_ctrl.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    BYPRODUCTS usb_debug_ctrl.bin usb_debug_ctrl.hex usb_debug_ctrl.map
    COMMENT "objcopy .bin/.hex and size")
else()
  project(usb_debug_ctrl_host C)
  enable_testing()

  fw_generate_build_config(${CMAKE_BINARY_DIR}/generated "${FW_VERSION_DEFAULT}" "${REPO_URL_DEFAULT}")
  set(HOST_FLAGS -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined)

  # add_core_test(name sources...): builds test/test_<name>.c with the listed core sources
  # (paths relative to src/core) and registers it with ctest.
  function(add_core_test name)
    set(srcs)
    foreach(s IN LISTS ARGN)
      list(APPEND srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/core/${s})
    endforeach()
    add_executable(test_${name} ${CMAKE_CURRENT_SOURCE_DIR}/test/test_${name}.c ${srcs})
    target_include_directories(test_${name} PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/test)
    target_compile_options(test_${name} PRIVATE ${HOST_FLAGS})
    target_include_directories(test_${name} PRIVATE ${CMAKE_BINARY_DIR}/generated)
    target_link_options(test_${name} PRIVATE -fsanitize=address,undefined)
    add_test(NAME ${name} COMMAND test_${name})
  endfunction()

  add_core_test(smoke)
  add_core_test(linebuf linebuf.c)
  add_core_test(protocol protocol.c)
  add_core_test(relays relays.c)
  add_core_test(buttons_inputs debounce.c buttons.c inputs.c)
  add_core_test(passthrough passthrough.c)
  add_core_test(seq seq.c relays.c debounce.c inputs.c passthrough.c)
  add_core_test(help help_text.c protocol.c)
  add_core_test(app app.c help_text.c linebuf.c protocol.c relays.c debounce.c buttons.c inputs.c passthrough.c
                seq.c)
  add_core_test(bootrec bootrec.c)
  # FW-32 debug log queue: hardware-free target unit (linked into the target only with FW_DEBUG_UART=ON).
  add_core_test(dbg_ring)
  target_sources(test_dbg_ring PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/target/dbg_ring.c)

  # FW-38: docs/PROTOCOL.md is generated from the HELP pages; the generator uses the same defaults-only header,
  # so the check is reproducible whatever the cache holds.
  add_executable(gen_protocol_md ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_protocol_md.c
                 ${CMAKE_CURRENT_SOURCE_DIR}/src/core/help_text.c)
  target_include_directories(gen_protocol_md PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated)
  target_compile_options(gen_protocol_md PRIVATE ${HOST_FLAGS})
  target_link_options(gen_protocol_md PRIVATE -fsanitize=address,undefined)
  set(PROTOCOL_MD ${CMAKE_CURRENT_SOURCE_DIR}/docs/PROTOCOL.md)
  add_custom_target(protocol_md
    COMMAND gen_protocol_md ${PROTOCOL_MD}
    DEPENDS gen_protocol_md
    COMMENT "Regenerating docs/PROTOCOL.md from src/core/help_text.c"
    VERBATIM)
  add_test(NAME protocol_md_fresh
    COMMAND ${CMAKE_COMMAND} -DGEN=$<TARGET_FILE:gen_protocol_md> -DDOC=${PROTOCOL_MD}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_protocol_md.cmake)
endif()
