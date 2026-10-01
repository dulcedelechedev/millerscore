# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2025 MuseScore Limited and others
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

set(MODULE_TEST muse_audio_tests)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/rpcpacker_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/alignbuffer_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/audioresourcetypes_tests.cpp
)

include(SetupGTest)

# The engine tests link the audio engine, whose code sees audiotypes.h unpacked. The
# tests above include it under #pragma pack(1), so the two cannot share an executable:
# the differing layouts of the same types crash at static initialization (ODR).
set(MODULE_TEST muse_audio_engine_tests)

set(MODULE_TEST_SRC
    ${CMAKE_CURRENT_LIST_DIR}/fxchain_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/latency_tests.cpp
    ${CMAKE_CURRENT_LIST_DIR}/fluidsequencer_tests.cpp
)

# The engine includes its headers relative to framework/audio.
set(MODULE_TEST_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/..
)

set(MODULE_TEST_LINK
    muse_audio_engine
)

include(SetupGTest)
