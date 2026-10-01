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

#include <thread>
#include <vector>

#include "audio/engine/internal/nodes/fxchain.h"
#include "audio/engine/internal/fx/abstractfxresolver.h"
#include "audio/engine/internal/fx/fxresolver.h"
#include "audio/engine/internal/audioengine.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::engine;
using namespace muse::audio::fx;

namespace {
AudioFxParams makeFx(AudioFxChainOrder order, const std::string& id,
                     AudioResourceType type = AudioResourceType::NativeEffect, bool active = true)
{
    AudioFxParams params;
    params.chainOrder = order;
    params.active = active;
    params.resourceMeta.id = id;
    params.resourceMeta.vendor = "Test";
    params.resourceMeta.type = resourceTypeName(type); // the in-memory wire name, e.g. "VstPlugin"
    return params;
}

//! Records the order it processes in and the configuration it is given.
class FakeFx : public IFxProcessor
{
public:
    FakeFx(const AudioFxParams& params, std::vector<std::string>* processLog = nullptr)
        : m_params(params), m_processLog(processLog) {}

    AudioFxType type() const override { return m_params.type(); }
    std::string name() const override { return m_params.resourceMeta.id; }
    const AudioFxParams& params() const override { return m_params; }
    async::Channel<AudioFxParams> paramsChanged() const override { return m_paramsChanged; }
    void setOutputSpec(const OutputSpec&) override {}
    bool active() const override { return m_params.active; }
    void setActive(bool active) override { m_params.active = active; }
    void setConfiguration(const AudioUnitConfig& configuration) override
    {
        m_params.configuration = configuration;
        ++configurationsApplied;
    }

    void setMode(const ProcessMode) override {}
    bool shouldProcessDuringSilence() const override { return false; }
    void process(float*, samples_t, samples_t) override
    {
        if (m_processLog) {
            m_processLog->push_back(m_params.resourceMeta.id);
        }
    }

    //! What a plug-in does when its own editor changes its state.
    void echo(const AudioFxParams& params) { m_paramsChanged.send(params); }

    int configurationsApplied = 0;

private:
    AudioFxParams m_params;
    std::vector<std::string>* m_processLog = nullptr;
    async::Channel<AudioFxParams> m_paramsChanged;
};

//! A resolver for one effect type that counts what it creates and removes.
class FakeResolver : public AbstractFxResolver
{
public:
    AudioResourceMetaList resolveResources() const override { return {}; }

    mutable int created = 0;
    int removed = 0;
    std::vector<std::string>* processLog = nullptr;

protected:
    IFxProcessorPtr createMasterFx(const AudioFxParams& params, const OutputSpec&) const override
    {
        ++created;
        return std::make_shared<FakeFx>(params, processLog);
    }

    IFxProcessorPtr createTrackFx(const TrackId, const AudioFxParams& params, const OutputSpec&) const override
    {
        ++created;
        return std::make_shared<FakeFx>(params, processLog);
    }

    void removeTrackFx(const TrackId, const AudioResourceId&, AudioFxChainOrder) override { ++removed; }
};

const OutputSpec SPEC { 44100, 64, 2 };
}

// A slot whose effect cannot be resolved (plug-in missing) keeps its effect,
// bypass and opaque state in the chain spec, so it is saved unchanged.
TEST(Audio_FxChainTests, KeepsUnresolvedSlot)
{
    AudioFxParams reverb = makeFx(0, "Muse Reverb");
    AudioFxParams absent = makeFx(1, "Absent Effect", AudioResourceType::VstPlugin, false);
    absent.configuration["componentState"] = "opaque";

    FxChain chain;
    chain.setFxList({ std::make_shared<FakeFx>(reverb) });
    chain.setFxChainSpec({ { 0, reverb }, { 1, absent } });

    ASSERT_EQ(chain.fxChainSpec().size(), 2u);
    EXPECT_EQ(chain.fxChainSpec().at(1), absent);
}

// An empty slot (no resource) is not kept.
TEST(Audio_FxChainTests, DropsBlankSlot)
{
    FxChain chain;
    chain.setFxList({});
    chain.setFxChainSpec({ { 0, AudioFxParams() } });

    EXPECT_TRUE(chain.fxChainSpec().empty());
}

// A processor reporting its own state (e.g. an editor edit) must not revert
// the user's bypass.
TEST(Audio_FxChainTests, ProcessorEchoKeepsBypass)
{
    AudioFxParams fx = makeFx(0, "Muse Reverb", AudioResourceType::NativeEffect, true);
    auto processor = std::make_shared<FakeFx>(fx);

    FxChain chain;
    chain.setFxList({ processor });

    AudioFxParams bypassed = fx;
    bypassed.active = false;
    chain.setFxChainSpec({ { 0, bypassed } });

    AudioFxParams echoed = fx; // still says active, with a new state
    echoed.configuration["componentState"] = "edited";
    processor->echo(echoed);

    const AudioFxParams& result = chain.fxChainSpec().at(0);
    EXPECT_FALSE(result.active);
    EXPECT_EQ(result.configuration.at("componentState"), "edited");
}

// Effects run in slot order whichever resolver created them.
TEST(Audio_FxResolverTests, ResolvesInSlotOrderAcrossTypes)
{
    auto vst = std::make_shared<FakeResolver>();
    auto muse = std::make_shared<FakeResolver>();

    FxResolver resolver;
    resolver.registerResolver(AudioFxType::VstFx, vst);
    resolver.registerResolver(AudioFxType::MuseFx, muse);

    AudioFxChain chain {
        { 0, makeFx(0, "Vst A", AudioResourceType::VstPlugin) },
        { 1, makeFx(1, "Muse B") },
        { 2, makeFx(2, "Vst C", AudioResourceType::VstPlugin) },
    };

    std::vector<IFxProcessorPtr> list = resolver.resolveFxList(1, chain, SPEC);

    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0]->params().chainOrder, 0);
    EXPECT_EQ(list[1]->params().chainOrder, 1);
    EXPECT_EQ(list[2]->params().chainOrder, 2);
}

// An empty chain releases what a resolver still holds for the track.
TEST(Audio_FxResolverTests, EmptyChainReleasesTrackEffects)
{
    FakeResolver resolver;

    resolver.resolveFxList(7, { { 0, makeFx(0, "A") }, { 1, makeFx(1, "B") } }, SPEC);
    ASSERT_EQ(resolver.created, 2);

    std::vector<IFxProcessorPtr> list = resolver.resolveFxList(7, {}, SPEC);

    EXPECT_TRUE(list.empty());
    EXPECT_EQ(resolver.removed, 2);
}

// A kept slot keeps its processor; a new saved state is applied to it.
TEST(Audio_FxResolverTests, AppliesChangedStateToKeptEffect)
{
    FakeResolver resolver;

    AudioFxParams first = makeFx(0, "A");
    first.configuration["componentState"] = "one";
    IFxProcessorPtr processor = resolver.resolveFxList(3, { { 0, first } }, SPEC).at(0);

    AudioFxParams second = first;
    second.configuration["componentState"] = "two";
    IFxProcessorPtr again = resolver.resolveFxList(3, { { 0, second } }, SPEC).at(0);

    EXPECT_EQ(resolver.created, 1);
    EXPECT_EQ(again, processor);
    EXPECT_EQ(again->params().configuration.at("componentState"), "two");
    EXPECT_EQ(std::static_pointer_cast<FakeFx>(again)->configurationsApplied, 1);
}

// Moving an effect to another slot re-creates it there with its saved state.
TEST(Audio_FxResolverTests, SwappedSlotsKeepTheirState)
{
    FakeResolver resolver;

    AudioFxParams a = makeFx(0, "A");
    a.configuration["componentState"] = "state of A";
    AudioFxParams b = makeFx(1, "B");
    resolver.resolveFxList(4, { { 0, a }, { 1, b } }, SPEC);

    AudioFxParams movedA = a;
    movedA.chainOrder = 1;
    AudioFxParams movedB = b;
    movedB.chainOrder = 0;
    std::vector<IFxProcessorPtr> list = resolver.resolveFxList(4, { { 0, movedB }, { 1, movedA } }, SPEC);

    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[1]->params().resourceMeta.id, "A");
    EXPECT_EQ(list[1]->params().configuration.at("componentState"), "state of A");
}

// The FxChain processes its nodes in the order of the list it is given.
TEST(Audio_FxChainTests, ProcessesInListOrder)
{
    std::vector<std::string> log;
    FxChain chain;
    chain.setOutputSpec(SPEC);
    chain.setFxList({ std::make_shared<FakeFx>(makeFx(0, "first"), &log),
                      std::make_shared<FakeFx>(makeFx(1, "second"), &log) });

    std::vector<float> buffer(SPEC.samplesPerChannel * SPEC.audioChannelCount);
    chain.process(buffer.data(), SPEC.samplesPerChannel);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "first");
    EXPECT_EQ(log[1], "second");
}

// The audio callback outputs silence during a LongOperation...
TEST(Audio_AudioEngineTests, CallbackIsSilentDuringLongOperation)
{
    AudioEngine engine;
    engine.init(SPEC);
    std::vector<float> buffer(SPEC.samplesPerChannel * SPEC.audioChannelCount);

    EXPECT_EQ(engine.process(buffer.data(), SPEC.samplesPerChannel), SPEC.samplesPerChannel);

    samples_t produced = 12345; // anything but the expected 0
    engine.execOperation(OperationType::LongOperation, [&]() {
        std::thread callback([&]() { produced = engine.process(buffer.data(), SPEC.samplesPerChannel); });
        callback.join();
    });

    EXPECT_EQ(produced, 0);
}

// ...and stays silent when a QuickOperation runs inside it (RPC playback data
// delivered while an export renders): the nested operation must not let the
// callback process the graph mid-render, nor wait for it or re-lock.
TEST(Audio_AudioEngineTests, NestedQuickOperationKeepsLongOperationSilent)
{
    AudioEngine engine;
    engine.init(SPEC);
    std::vector<float> buffer(SPEC.samplesPerChannel * SPEC.audioChannelCount);

    samples_t produced = 12345; // anything but the expected 0
    engine.execOperation(OperationType::LongOperation, [&]() {
        engine.execOperation(OperationType::QuickOperation, [&]() {
            std::thread callback([&]() { produced = engine.process(buffer.data(), SPEC.samplesPerChannel); });
            callback.join();
        });
    });

    EXPECT_EQ(produced, 0);

    // After both operations the callback processes normally again.
    EXPECT_EQ(engine.process(buffer.data(), SPEC.samplesPerChannel), SPEC.samplesPerChannel);
}
