# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Martin Faltus (mafaltu)
# CTest staleness check for docs/PROTOCOL.md (FW-38): runs the generator and compares its output byte for byte
# with the committed file. Called as: cmake -DGEN=<gen_protocol_md> -DDOC=<docs/PROTOCOL.md> -P <this file>
execute_process(COMMAND ${GEN} OUTPUT_VARIABLE generated RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gen_protocol_md failed (exit ${rc})")
endif()
if(NOT EXISTS ${DOC})
  message(FATAL_ERROR "${DOC} is missing. Regenerate it: cmake --build <host build dir> --target protocol_md")
endif()
file(READ ${DOC} committed)
if(NOT generated STREQUAL committed)
  message(FATAL_ERROR "${DOC} is stale: it differs from the HELP pages in src/core/help_text.c. Regenerate it "
                      "with `cmake --build <host build dir> --target protocol_md` (host build, FW_TARGET=OFF) and "
                      "commit the result; never edit PROTOCOL.md by hand.")
endif()
message(STATUS "${DOC} matches the generator output")
