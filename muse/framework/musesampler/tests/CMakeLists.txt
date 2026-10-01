# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies

set(MODULE_TEST muse_musesampler_tests)
set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/libhandler_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../internal/musesamplerconfiguration.cpp
)
set(MODULE_TEST_INCLUDE ${CMAKE_CURRENT_LIST_DIR}/..)
include(SetupGTest)
