/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_BUILD_INFO_H
#define CORE_BUILD_INFO_H

/* Build-time identity, shared by the host tests and the target (M25). FW_VERSION and REPO_URL come from the
 * build_config.h that CMake generates (defaults live only in CMakeLists.txt; -DFW_VERSION / -DREPO_URL override them
 * for the target build). */
#include "build_config.h"

/* Protocol version reported as proto= by ID? and printed by tools/gen_protocol_md.c (FW-38). Not a CMake
 * option: it changes only with the command set. */
#define PROTO_VERSION "1"

#endif
