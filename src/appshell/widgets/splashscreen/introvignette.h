/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <memory>
#include <string>

namespace mu::appshell {
//! The MillerScore intro: pre-rendered frames (share/intro/vignette.frames) played in sync with the
//! jingle (share/intro/jingle.wav), both embedded in the executable as Windows resources.
//! It plays on its own thread in its own native window, above everything, so it never holds the
//! app's startup up: the app keeps loading underneath and the intro fades out over it.
//! Any click or key skips it. Windows only; elsewhere shouldPlay() is false and the static splash
//! screen shows as before. The frames are regenerated with buildscripts/tools/intro_vignette.
class IntroVignette
{
public:
    //! false when not on Windows, when turned off (--no-intro, MILLERSCORE_NO_INTRO=1, --autodrive,
    //! MILLERSCORE_AUTODRIVE=1), when Windows animations are turned off, or when the resources are missing
    //! `why`, if given, gets the reason when it is false (the log is not up yet when this is asked)
    static bool shouldPlay(std::string* why = nullptr);

    IntroVignette();
    ~IntroVignette(); //! stops it if it is still playing and waits for its thread

    //! false if it could not start (the caller then shows the static splash screen)
    bool start();
    void stop();

    //! why start() failed, for the log
    const std::string& error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
