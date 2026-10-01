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

#include "introvignette.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <QImage>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

#include <windows.h>
#include <mmsystem.h>
#include <dwmapi.h>
#endif

using namespace mu::appshell;

#ifdef Q_OS_WIN

namespace {
// see share/icons/windows_icons.rc
constexpr const wchar_t* FRAMES_RESOURCE = L"MS_INTRO_FRAMES";
constexpr const wchar_t* JINGLE_RESOURCE = L"MS_INTRO_JINGLE";
constexpr const wchar_t* WINDOW_CLASS = L"MillerScoreIntroVignette";

constexpr double CLOSE_FADE_SEC = 0.15;  // the frames already fade to black; this takes the window away
constexpr double SKIP_FADE_SEC = 0.2;
constexpr double SKIP_SOUND_MARGIN_SEC = 0.15; // sound the device may already have taken, left as it is
constexpr double SKIP_SOUND_FADE_SEC = 0.15;
constexpr double MAX_SOUND_LAG_SEC = 0.5;    // the pictures never wait longer than this for a stalled sound

constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE_ = 33; // not in older SDKs
constexpr DWORD DWMWCP_ROUND_ = 2;

struct Blob {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

Blob resource(const wchar_t* name)
{
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(module, name, MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!res) {
        return {};
    }
    HGLOBAL handle = LoadResource(module, res);
    if (!handle) {
        return {};
    }
    return { static_cast<const uint8_t*>(LockResource(handle)), SizeofResource(module, res) };
}

uint32_t readU32(const uint8_t* p)
{
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v)); // little endian, as Windows is
    return v;
}

//! vignette.frames, written by buildscripts/tools/intro_vignette/pack.py
struct Frames {
    static constexpr size_t HEADER_SIZE = 36;

    uint32_t count = 0;
    uint32_t fps = 0;
    uint32_t logicalWidth = 0;
    uint32_t logicalHeight = 0;
    uint32_t endMs = 0;
    const uint8_t* base = nullptr;

    bool load(const Blob& blob)
    {
        if (blob.size < HEADER_SIZE || std::memcmp(blob.data, "MSVF", 4) != 0 || readU32(blob.data + 4) != 1) {
            return false;
        }
        count = readU32(blob.data + 8);
        fps = readU32(blob.data + 12);
        logicalWidth = readU32(blob.data + 24);
        logicalHeight = readU32(blob.data + 28);
        endMs = readU32(blob.data + 32);
        if (count == 0 || fps == 0 || logicalWidth == 0 || logicalHeight == 0 || endMs == 0) {
            return false;
        }
        const size_t tableEnd = HEADER_SIZE + 4 * (size_t(count) + 1);
        if (tableEnd > blob.size || readU32(blob.data + HEADER_SIZE + 4 * size_t(count)) > blob.size) {
            return false;
        }
        base = blob.data;
        return true;
    }

    QImage frame(uint32_t index) const
    {
        const uint32_t from = readU32(base + HEADER_SIZE + 4 * size_t(index));
        const uint32_t to = readU32(base + HEADER_SIZE + 4 * (size_t(index) + 1));
        if (from >= to) {
            return QImage();
        }
        return QImage::fromData(base + from, int(to - from), "JPG").convertToFormat(QImage::Format_RGB32);
    }
};

//! jingle.wav: 16-bit PCM; copied, so a skip can fade out what has not been played yet
struct Wave {
    WAVEFORMATEX format = {};
    std::vector<uint8_t> pcm;

    bool load(const Blob& blob)
    {
        if (blob.size < 12 || std::memcmp(blob.data, "RIFF", 4) != 0 || std::memcmp(blob.data + 8, "WAVE", 4) != 0) {
            return false;
        }
        bool hasFormat = false;
        size_t pos = 12;
        while (pos + 8 <= blob.size) {
            const uint8_t* chunk = blob.data + pos;
            const size_t len = std::min<size_t>(readU32(chunk + 4), blob.size - pos - 8);
            if (std::memcmp(chunk, "fmt ", 4) == 0 && len >= 16) {
                std::memcpy(&format, chunk + 8, 16);
                format.cbSize = 0;
                hasFormat = true;
            } else if (std::memcmp(chunk, "data", 4) == 0) {
                pcm.assign(chunk + 8, chunk + 8 + len);
            }
            pos += 8 + len + (len & 1);
        }
        return hasFormat && format.wFormatTag == WAVE_FORMAT_PCM && format.wBitsPerSample == 16
               && format.nBlockAlign > 0 && format.nSamplesPerSec > 0 && !pcm.empty();
    }
};

double secondsSince(std::chrono::steady_clock::time_point from)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - from).count();
}
}

struct IntroVignette::Impl
{
    Frames frames;
    Wave wave;
    bool hasSound = false;
    std::string error;
    DWORD windowError = 0;

    std::thread thread;
    std::atomic<bool> stopRequested { false };
    bool skipRequested = false; // set by the window, on the vignette's thread

    HWND hwnd = nullptr;
    QImage current;

    HWAVEOUT waveOut = nullptr;
    WAVEHDR header = {};
    std::chrono::steady_clock::time_point startedAt;

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_CLOSE:
            if (self) {
                self->skipRequested = true;
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (self) {
                self->draw(dc);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool createWindow()
    {
        HINSTANCE instance = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Impl::windowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512) /* IDC_ARROW */);
        wc.lpszClassName = WINDOW_CLASS;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            windowError = GetLastError();
            return false;
        }

        // on the monitor the pointer is on, which is where the app was just started from
        POINT cursor = {};
        GetCursorPos(&cursor);
        MONITORINFO monitor = {};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
        const RECT area = monitor.rcWork;

        hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, WINDOW_CLASS, L"MillerScore", WS_POPUP,
                               area.left, area.top, 1, 1, nullptr, nullptr, instance, nullptr);
        if (!hwnd) {
            windowError = GetLastError();
            return false;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        const UINT dpi = std::max<UINT>(96, GetDpiForWindow(hwnd));
        const int areaWidth = area.right - area.left;
        const int areaHeight = area.bottom - area.top;
        double width = MulDiv(int(frames.logicalWidth), int(dpi), 96);
        double height = MulDiv(int(frames.logicalHeight), int(dpi), 96);
        const double fit = std::min({ 1.0, areaWidth * 0.9 / width, areaHeight * 0.9 / height });
        width *= fit;
        height *= fit;

        SetWindowPos(hwnd, HWND_TOPMOST, area.left + (areaWidth - int(width)) / 2, area.top + (areaHeight - int(height)) / 2,
                     int(width), int(height), SWP_NOACTIVATE);

        const DWORD corners = DWMWCP_ROUND_;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_, &corners, sizeof(corners));
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        return true;
    }

    void draw(HDC dc)
    {
        RECT rect;
        GetClientRect(hwnd, &rect);
        if (current.isNull()) {
            HBRUSH brush = CreateSolidBrush(RGB(5, 4, 3));
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
            return;
        }

        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = current.width();
        info.bmiHeader.biHeight = -current.height(); // top-down, like QImage
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, 0, 0, rect.right, rect.bottom, 0, 0, current.width(), current.height(),
                      current.constBits(), &info, DIB_RGB_COLORS, SRCCOPY);
    }

    void show(uint32_t index)
    {
        QImage image = frames.frame(index);
        if (image.isNull()) {
            return;
        }
        current = std::move(image);
        HDC dc = GetDC(hwnd);
        draw(dc);
        ReleaseDC(hwnd, dc);
    }

    //! opened paused, so the first frame and the sound can start together
    bool openAudio()
    {
        if (!hasSound || waveOutOpen(&waveOut, WAVE_MAPPER, &wave.format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            waveOut = nullptr;
            return false;
        }
        header.lpData = reinterpret_cast<LPSTR>(wave.pcm.data());
        header.dwBufferLength = DWORD(wave.pcm.size());
        if (waveOutPrepareHeader(waveOut, &header, sizeof(header)) != MMSYSERR_NOERROR) {
            waveOutClose(waveOut);
            waveOut = nullptr;
            return false;
        }
        waveOutPause(waveOut);
        waveOutWrite(waveOut, &header, sizeof(header));
        return true;
    }

    void closeAudio()
    {
        if (!waveOut) {
            return;
        }
        waveOutReset(waveOut);
        waveOutUnprepareHeader(waveOut, &header, sizeof(header));
        waveOutClose(waveOut);
        waveOut = nullptr;
    }

    //! seconds of the jingle heard so far; the vignette's clock
    bool soundPosition(double& seconds) const
    {
        MMTIME time = {};
        time.wType = TIME_SAMPLES;
        if (!waveOut || waveOutGetPosition(waveOut, &time, sizeof(time)) != MMSYSERR_NOERROR) {
            return false;
        }
        if (time.wType == TIME_SAMPLES) {
            seconds = double(time.u.sample) / wave.format.nSamplesPerSec;
            return true;
        }
        if (time.wType == TIME_BYTES) {
            seconds = double(time.u.cb) / wave.format.nAvgBytesPerSec;
            return true;
        }
        return false;
    }

    //! fades out the part of the jingle the device has not taken yet (waveOutSetVolume would change
    //! the app's volume in the Windows mixer, so the samples are faded instead)
    void fadeOutSound(double at)
    {
        const size_t block = wave.format.nBlockAlign;
        const size_t channels = wave.format.nChannels;
        const size_t frameCount = wave.pcm.size() / block;
        const size_t from = std::min(frameCount, size_t((at + SKIP_SOUND_MARGIN_SEC) * wave.format.nSamplesPerSec));
        const size_t length = size_t(SKIP_SOUND_FADE_SEC * wave.format.nSamplesPerSec);
        for (size_t f = from; f < frameCount; ++f) {
            const double gain = f < from + length ? 1.0 - double(f - from) / length : 0.0;
            for (size_t c = 0; c < channels; ++c) {
                int16_t sample;
                uint8_t* p = wave.pcm.data() + f * block + c * 2;
                std::memcpy(&sample, p, 2);
                sample = int16_t(sample * gain);
                std::memcpy(p, &sample, 2);
            }
        }
    }

    void pumpMessages()
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    void run(std::promise<bool>& started)
    {
        if (!createWindow()) {
            started.set_value(false);
            return;
        }
        started.set_value(true);

        const bool sound = openAudio();
        show(0);
        ShowWindow(hwnd, SW_SHOWNORMAL); // activated, so a key press can skip it
        UpdateWindow(hwnd);
        startedAt = std::chrono::steady_clock::now();
        if (sound) {
            waveOutRestart(waveOut);
        }

        const double end = frames.endMs / 1000.0;
        double fadeStart = -1;
        double fadeLength = 0;
        double soundStopAt = -1; // after a skip: when the faded jingle is silent
        uint32_t shown = 0;

        while (!stopRequested) {
            pumpMessages();

            const double wall = secondsSince(startedAt);
            double t = wall;
            if (sound && soundPosition(t)) {
                t = std::max(t, wall - MAX_SOUND_LAG_SEC);
            }

            if (skipRequested && fadeStart < 0) {
                fadeStart = wall;
                fadeLength = SKIP_FADE_SEC;
                if (sound) {
                    fadeOutSound(t);
                    soundStopAt = wall + SKIP_SOUND_MARGIN_SEC + SKIP_SOUND_FADE_SEC + 0.05;
                }
            } else if (fadeStart < 0 && t >= end) {
                fadeStart = wall;
                fadeLength = CLOSE_FADE_SEC;
            }

            if (fadeStart >= 0) {
                const double k = (wall - fadeStart) / fadeLength;
                if (k >= 1) {
                    break;
                }
                SetLayeredWindowAttributes(hwnd, 0, BYTE(255 * (1 - k)), LWA_ALPHA);
            }

            if (!skipRequested) {
                const uint32_t index = std::min(frames.count - 1, uint32_t(std::max(0.0, t) * frames.fps));
                if (index != shown) {
                    show(index);
                    shown = index;
                }
            }

            MsgWaitForMultipleObjects(0, nullptr, FALSE, 4, QS_ALLINPUT);
        }

        DestroyWindow(hwnd);
        hwnd = nullptr;
        current = QImage();

        // the jingle rings out under the app
        while (sound && !stopRequested && !(header.dwFlags & WHDR_DONE)
               && (soundStopAt < 0 || secondsSince(startedAt) < soundStopAt)) {
            Sleep(20);
        }
        closeAudio();
    }
};

bool IntroVignette::shouldPlay(std::string* why)
{
    auto no = [why](const char* reason) {
        if (why) {
            *why = reason;
        }
        return false;
    };

    if (qEnvironmentVariableIntValue("MILLERSCORE_AUTODRIVE") != 0) {
        return no("autodrive");
    }
    if (qEnvironmentVariableIntValue("MILLERSCORE_NO_INTRO") != 0) {
        return no("turned off (--no-intro)");
    }

    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) && !animations) {
        return no("Windows animations are off");
    }

    if (!resource(FRAMES_RESOURCE).data) {
        return no("no frames in the executable");
    }
    return true;
}

IntroVignette::IntroVignette()
    : m_impl(std::make_unique<Impl>())
{
}

IntroVignette::~IntroVignette()
{
    stop();
}

bool IntroVignette::start()
{
    if (m_impl->thread.joinable()) {
        return true;
    }

    if (!m_impl->frames.load(resource(FRAMES_RESOURCE))) {
        m_impl->error = "the frames could not be read";
        return false;
    }
    m_impl->hasSound = m_impl->wave.load(resource(JINGLE_RESOURCE));
    if (!m_impl->hasSound) {
        m_impl->error = "no jingle, playing silently";
    }

    std::promise<bool> started;
    std::future<bool> result = started.get_future();
    Impl* impl = m_impl.get();
    m_impl->thread = std::thread([impl, &started]() { impl->run(started); });
    if (!result.get()) {
        m_impl->thread.join();
        m_impl->error = "could not create its window (error " + std::to_string(m_impl->windowError) + ")";
        return false;
    }
    return true;
}

const std::string& IntroVignette::error() const
{
    return m_impl->error;
}

void IntroVignette::stop()
{
    m_impl->stopRequested = true;
    if (m_impl->thread.joinable()) {
        m_impl->thread.join();
    }
}

#else

struct IntroVignette::Impl {
    std::string error;
};

bool IntroVignette::shouldPlay(std::string* why)
{
    if (why) {
        *why = "only on Windows";
    }
    return false;
}

IntroVignette::IntroVignette()
    : m_impl(std::make_unique<Impl>())
{
}

IntroVignette::~IntroVignette() = default;

bool IntroVignette::start()
{
    return false;
}

void IntroVignette::stop()
{
}

const std::string& IntroVignette::error() const
{
    return m_impl->error;
}

#endif
