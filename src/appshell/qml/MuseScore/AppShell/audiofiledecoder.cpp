/* SPDX-License-Identifier: GPL-3.0-only */
#include "audiofiledecoder.h"

#include <QFile>
#include <QFileInfo>
#include <QStringList>

// dr_libs by David Reid (public domain / MIT No Attribution): src/appshell/thirdparty/dr_libs
#define DR_WAV_IMPLEMENTATION
#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#include "../../../thirdparty/dr_libs/dr_wav.h"
#include "../../../thirdparty/dr_libs/dr_mp3.h"
#include "../../../thirdparty/dr_libs/dr_flac.h"

using namespace mu::appshell;

QStringList mu::appshell::audioFileSuffixes()
{
    return { "wav", "mp3", "flac" };
}

bool mu::appshell::writeWavFile(const QString& path, const std::vector<float>& samples, int channels, int sampleRate,
                                QString& error)
{
    if (channels <= 0 || sampleRate <= 0) {
        error = QObject::tr("The recording has no audio format.");
        return false;
    }
    drwav_data_format format {};
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = drwav_uint32(channels);
    format.sampleRate = drwav_uint32(sampleRate);
    format.bitsPerSample = 32;

    void* data = nullptr;
    size_t size = 0;
    drwav wav;
    if (!drwav_init_memory_write(&wav, &data, &size, &format, nullptr)) {
        error = QObject::tr("The recording could not be encoded.");
        return false;
    }
    drwav_write_pcm_frames(&wav, samples.size() / size_t(channels), samples.data());
    drwav_uninit(&wav);

    QFile file(path);
    const bool written = file.open(QIODevice::WriteOnly) && file.write(static_cast<const char*>(data), qint64(size)) == qint64(size);
    drwav_free(data, nullptr);
    if (!written) {
        error = QObject::tr("The recording could not be saved to %1").arg(path);
        return false;
    }
    return true;
}

DecodedAudioPtr mu::appshell::decodeAudioFile(const QString& path, QString& error)
{
    // Read through Qt, so paths with any characters work on every platform
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QObject::tr("The file could not be opened: %1").arg(path);
        return nullptr;
    }
    const QByteArray data = file.readAll();
    const QString suffix = QFileInfo(path).suffix().toLower();

    auto decoded = std::make_shared<DecodedAudio>();
    float* frames = nullptr;
    unsigned long long frameCount = 0;
    unsigned int channels = 0;
    unsigned int sampleRate = 0;

    if (suffix == "wav") {
        frames = drwav_open_memory_and_read_pcm_frames_f32(data.constData(), size_t(data.size()), &channels, &sampleRate,
                                                           reinterpret_cast<drwav_uint64*>(&frameCount), nullptr);
    } else if (suffix == "mp3") {
        drmp3_config config {};
        frames = drmp3_open_memory_and_read_pcm_frames_f32(data.constData(), size_t(data.size()), &config,
                                                           reinterpret_cast<drmp3_uint64*>(&frameCount), nullptr);
        channels = config.channels;
        sampleRate = config.sampleRate;
    } else if (suffix == "flac") {
        frames = drflac_open_memory_and_read_pcm_frames_f32(data.constData(), size_t(data.size()), &channels, &sampleRate,
                                                            reinterpret_cast<drflac_uint64*>(&frameCount), nullptr);
    } else {
        error = QObject::tr("This file type is not supported yet: .%1 (use WAV, MP3 or FLAC)").arg(suffix);
        return nullptr;
    }

    if (!frames || channels == 0 || sampleRate == 0) {
        error = QObject::tr("The audio in this file could not be read: %1").arg(path);
        return nullptr;
    }

    decoded->samples.assign(frames, frames + frameCount * channels);
    decoded->channels = int(channels);
    decoded->sampleRate = int(sampleRate);

    if (suffix == "wav") {
        drwav_free(frames, nullptr);
    } else if (suffix == "mp3") {
        drmp3_free(frames, nullptr);
    } else {
        drflac_free(frames, nullptr);
    }
    return decoded;
}
