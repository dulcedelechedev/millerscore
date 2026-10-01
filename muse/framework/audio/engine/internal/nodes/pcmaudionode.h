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

#pragma once

#include <vector>

#include "audiosourcenode.h"

#include "global/io/iodevice.h"

namespace muse::audio::engine {
//! Plays prepared audio (an audio track: clips already decoded and placed on the timeline).
//! The device holds a header, "MSPC", then little-endian uint32 version (1), sample rate and
//! channel count, then interleaved float32 frames starting at time zero. The audio is converted
//! to the engine's rate and channels once, then read at the playhead position.
class PcmAudioNode : public AudioSourceNode
{
public:
    explicit PcmAudioNode(io::IODevice* data);

    bool isValid() const;

    void seek(const TimePosition& position, const bool flushSound = true) override;
    void flush() override;

    const AudioInputParams& inputParams() const override;
    void applyInputParams(const AudioInputParams& requiredParams) override;
    async::Channel<AudioInputParams> inputParamsChanged() const override;

    void prepareToPlay() override;
    bool readyToPlay() const override;
    async::Notification readyToPlayChanged() const override;

    bool hasPendingChunks() const override;
    void processInput() override;
    InputProcessingProgress inputProcessingProgress() const override;

    void clearCache() override;

private:
    void onOutputSpecChanged(const OutputSpec& spec) override;
    void doSelfProcess(float* buffer, samples_t samplesPerChannel) override;

    void convertToOutput();

    std::vector<float> m_source;       // as received
    sample_rate_t m_sourceRate = 0;
    audioch_t m_sourceChannels = 0;

    std::vector<float> m_output;       // at the output rate, with the output channel count
    sample_rate_t m_outputRate = 0;
    audioch_t m_outputChannels = 0;

    int64_t m_frame = 0;               // playhead, in output frames
    float m_tracePeak = 0.f;
    int64_t m_traceFrames = 0;
    TimePosition m_position;

    AudioInputParams m_params;
    async::Channel<AudioInputParams> m_paramsChanges;
    async::Notification m_readyToPlayChanged;
};

using PcmAudioNodePtr = std::shared_ptr<PcmAudioNode>;
}
