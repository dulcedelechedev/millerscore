"""Regression checks for the portable runtime's distribution boundary."""
# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
import re
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("package_millerscore", Path(__file__).parents[1] / "package_millerscore.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class RuntimeBoundaryTests(unittest.TestCase):
    def test_deployment_imports_cover_application_and_framework(self):
        root = Path(__file__).resolve().parents[2]
        deployed = set(re.findall(r"^import (Qt\S*)", (root / "buildscripts/packaging/Windows/DeploymentImports.qml").read_text(encoding="utf-8"), re.MULTILINE))
        required = set()
        for directory in (root / "src", root / "muse/framework"):
            for source in directory.rglob("*.qml"):
                required.update(re.findall(r"^import (Qt\S*)", source.read_text(encoding="utf-8"), re.MULTILINE))
        self.assertFalse(required - deployed, f"Missing deployed QML imports: {required - deployed}")

    def test_development_executables_and_media_cannot_enter_runtime(self):
        for value in (
            "bin/MillerScore5-diagnostic3.exe", "bin/MillerScore5.stale-pid123.exe",
            "bin/test.exe", "bin/other-app.exe", "bin/user.wav", "qml/private.json",
            "qml/crash.dmp", "qml/token.json", "qml/credentials.json",
            "qml/AUTONOMOUS_PROMPT.md", "personal-audio/session.wav",
            "testflowscripts/smoke.js", "include/model.h", "bin/MillerScore.pdb",
            "extensions/dev/example.json",
            "locale/musescore_pt_BR.qm", "locale/instruments_pt_PT.qm",
            "translations/qtbase_de.qm",
            "plugins/qmltooling/qmldbg_debugger.dll",
        ):
            with self.subTest(path=value):
                self.assertFalse(package.allowed_runtime_path(Path(value)))

    def test_required_runtime_and_license_notices_are_allowed(self):
        for value in (
            "bin/MillerScore.exe", "bin/crashpad_handler.exe", "bin/museupdater.exe",
            "bin/Qt6Core.dll", "bin/vcruntime140_1.dll", "plugins/platforms/qwindows.dll",
            "qml/QtQuick/Controls/qmldir", "sound/MS Basic.sf3",
            "licenses/Qt/LICENSE.LGPLv3", "licenses/crashpad/LICENSE",
            "locale/musescore_en_US.qm", "locale/instruments_en.qm",
            "sound/MS Basic_License.md",
            "qml/Qt5Compat/GraphicalEffects/private/qmldir",
            "qml/Qt5Compat/GraphicalEffects/private/qtgraphicaleffectsprivateplugin.dll",
        ):
            with self.subTest(path=value):
                self.assertTrue(package.allowed_runtime_path(Path(value)))

    def test_qt_graphical_effects_private_exception_is_release_only(self):
        prefix = "qml/Qt5Compat/GraphicalEffects/private/"
        for name in (
            "DropShadowBase.qml", "FastGlow.qml", "FastInnerShadow.qml",
            "GaussianDirectionalBlur.qml", "GaussianGlow.qml", "GaussianInnerShadow.qml",
            "GaussianMaskedBlur.qml", "plugins.qmltypes", "qmldir",
            "qtgraphicaleffectsprivateplugin.dll",
        ):
            with self.subTest(vendor=name):
                self.assertTrue(package.allowed_runtime_path(Path(prefix + name)))
        for value in (
            prefix + "personal.qml", prefix + "credentials.qml", prefix + "token.qml",
            prefix + "other.qml", prefix + "other.dll",
            prefix + "qtgraphicaleffectsprivateplugind.dll",
            prefix + "nested/FastGlow.qml", prefix + "internal.qml",
            "qml/AnotherModule/private/FastGlow.qml", "qml/private.json",
        ):
            with self.subTest(private=value):
                self.assertFalse(package.allowed_runtime_path(Path(value)))

    def test_license_exception_does_not_allow_private_or_sensitive_material(self):
        for value in (
            "licenses/personal_notes.md", "licenses/credentials.json", "licenses/token.txt",
            "licenses/Qt/internal.txt", "licenses/private/session.txt",
            "licenses/CODEBASE_CLEANUP_AUDIT.md", "licenses/autonomous_prompt.txt",
        ):
            with self.subTest(private=value):
                self.assertFalse(package.allowed_runtime_path(Path(value)))
        for value in (
            "licenses/crashpad/LICENSE", "licenses/Qt/LICENSE.LGPLv3",
            "licenses/Qt/third_party_notices.txt", "licenses/font/NOTICE",
            "sound/MS Basic_License.md",
        ):
            with self.subTest(legal=value):
                self.assertTrue(package.allowed_runtime_path(Path(value)))

    def test_false_release_profiles_are_rejected_before_file_copy(self):
        required = (
            "CMAKE_BUILD_TYPE:STRING=Release\nMUSE_APP_BUILD_MODE:STRING=release\n"
            "MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT:BOOL=ON\n"
            "MUSE_MODULE_DIAGNOSTICS_CRASHREPORT_URL:STRING=\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "muse_framework_config.h").write_text('#define MUSE_APP_VERSION "0.1.0"\n', encoding="utf-8")
            (root / "CPackConfig.cmake").write_text("-dMUSE_APP_RELEASE_CHANNEL=stable;", encoding="utf-8")
            for bad in (
                required.replace("=Release", "=RelWithDebInfo"),
                required.replace("=release", "=dev"),
                required.replace("CLIENT:BOOL=ON", "CLIENT:BOOL=OFF"),
                required.replace("URL:STRING=\n", "URL:STRING=https://upload.example/\n"),
            ):
                with self.subTest(cache=bad):
                    (root / "CMakeCache.txt").write_text(bad, encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, "Required release setting"):
                        package.verify_build(root)
            (root / "CMakeCache.txt").write_text(required, encoding="utf-8")
            (root / "CPackConfig.cmake").write_text("-dMUSE_APP_RELEASE_CHANNEL=dev;", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "not stable"):
                package.verify_build(root)


if __name__ == "__main__":
    unittest.main()
