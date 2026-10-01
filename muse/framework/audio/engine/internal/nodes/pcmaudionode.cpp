/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
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

#include "pcmaudionode.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QtGlobal>

#include "audio/common/audiosanitizer.h"

#include "log.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::engine;

namespace {
constexpr size_t HEADER_SIZE = 16;

uint32_t readU32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}

PcmAudioNode::PcmAudioNode(io::IODevice* data)
{
    if (!data) {
        return;
    }
    data->seek(0);
    const ByteArray bytes = data->readAll();
    if (bytes.size() < HEADER_SIZE || std::memcmp(bytes.constData(), "MSPC", 4) != 0 || readU32(bytes.constData() + 4) != 1) {
        LOGE() << "not an MSPC audio buffer";
        return;
    }
    m_sourceRate = readU32(bytes.constData() + 8);
    m_sourceChannels = audioch_t(readU32(bytes.constData() + 12));
    if (m_sourceRate == 0 || m_sourceChannels == 0) {
        return;
    }
    const size_t floats = (bytes.size() - HEADER_SIZE) / sizeof(float);
    m_source.resize(floats - floats % m_sourceChannels);
    std::memcpy(m_source.data(), bytes.constData() + HEADER_SIZE, m_source.size() * sizeof(float));
}

bool PcmAudioNode::isValid() const
{
    return m_sourceRate > 0 && m_sourceChannels > 0;
}

void PcmAudioNode::onOutputSpecChanged(const OutputSpec& spec)
{
    ONLY_AUDIO_ENGINE_THREAD;
    if (spec.sampleRate == m_outputRate && spec.audioChannelCount == m_outputChannels) {
        return;
    }
    m_outputRate = spec.sampleRate;
    m_outputChannels = spec.audioChannelCount;
    convertToOutput();
    if (m_position.isValid()) {
        m_frame = int64_t(std::llround(double(m_position.time()) * m_outputRate));
    }
}

//! Linear-interpolation resampling and channel mapping (mono is copied to every channel,
//! extra source channels are folded into the last output channel)
void PcmAudioNode::convertToOutput()
{
    m_output.clear();
    if (!isValid() || m_outputRate == 0 || m_outputChannels == 0) {
        return;
    }
    const size_t sourceFrames = m_source.size() / m_sourceChannels;
    const double ratio = double(m_sourceRate) / double(m_outputRate);
    const size_t outputFrames = size_t(std::floor(sourceFrames / ratio));
    m_output.assign(outputFrames * m_outputChannels, 0.f);

    for (size_t frame = 0; frame < outputFrames; ++frame) {
        const double position = frame * ratio;
        const size_t index = size_t(position);
        const size_t next = std::min(index + 1, sourceFrames - 1);
        const float fraction = float(position - double(index));
        for (audioch_t channel = 0; channel < m_outputChannels; ++channel) {
            const audioch_t sourceChannel = m_sourceChannels == 1 ? 0 : std::min<audioch_t>(channel, m_sourceChannels - 1);
            const float a = m_source[index * m_sourceChannels + sourceChannel];
            const float b = m_source[next * m_sourceChannels + sourceChannel];
            m_output[frame * m_outputChannels + channel] = a + (b - a) * fraction;
        }
    }
}

void PcmAudioNode::doSelfProcess(float* buffer, samples_t samplesPerChannel)
{
    ONLY_AUDIO_PROC_THREAD;
    const audioch_t channels = outputSpec().audioChannelCount;
    const size_t total = size_t(samplesPerChannel) * channels;
    // The mixer also processes tracks while the transport is stopped: stay silent and keep the position
    if (mode() != ProcessMode::Playing && mode() != ProcessMode::PlayingOffline) {
        std::fill(buffer, buffer + total, 0.f);
        return;
    }
    if (channels != m_outputChannels || m_output.empty()) {
        std::fill(buffer, buffer + total, 0.f);
        m_frame += samplesPerChannel;
        return;
    }

    const int64_t available = int64_t(m_output.size() / channels);
    for (samples_t sample = 0; sample < samplesPerChannel; ++sample) {
        const int64_t frame = m_frame + sample;
        float* out = buffer + size_t(sample) * channels;
        if (frame < 0 || frame >= available) {
            std::fill(out, out + channels, 0.f);
        } else {
            std::memcpy(out, m_output.data() + size_t(frame) * channels, channels * sizeof(float));
        }
    }

    // MILLERSCORE_AUDIO_TRACE=1: the peak delivered, about once a second (for checking without listening)
    static const bool trace = qEnvironmentVariableIntValue("MILLERSCORE_AUDIO_TRACE") != 0;
    if (trace) {
        for (size_t index = 0; index < total; ++index) {
            m_tracePeak = std::max(m_tracePeak, std::abs(buffer[index]));
        }
        m_traceFrames += samplesPerChannel;
        if (m_traceFrames >= int64_t(m_outputRate)) {
            LOGI() << "audio track at " << double(m_frame) / m_outputRate << " s: peak " << m_tracePeak;
            m_tracePeak = 0.f;
            m_traceFrames = 0;
        }
    }
    m_frame += samplesPerChannel;
}

void PcmAudioNode::seek(const TimePosition& position, const bool)
{
    ONLY_AUDIO_ENGINE_THREAD;
    m_position = position;
    const sample_rate_t rate = m_outputRate > 0 ? m_outputRate : position.sampleRate();
    m_frame = int64_t(std::llround(double(position.time()) * rate));
}

void PcmAudioNode::flush()
{
}

const AudioInputParams& PcmAudioNode::inputParams() const
{
    return m_params;
}

void PcmAudioNode::applyInputParams(const AudioInputParams&)
{
}

async::Channel<AudioInputParams> PcmAudioNode::inputParamsChanged() const
{
    return m_paramsChanges;
}

void PcmAudioNode::prepareToPlay()
{
}

bool PcmAudioNode::readyToPlay() const
{
    return true;
}

async::Notification PcmAudioNode::readyToPlayChanged() const
{
    return m_readyToPlayChanged;
}

bool PcmAudioNode::hasPendingChunks() const
{
    return false;
}

void PcmAudioNode::processInput()
{
}

InputProcessingProgress PcmAudioNode::inputProcessingProgress() const
{
    return {};
}

void PcmAudioNode::clearCache()
{
}
