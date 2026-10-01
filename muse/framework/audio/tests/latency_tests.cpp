/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
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
#include <gtest/gtest.h>

#include <vector>

#include "audio/engine/internal/mixer.h"
#include "audio/engine/internal/nodes/trackchain.h"
#include "audio/engine/internal/nodes/fxchain.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::engine;

namespace {
const OutputSpec SPEC { 44100, 64, 2 };

struct ImpulseTag {
    static constexpr const char* name = "Impulse";
};

//! Emits one full-scale sample on every channel at the first frame, then silence.
class ImpulseSource : public AudioNode<ImpulseTag>
{
protected:
    void doSelfProcess(float* buffer, samples_t samplesPerChannel) override
    {
        if (!m_fired && samplesPerChannel > 0) {
            for (audioch_t ch = 0; ch < outputSpec().audioChannelCount; ++ch) {
                buffer[ch] = 1.f;
            }
            m_fired = true;
        }
    }

private:
    bool m_fired = false;
};

//! An effect that really delays the signal by its reported latency.
class DelayFx : public IFxProcessor
{
public:
    explicit DelayFx(samples_t latency)
        : m_latency(latency), m_ring(latency * SPEC.audioChannelCount, 0.f)
    {
        m_params.chainOrder = 0;
        m_params.active = true;
        m_params.resourceMeta = { "Delay", "Test", {}, resourceTypeName(AudioResourceType::NativeEffect) };
    }

    AudioFxType type() const override { return AudioFxType::MuseFx; }
    std::string name() const override { return "Delay"; }
    const AudioFxParams& params() const override { return m_params; }
    async::Channel<AudioFxParams> paramsChanged() const override { return m_paramsChanged; }
    void setOutputSpec(const OutputSpec&) override {}
    bool active() const override { return m_params.active; }
    void setActive(bool active) override { m_params.active = active; }
    void setMode(const ProcessMode) override {}
    bool shouldProcessDuringSilence() const override { return true; }
    samples_t latencySamples() const override { return m_latency; }

    void process(float* buffer, samples_t samplesPerChannel, samples_t) override
    {
        const size_t channels = SPEC.audioChannelCount;
        for (samples_t frame = 0; frame < samplesPerChannel; ++frame) {
            for (size_t ch = 0; ch < channels; ++ch) {
                float& slot = m_ring[m_pos * channels + ch];
                const float out = slot;
                slot = buffer[frame * channels + ch];
                buffer[frame * channels + ch] = out;
            }
            m_pos = (m_pos + 1) % m_latency;
        }
    }

private:
    samples_t m_latency = 0;
    std::vector<float> m_ring;
    size_t m_pos = 0;
    AudioFxParams m_params;
    async::Channel<AudioFxParams> m_paramsChanged;
};

TrackChainPtr makeTrack(TrackId id, IFxProcessorPtr fx)
{
    auto chain = std::make_shared<TrackChain>(id, "track");
    chain->setOutputSpec(SPEC);
    chain->setSource(std::make_shared<ImpulseSource>());

    auto fxChain = std::make_shared<FxChain>();
    fxChain->setFxList(fx ? std::vector<IFxProcessorPtr> { fx } : std::vector<IFxProcessorPtr> {});
    chain->setFxChain(fxChain);
    chain->rebuild();
    return chain;
}

//! Frames (channel 0) where the mixed output is not silent.
std::vector<size_t> soundingFrames(const std::vector<float>& buffer)
{
    std::vector<size_t> frames;
    for (size_t i = 0; i < buffer.size(); i += SPEC.audioChannelCount) {
        if (buffer[i] != 0.f) {
            frames.push_back(i / SPEC.audioChannelCount);
        }
    }
    return frames;
}
}

// A track without latency is delayed to line up with a track whose effect adds 10 samples.
TEST(Audio_LatencyTests, TracksLineUpWithTheSlowestChain)
{
    Mixer mixer;
    mixer.setOutputSpec(SPEC);
    mixer.addTrack(makeTrack(1, std::make_shared<DelayFx>(10)), {});
    mixer.addTrack(makeTrack(2, nullptr), {});

    std::vector<float> out(SPEC.samplesPerChannel * SPEC.audioChannelCount, 0.f);
    mixer.process(out.data(), SPEC.samplesPerChannel);

    ASSERT_EQ(soundingFrames(out), std::vector<size_t>({ 10 }));
    EXPECT_FLOAT_EQ(out[10 * SPEC.audioChannelCount], 2.f);
}

// With no latency anywhere nothing is delayed.
TEST(Audio_LatencyTests, NoLatencyNoDelay)
{
    Mixer mixer;
    mixer.setOutputSpec(SPEC);
    mixer.addTrack(makeTrack(1, nullptr), {});
    mixer.addTrack(makeTrack(2, nullptr), {});

    std::vector<float> out(SPEC.samplesPerChannel * SPEC.audioChannelCount, 0.f);
    mixer.process(out.data(), SPEC.samplesPerChannel);

    ASSERT_EQ(soundingFrames(out), std::vector<size_t>({ 0 }));
    EXPECT_FLOAT_EQ(out[0], 2.f);
}

// The delayed signal crosses block boundaries.
TEST(Audio_LatencyTests, CompensationSpansBlocks)
{
    Mixer mixer;
    mixer.setOutputSpec(SPEC);
    const samples_t latency = SPEC.samplesPerChannel + 5; // longer than one block
    mixer.addTrack(makeTrack(1, std::make_shared<DelayFx>(latency)), {});
    mixer.addTrack(makeTrack(2, nullptr), {});

    std::vector<float> first(SPEC.samplesPerChannel * SPEC.audioChannelCount, 0.f);
    mixer.process(first.data(), SPEC.samplesPerChannel);
    std::vector<float> second(SPEC.samplesPerChannel * SPEC.audioChannelCount, 0.f);
    mixer.process(second.data(), SPEC.samplesPerChannel);

    EXPECT_TRUE(soundingFrames(first).empty());
    ASSERT_EQ(soundingFrames(second), std::vector<size_t>({ 5 }));
    EXPECT_FLOAT_EQ(second[5 * SPEC.audioChannelCount], 2.f);
}

// A bypassed effect adds no latency, so nothing is compensated for it.
TEST(Audio_LatencyTests, BypassedEffectAddsNoLatency)
{
    auto fx = std::make_shared<DelayFx>(10);
    TrackChainPtr slow = makeTrack(1, fx);

    AudioFxParams bypassed = fx->params();
    bypassed.active = false;
    slow->fxChain()->setFxChainSpec({ { 0, bypassed } });

    EXPECT_EQ(slow->latencySamples(), 0u);
}
