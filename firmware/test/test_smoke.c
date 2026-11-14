/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 Martin Faltus (mafaltu) */
#include "minitest.h"
#include "core/timebase.h"

TEST(ms_reached_basic)
{
    CHECK(ms_reached(5, 5));
    CHECK(!ms_reached(4, 5));
}

TEST(ms_reached_wrap)
{
    CHECK(ms_reached(2, 0xFFFFFFF0u));
    CHECK(!ms_reached(0xFFFFFFF0u, 2));
}

int main(void)
{
    RUN(ms_reached_basic);
    RUN(ms_reached_wrap);
    return minitest_summary();
}
