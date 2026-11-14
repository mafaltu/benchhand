/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */

/* Host tool (FW-38): prints docs/PROTOCOL.md, generated from the HELP pages in src/core/help_text.c through its
 * public API, so every topic is included and REPO_URL / FW_VERSION are expanded exactly as on the board.
 * Usage: gen_protocol_md [<output file>]   (no argument: write to stdout). Output is deterministic. */
#include <stdint.h>
#include <stdio.h>

#include "core/build_info.h"
#include "core/help_text.h"

static int put_page(FILE *f, const char *topic)
{
    fputs("```text\n", f);
    for (uint16_t i = 0;; i++) {
        const char *l = help_line(topic, i);
        if (l == NULL) break;
        if (l[0] != '#' || (l[1] != ' ' && l[1] != '\0')) {
            fprintf(stderr, "gen_protocol_md: HELP %s line %u does not start with \"# \"\n", topic, i);
            return 1;
        }
        fputs(l[1] == ' ' ? l + 2 : l + 1, f);
        fputc('\n', f);
    }
    fputs("```\n", f);
    return 0;
}

int main(int argc, char **argv)
{
    FILE *f = stdout;
    if (argc > 2) {
        fprintf(stderr, "usage: %s [<output file>]\n", argv[0]);
        return 2;
    }
    if (argc == 2) {
        f = fopen(argv[1], "wb");
        if (f == NULL) {
            perror(argv[1]);
            return 1;
        }
    }

    int rc = 0;
    fputs("<!-- SPDX-License-Identifier: GPL-3.0-or-later -->\n"
          "<!-- Copyright (c) 2026 Martin Faltus (mafaltu) -->\n"
          "# BenchHand protocol reference\n\n"
          "Protocol version proto=" PROTO_VERSION ", firmware " FW_VERSION "\n\n"
          "Generated from firmware/src/core/help_text.c by firmware/tools/gen_protocol_md.c - do not edit by hand;\n"
          "HELP on the board is authoritative. Each section is one HELP page (`HELP` alone, then `HELP <topic>`);\n"
          "`> ` marks what you send and `< ` the board's reply. Regenerate: `cmake --build <host build dir> --target\n"
          "protocol_md`.\n\n"
          "## Introduction and command list\n\n",
          f);
    rc |= put_page(f, "");
    for (uint16_t t = 0;; t++) {
        const char *topic = help_topic_name(t);
        if (topic == NULL) break;
        fprintf(f, "\n## %s\n\n", topic);
        rc |= put_page(f, topic);
    }

    if (ferror(f)) rc = 1;
    if (f != stdout && fclose(f) != 0) rc = 1;
    return rc;
}
