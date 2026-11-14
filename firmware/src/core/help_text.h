/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#ifndef CORE_HELP_TEXT_H
#define CORE_HELP_TEXT_H
#include <stdbool.h>
#include <stdint.h>

/* HELP text (design spec 5.4, FW-17, FW-24). Every line starts with "# " and has no line terminator.
 * In the pages, "# > " marks a line the agent sends and "# < " the board's reply (checked by test_help).
 * topic "" = introduction + command list; otherwise a command keyword (case-insensitive), e.g. "SET". */
const char *help_line(const char *topic, uint16_t idx); /* NULL after the last line */
bool help_topic_exists(const char *topic);
/* Topic keyword i (0-based) for iteration; NULL after the last one. */
const char *help_topic_name(uint16_t i);

#endif
