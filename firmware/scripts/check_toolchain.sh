#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Martin Faltus (mafaltu)
# Prints the versions of every tool the firmware build needs; exit 1 if any is missing.
ARM_TOOLCHAIN_DIR="${ARM_TOOLCHAIN_DIR:-$HOME/.pico-sdk/toolchain/15_2_Rel1}"
STM32_PROGRAMMER_CLI="${STM32_PROGRAMMER_CLI:-$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI}"

status=0
show() { # label, command..., prints first line of version output or flags missing
  local label="$1"; shift
  local out
  if out=$("$@" 2>&1); then
    printf '%-22s %s\n' "$label" "$(printf '%s\n' "$out" | sed 's/\x1b\[[0-9;]*m//g' | grep -m1 -i -E '[0-9]+\.[0-9]+')"
  else
    printf '%-22s MISSING (%s)\n' "$label" "$*"
    status=1
  fi
}

ARM_GCC="arm-none-eabi-gcc"
if ! command -v "$ARM_GCC" >/dev/null 2>&1; then
  ARM_GCC="$ARM_TOOLCHAIN_DIR/bin/arm-none-eabi-gcc"
fi

show cmake cmake --version
show gcc gcc --version
show arm-none-eabi-gcc "$ARM_GCC" --version
show dfu-util dfu-util --version
show STM32_Programmer_CLI "$STM32_PROGRAMMER_CLI" --version
exit $status
