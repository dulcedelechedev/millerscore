#!/usr/bin/env python3
"""Assemble a Windows ZIP from a verified build and a controlled runtime allowlist."""
# SPDX-License-Identifier: GPL-3.0-only
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESOURCE_DIRS = {
    "licenses", "locale", "plugins", "qml", "sound", "styles", "tables",
    "templates", "translations", "wallpapers", "workspaces",
}
BINARIES = {
    "millerscore.exe", "museupdater.exe", "crashpad_handler.exe",
    "concrt140.dll", "d3dcompiler_47.dll", "dxcompiler.dll", "dxil.dll",
    "fdk-aac.dll", "flac.dll", "flac++.dll", "freetype.dll", "harfbuzz.dll",
    "icuuc.dll", "libpng16.dll", "ogg.dll", "opengl32sw.dll", "opus.dll",
    "sndfile.dll", "vorbis.dll", "vorbisenc.dll", "vorbisfile.dll", "zlib1.dll",
    "lame_enc.dll", "libmp3lame.dll", "qt.conf",
}
FORBIDDEN_NAME = re.compile(
    r"(^|[/_.-])(diagnostic\d*|stale|backup|preclean|inflated|autonomous|handoff|audit|personal|private|internal|test|credentials?|tokens?|crash)([/_.-]|$)",
    re.IGNORECASE,
)
QT_GRAPHICAL_EFFECTS_PRIVATE_FILES = {
    "dropshadowbase.qml", "fastglow.qml", "fastinnershadow.qml",
    "gaussiandirectionalblur.qml", "gaussianglow.qml", "gaussianinnershadow.qml",
    "gaussianmaskedblur.qml", "plugins.qmltypes", "qmldir",
    "qtgraphicaleffectsprivateplugin.dll",
}


def allowed_runtime_path(relative: Path) -> bool:
    """Fail closed for executables and development output; preserve third-party notices."""
    name = relative.name.lower()
    parts = tuple(part.lower() for part in relative.parts)
    vendor_private = (
        len(parts) == 5 and parts[:4] == ("qml", "qt5compat", "graphicaleffects", "private")
        and name in QT_GRAPHICAL_EFFECTS_PRIVATE_FILES
    )
    # Only this exact vendor directory segment is exempt. Filename and all
    # other segment checks also apply before the broad license-notice rule.
    for index, part in enumerate(parts):
        if vendor_private and index == 3:
            continue
        if FORBIDDEN_NAME.search(part):
            return False
    if relative.as_posix() == "sound/MS Basic_License.md":
        return True
    if relative.parts[0] == "licenses":
        return relative.suffix.lower() not in {".exe", ".dll", ".pdb", ".dmp", ".obj", ".lib"}
    if vendor_private:
        return True
    if relative.parts[:2] == ("plugins", "qmltooling"):
        return False
    if relative.suffix.lower() in {".pdb", ".dmp", ".obj", ".lib", ".log", ".ps1", ".bat", ".cpp", ".h", ".md"}:
        return False
    if relative.parts[0] in {"locale", "translations"} and relative.suffix.lower() == ".qm":
        # This distribution uses English product text; retain optional language metadata.
        return bool(re.search(r"(?:^|_)en(?:_[a-z]{2})?\.qm$", name))
    if relative.parts[0] == "bin":
        return len(relative.parts) == 2 and (
            name in BINARIES or bool(re.fullmatch(r"qt6[a-z0-9_]+\.dll|(?:msvcp|vcruntime)140[a-z0-9_]*\.dll", name))
        )
    if len(relative.parts) > 1 and relative.parts[0] in RESOURCE_DIRS:
        return relative.suffix.lower() != ".exe"
    return False


def metadata(executable: Path) -> dict:
    # Pass paths as arguments, never as interpolated executable shell text.
    code = "param([string]$Executable)\n[Diagnostics.FileVersionInfo]::GetVersionInfo($Executable) | Select-Object ProductName,CompanyName,FileVersion,ProductVersion | ConvertTo-Json -Compress\n"
    with tempfile.TemporaryDirectory(prefix="millerscore-version-") as temporary:
        script = Path(temporary) / "read-version.ps1"
        script.write_text(code, encoding="utf-8")
        command = ["pwsh", "-NoProfile", "-File", str(script), str(executable)]
        result = subprocess.run(command, check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def verify_build(build: Path) -> str:
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    config = (build / "muse_framework_config.h").read_text(encoding="utf-8")
    packaging = (build / "CPackConfig.cmake").read_text(encoding="utf-8")
    if "-dMUSE_APP_RELEASE_CHANNEL=stable;" not in packaging:
        raise ValueError("The generated release channel is not stable.")
    for expression in (
        r"^CMAKE_BUILD_TYPE:[^=]+=Release$", r"^MUSE_APP_BUILD_MODE:[^=]+=release$",
        r"^MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT:[^=]+=ON$",
        r"^MUSE_MODULE_DIAGNOSTICS_CRASHREPORT_URL:[^=]+=$",
    ):
        if not re.search(expression, cache, re.MULTILINE):
            raise ValueError(f"Required release setting is absent: {expression}")
    if re.search(r"^#define MUSE_APP_UNSTABLE\b", config, re.MULTILINE):
        raise ValueError("The build is a development profile.")
    version = re.search(r'^#define MUSE_APP_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"$', config, re.MULTILINE)
    if not version:
        raise ValueError("Semantic MillerScore version is missing.")
    installed = build / "install"
    for relative in ("MillerScore.exe", "bin/MillerScore.exe", "bin/crashpad_handler.exe", "bin/Qt6Core.dll", "bin/qt.conf", "plugins/platforms/qwindows.dll", "sound/MS Basic.sf3"):
        if not (installed / relative).is_file():
            raise ValueError(f"Required runtime file is missing: {relative}")
    for executable in (installed / "MillerScore.exe", installed / "bin/MillerScore.exe"):
        info = metadata(executable)
        if info["ProductName"] != "MillerScore" or info["CompanyName"] != "DulceDeLecheDEV" or info["ProductVersion"] != version[1]:
            raise ValueError(f"Incorrect executable version metadata: {info}")
    return version[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path, help="A new directory; existing output is never replaced.")
    args = parser.parse_args()
    build = args.build.resolve()
    version = verify_build(build)
    destination = args.output.resolve()
    destination.mkdir(parents=True, exist_ok=False)
    stem = f"MillerScore-v{version}-Windows-x64"
    runtime = destination / stem
    runtime.mkdir()
    skipped = []
    for source in sorted((build / "install").rglob("*")):
        if not source.is_file():
            continue
        relative = source.relative_to(build / "install")
        if not source.resolve().is_relative_to((build / "install").resolve()):
            raise ValueError(f"Runtime file resolves outside the installation: {relative}")
        if relative.as_posix() == "MillerScore.exe" or allowed_runtime_path(relative):
            target = runtime / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        else:
            skipped.append(relative.as_posix())
    for name in ("LICENSE.txt", "PRIVACY.md"):
        shutil.copy2(ROOT / name, runtime / name)
    guide = runtime / "guide"
    guide.mkdir()
    shutil.copy2(ROOT / "docs/index.html", guide / "index.html")
    translated_guide = ROOT / "docs/pt-BR/index.html"
    translation_note = ""
    if translated_guide.is_file():
        (guide / "pt-BR").mkdir()
        shutil.copy2(translated_guide, guide / "pt-BR/index.html")
        translation_note = "Open guide/pt-BR/index.html for the Brazilian Portuguese translation.\n"
    (runtime / "README.txt").write_text(
        f"MillerScore {version} for Windows (x64)\n\n"
        "Launch MillerScore.exe. Keep this folder and its resources together.\n"
        "Open guide/index.html for the user guide. Imported audio is referenced externally.\n"
        + translation_note +
        "Muse Sounds is optional, separately installed, and is not bundled.\n"
        "Local crash reports are not uploaded automatically.\n\n"
        "Source: https://github.com/dulcedelechedev/millerscore\n"
        "Windows builds: https://dulcedelechedev.itch.io/millerscore\n"
        "MillerScore is licensed under GPLv3; see LICENSE.txt and licenses/.\n"
        "Independent community fork; no affiliation with or endorsement by Muse Group.\n",
        encoding="utf-8",
    )
    entries = []
    for file in sorted(runtime.rglob("*")):
        if file.is_file():
            with file.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            entries.append({"path": file.relative_to(runtime).as_posix(), "size": file.stat().st_size, "sha256": digest})
    archive = destination / f"{stem}.zip"
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for entry in entries:
            output.write(runtime / entry["path"], f"{stem}/{entry['path']}")
    with archive.open("rb") as stream:
        checksum = hashlib.file_digest(stream, "sha256").hexdigest()
    manifest = {"product": "MillerScore", "version": version, "profile": "release", "channel": "stable",
                "archive": archive.name, "size": archive.stat().st_size, "sha256": checksum,
                "status": "assembled; packaged smoke tests required", "files": entries, "excluded": skipped}
    (destination / "package-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: manifest[key] for key in ("archive", "size", "sha256", "status")}, indent=2))


if __name__ == "__main__":
    main()
