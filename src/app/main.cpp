/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include <csignal>

#if defined(_MSC_VER) && !defined(MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT)
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include <QApplication>
#include <QStyleHints>
#include <QQuickWindow>
#include <QSslSocket>

#include "appfactory.h"
#include "internal/commandlineparser.h"
#include "global/iapplication.h"

#include "muse_framework_config.h"
#include "app_config.h"

#include "log.h"

// C++20 check
// #include <concepts>
// #include <type_traits>
// consteval int square(int n) { return n * n; }
// static_assert(square(5) == 25);
// ========================

#ifndef MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT
#ifdef _MSC_VER
//! Logs the crashing thread's stack. The MSVC runtime calls the signal
//! handler on the faulting thread, so the frames below the handler are the
//! crash site. Symbols resolve when the PDB of this build is reachable.
static void logCrashStack()
{
    void* frames[64];
    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    const bool symbols = SymInitialize(process, nullptr, TRUE);

    alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);

    for (USHORT i = 0; i < count; ++i) {
        const DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);

        char moduleName[MAX_PATH] = "?";
        HMODULE module = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(frames[i]), &module)) {
            GetModuleFileNameA(module, moduleName, MAX_PATH);
        }
        const char* moduleBase = strrchr(moduleName, '\\');
        moduleBase = moduleBase ? moduleBase + 1 : moduleName;

        const char* name = "?";
        DWORD64 displacement = 0;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        if (symbols && SymFromAddr(process, address, &displacement, symbol)) {
            name = symbol->Name;
        }

        IMAGEHLP_LINE64 line = {};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (symbols && SymGetLineFromAddr64(process, address, &lineDisplacement, &line)) {
            LOGE() << "  #" << i << " " << moduleBase << "!" << name << " (" << line.FileName << ":" << line.LineNumber << ")";
        } else {
            LOGE() << "  #" << i << " " << moduleBase << "!" << name << "+0x" << std::hex << displacement << std::dec;
        }
    }
}

#endif

static void crashCallback(int signum)
{
    const char* signame = "UNKNOWN SIGNAME";
    const char* sigdescript = "";
    switch (signum) {
    case SIGILL:
        signame = "SIGILL";
        sigdescript = "Illegal Instruction";
        break;
    case SIGSEGV:
        signame = "SIGSEGV";
        sigdescript =  "Invalid memory reference";
        break;
    }
    LOGE() << "Oops! Application crashed with signal: [" << signum << "] " << signame << "-" << sigdescript;
#ifdef _MSC_VER
    logCrashStack();
#endif
    exit(EXIT_FAILURE);
}

#endif

static void app_init_qrc()
{
    Q_INIT_RESOURCE(app);

#ifdef Q_OS_WIN
    Q_INIT_RESOURCE(app_win);
#endif
}

int main(int argc, char** argv)
{
#ifndef MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT
    signal(SIGSEGV, crashCallback);
    signal(SIGILL, crashCallback);
    signal(SIGFPE, crashCallback);
#endif

    // ====================================================
    // Setup global Qt application variables
    // ====================================================

    app_init_qrc();

    qputenv("QT_STYLE_OVERRIDE", "Fusion");
    qputenv("QML_DISABLE_DISK_CACHE", "true");

    // HACK: Workaround for crash #28840. This disables the incremental GC
    if (!qEnvironmentVariableIsSet("MU_QV4_GC_TIMELIMIT")) {
        qputenv("QV4_GC_TIMELIMIT", "0");
    }

    if (!qEnvironmentVariableIsSet("QT_QUICK_FLICKABLE_WHEEL_DECELERATION")) {
        qputenv("QT_QUICK_FLICKABLE_WHEEL_DECELERATION", "5000");
    }

#ifdef Q_OS_LINUX
    if (qEnvironmentVariable("MU_QT_QPA_PLATFORM") != "offscreen") {
        qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
    }

    //! NOTE Forced X11, with Wayland there are a number of problems now
    if (qEnvironmentVariable("MU_QT_QPA_PLATFORM") == "") {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif

#ifdef Q_OS_WIN
    // NOTE: There are some problems with rendering the application window on some integrated graphics processors
    //       see https://github.com/musescore/MuseScore/issues/8270
    if (!qEnvironmentVariableIsSet("QT_OPENGL_BUGLIST")) {
        qputenv("QT_OPENGL_BUGLIST", ":/resources/win_opengl_buglist.json");
    }
#endif

    QGuiApplication::styleHints()->setMousePressAndHoldInterval(250);

#ifdef MUSE_APP_UNSTABLE
    QCoreApplication::setApplicationName(MUSE_APP_NAME_MACHINE_READABLE "Development");
#else
    QCoreApplication::setApplicationName(MUSE_APP_NAME_MACHINE_READABLE);
#endif
    QCoreApplication::setOrganizationName("MillerScore");
    QCoreApplication::setOrganizationDomain("github.com/dulcedelechedev/millerscore");
    QCoreApplication::setApplicationVersion(MUSE_APP_VERSION);

#if !defined(Q_OS_WIN) && !defined(Q_OS_DARWIN) && !defined(Q_OS_WASM)
    // Any OS that uses Freedesktop.org Desktop Entry Specification (e.g. Linux, BSD)
#ifndef MUSE_APP_INSTALL_SUFFIX
#define MUSE_APP_INSTALL_SUFFIX ""
#endif
    QGuiApplication::setDesktopFileName("org.musescore.MuseScore" MUSE_APP_INSTALL_SUFFIX);
#endif

    using namespace muse;
    using namespace mu::app;

    auto fixSslBackend = []() {
#ifdef Q_OS_WIN
        // NOTE: Force schannel backend. Qt prefers OpenSSL when qopensslbackend.dll
        //       is present, which can crash on ABI mismatch with bundled OpenSSL.
        //       see https://github.com/musescore/MuseScore/issues/33401
        QSslSocket::setActiveBackend("schannel");
#endif
    };

    // ====================================================
    // Parse command line options
    // ====================================================
#ifdef MUE_ENABLE_CONSOLEAPP
    CommandLineParser commandLineParser;
    commandLineParser.init();
    commandLineParser.parse(argc, argv);

    IApplication::RunMode runMode = commandLineParser.runMode();
    QCoreApplication* qapp = nullptr;

    if (runMode == IApplication::RunMode::AudioPluginRegistration) {
        qapp = new QCoreApplication(argc, argv);
    } else {
        qapp = new QApplication(argc, argv);
    }

    fixSslBackend();

    commandLineParser.processBuiltinArgs(*qapp);
    std::shared_ptr<MuseScoreCmdOptions> opt = commandLineParser.options();

#else
    QCoreApplication* qapp = new QApplication(argc, argv);

    fixSslBackend();

    std::shared_ptr<MuseScoreCmdOptions> opt = std::make_shared<MuseScoreCmdOptions>();
    opt->runMode = IApplication::RunMode::GuiApp;
#endif

    // ====================================================
    // Setup application
    // ====================================================

    //! NOTE: We immediately launch the application's event loop
    // to be able to show a splash screen (on Linux, splash screen won't show without event loop).
    // All subsequent initialization steps will be executed as events in the event loop.

    std::shared_ptr<muse::IApplication> app;
    QMetaObject::invokeMethod(qapp, [qapp, &app, &opt]() {
        AppFactory f;
        app = f.newApp(opt);
        IF_ASSERT_FAILED(app) {
            return;
        }
        app->showSplash();
        QMetaObject::invokeMethod(qapp, [qapp, &app]() {
            app->setup();
            QMetaObject::invokeMethod(qapp, [&app]() {
                app->setupNewContext();

                LOGI() << QString("SSL Info: supported: %1, build: %2, runtime: %3, active backend: %4, available backends: %5")
                    .arg(QSslSocket::supportsSsl())
                    .arg(QSslSocket::sslLibraryBuildVersionString())
                    .arg(QSslSocket::sslLibraryVersionString())
                    .arg(QSslSocket::activeBackend())
                    .arg(QSslSocket::availableBackends().join(", "));
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);

    // ====================================================
    // Run main loop
    // ====================================================
    int code = qapp->exec();

    // ====================================================
    // Quit
    // ====================================================

    if (app) {
        app->finish();
    }

    delete qapp;

    LOGI() << "Goodbye!! code: " << code;
    return code;
}
