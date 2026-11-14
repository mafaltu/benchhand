/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "core/errors.h"
#include "core/help_text.h"
#include "minitest.h"

static bool starts_with(const char *s, const char *p)
{
    return strncmp(s, p, strlen(p)) == 0;
}

static bool lower_word(char c)
{
    return (c >= 'a' && c <= 'z') || c == '_' || (c >= '0' && c <= '9');
}

/* ^ERR [0-9]+ [a-z_]+: .+$ and <reason> is the reason of <code>. */
static bool err_example_ok(const char *s)
{
    if (!starts_with(s, "ERR ")) return false;
    s += 4;
    int code = 0;
    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') code = code * 10 + (*s++ - '0');
    if (*s++ != ' ') return false;
    const char *r = s;
    if (!((*s >= 'a' && *s <= 'z') || *s == '_')) return false;
    while ((*s >= 'a' && *s <= 'z') || *s == '_') s++;
    size_t rl = (size_t)(s - r);
    if (*s++ != ':' || *s++ != ' ' || *s == '\0') return false;
    if (code < 1 || code > 11) return false;
    const char *want = err_reason((err_code_t)code);
    return strlen(want) == rl && strncmp(want, r, rl) == 0;
}

/* ^OK( [a-z_]+=[^ ]+)*$ */
static bool ok_example_ok(const char *s)
{
    if (!starts_with(s, "OK")) return false;
    s += 2;
    while (*s == ' ') {
        s++;
        const char *k = s;
        while (lower_word(*s)) s++;
        if (s == k || *s++ != '=') return false;
        const char *v = s;
        while (*s != '\0' && *s != ' ') s++;
        if (s == v) return false;
    }
    return *s == '\0';
}

static const char *const keywords[] = {"HELP", "ID",  "STATUS", "SET", "PULSE",  "MAX_ON",    "INPUT",
                                       "WAIT", "USB", "SEQ",    "REBOOT", "BOOTLOADER", "ERRORS"};

TEST(test_help_grammar)
{
    /* topic "" (intro) + every named topic */
    for (int t = -1;; t++) {
        const char *topic = t < 0 ? "" : help_topic_name((uint16_t)t);
        if (topic == NULL) break;
        uint16_t n = 0, err_examples = 0;
        for (const char *l; (l = help_line(topic, n)) != NULL; n++) {
            if (!starts_with(l, "# ")) printf("topic '%s' line %u: %s\n", topic, n, l);
            CHECK(starts_with(l, "# "));
            CHECK(strlen(l) <= 500u); /* fits the 512-byte reply staging buffer with CRLF */
            for (const char *c = l; *c; c++) CHECK(*c >= 0x20 && *c <= 0x7E);
            if (starts_with(l, "# < ")) {
                const char *rep = l + 4;
                bool ok = starts_with(rep, "ERR") ? err_example_ok(rep) : ok_example_ok(rep);
                if (!ok) printf("topic '%s' bad example: %s\n", topic, rep);
                CHECK(ok);
                err_examples += starts_with(rep, "ERR");
            }
        }
        CHECK(n > 3);
        /* C8: every command can fail (at least with a bad / extra argument or busy): each page has an ERR example */
        if (t >= 0 && err_examples == 0) printf("topic '%s' has no ERR example\n", topic);
        if (t >= 0) CHECK(err_examples > 0);
    }

    /* every command keyword has a page; lookups are case-insensitive */
    for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
        CHECK(help_topic_exists(keywords[i]));
        CHECK(help_line(keywords[i], 0) != NULL);
    }
    CHECK(help_topic_exists("max_on"));
    CHECK(help_topic_exists(""));
    CHECK(!help_topic_exists("FOO"));
    CHECK(help_line("FOO", 0) == NULL);

    /* intro: REPO_URL line, "send HELP", the FW-23 / M14 lines */
    bool project = false, send_help = false, stty = false, serial = false, ext = false, tabs = false;
    for (uint16_t i = 0;; i++) {
        const char *l = help_line("", i);
        if (l == NULL) break;
        project = project || (starts_with(l, "# Project: ") && strstr(l, "https://github.com/mafaltu") != NULL);
        send_help = send_help || strstr(l, "send HELP") != NULL;
        stty = stty || strcmp(l, "# Before use: stty -F <port> raw -echo (prevents the tty echoing replies back).") == 0;
        serial = serial || strcmp(l, "# Serial settings (baud, DTR) are ignored.") == 0;
        ext = ext || strcmp(l, "# Watch out: ext=1 means a voltage is present on the USB-A output while it is off.") == 0;
        tabs = tabs || strstr(l, "tab") != NULL;
    }
    CHECK(project);
    CHECK(send_help);
    CHECK(stty);
    CHECK(serial);
    CHECK(ext);
    CHECK(tabs);
}

TEST(test_help_status_documents_every_field)
{
    static const char *const fields[] = {"rly", "req", "btn", "presses", "left", "in", "usb", "usb_req",
                                         "data_req", "ext", "usb_btn", "usb_presses", "fault", "seq", "ready",
                                         "boot", "reset", "uptime", "suspends", "lp_err", "optr_err"};
    for (size_t f = 0; f < sizeof fields / sizeof fields[0]; f++) {
        char pat[32];
        snprintf(pat, sizeof pat, "#   %s ", fields[f]);
        bool found = false;
        for (uint16_t i = 0; help_line("STATUS", i) != NULL; i++) {
            found = found || starts_with(help_line("STATUS", i), pat);
        }
        if (!found) printf("STATUS field not documented: %s\n", fields[f]);
        CHECK(found);
    }
}

static bool page_has(const char *topic, const char *needle)
{
    for (uint16_t i = 0; help_line(topic, i) != NULL; i++) {
        if (strstr(help_line(topic, i), needle) != NULL) return true;
    }
    return false;
}

/* Final review items Cx-I2, Cl-I1, Cl-m4, Cl-m5. */
TEST(test_help_final_review_content)
{
    /* Cx-I2: USB ON|OFF is the power request; data_req persists; example of the sequence */
    CHECK(!page_has("USB", "(power and data)"));
    CHECK(!page_has("", "USB-A output power + data"));
    CHECK(page_has("USB", "independent"));
    CHECK(page_has("USB", "# > USB OFF"));
    CHECK(page_has("USB", "# > USB ON"));
    CHECK(page_has("USB", "usb=power data_req=0"));
    /* Cl-I1: autosuspend */
    CHECK(page_has("", "autosuspend"));
    CHECK(page_has("", "power/control=on"));
    /* Cl-m4: 10 ms switch-on stagger and 1 ms resolution */
    CHECK(page_has("SET", "one per 10 ms"));
    CHECK(page_has("PULSE", "one per 10 ms"));
    CHECK(page_has("PULSE", "1 ms resolution"));
    CHECK(page_has("SEQ", "one per 10 ms"));
    /* Cl-m5: flock, uptime pauses in host sleep */
    CHECK(page_has("", "flock"));
    CHECK(page_has("STATUS", "not count time in host sleep"));
}

int main(void)
{
    RUN(test_help_grammar);
    RUN(test_help_status_documents_every_field);
    RUN(test_help_final_review_content);
    return minitest_summary();
}
