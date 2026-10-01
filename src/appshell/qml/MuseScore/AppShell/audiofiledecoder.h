/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <memory>
#include <vector>

#include <QString>

namespace mu::appshell {
//! An audio file decoded to interleaved float samples
struct DecodedAudio {
    std::vector<float> samples;
    int sampleRate = 0;
    int channels = 0;

    size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }
    double seconds() const { return sampleRate > 0 ? double(frames()) / sampleRate : 0.0; }
};

using DecodedAudioPtr = std::shared_ptr<const DecodedAudio>;

//! WAV, MP3 and FLAC (dr_libs). Returns nullptr and sets `error` when the file cannot be read.
DecodedAudioPtr decodeAudioFile(const QString& path, QString& error);

//! The file types decodeAudioFile() reads, for file dialogs
QStringList audioFileSuffixes();

//! Writes interleaved float samples as a 32-bit float WAV file (recordings)
bool writeWavFile(const QString& path, const std::vector<float>& samples, int channels, int sampleRate, QString& error);
}
