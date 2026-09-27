#!/usr/bin/env python3
"""Packs frames captured by capture.mjs into share/intro/vignette.frames and converts the jingle to
share/intro/jingle.wav (16-bit PCM, what waveOut plays). Needs ffmpeg on PATH.

usage: python pack.py <framesDir> [jpegQuality=4]

vignette.frames, little endian:
    char[4] "MSVF"; u32 version (1); u32 count; u32 fps; u32 width, height (pixels);
    u32 logicalWidth, logicalHeight (window size at 96 dpi); u32 endMs (when the vignette is over);
    u32 offsets[count + 1] (from the start of the file); then the JPEG frames back to back.
"""
import json
import os
import struct
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
out_dir = os.path.normpath(os.path.join(here, "..", "..", "..", "share", "intro"))

frames_dir = sys.argv[1]
quality = sys.argv[2] if len(sys.argv) > 2 else "4"
info = json.load(open(os.path.join(frames_dir, "cues.json"), encoding="utf-8"))
os.makedirs(out_dir, exist_ok=True)

with tempfile.TemporaryDirectory() as tmp:
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", os.path.join(frames_dir, "f%04d.png"),
                    "-q:v", quality, "-pix_fmt", "yuvj420p", os.path.join(tmp, "f%04d.jpg")], check=True)
    names = sorted(n for n in os.listdir(tmp) if n.endswith(".jpg"))
    jpegs = [open(os.path.join(tmp, n), "rb").read() for n in names]

width = round(info["width"] * info["dpr"])
height = round(info["height"] * info["dpr"])
header = struct.pack("<4s8I", b"MSVF", 1, len(jpegs), info["fps"], width, height,
                     info["width"], info["height"], round(info["cues"]["end"] * 1000))
offset = len(header) + 4 * (len(jpegs) + 1)
offsets = []
for j in jpegs:
    offsets.append(offset)
    offset += len(j)
offsets.append(offset)

with open(os.path.join(out_dir, "vignette.frames"), "wb") as f:
    f.write(header)
    f.write(struct.pack("<%dI" % len(offsets), *offsets))
    for j in jpegs:
        f.write(j)

subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", os.path.join(here, "assets", "jingle.mp3"),
                "-map_metadata", "-1", "-fflags", "+bitexact", "-c:a", "pcm_s16le", "-ar", "44100", "-ac", "2",
                os.path.join(out_dir, "jingle.wav")], check=True)

print("%d frames %dx%d, %.1f MB; jingle.wav %.1f MB" % (
    len(jpegs), width, height, offset / 1e6, os.path.getsize(os.path.join(out_dir, "jingle.wav")) / 1e6))
