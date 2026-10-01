/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 */
#include <gtest/gtest.h>

#include "audio/engine/internal/synthesizers/fluidsynth/fluidsequencer.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::synth;

namespace {
mpe::NoteEvent note(int staff = 0, int voice = 0, int timestamp = 1000000)
{
    return mpe::NoteEvent(timestamp, 500000, voice, staff, 0,
                          mpe::dynamicLevelFromType(mpe::DynamicType::Natural), {}, 2.0);
}

mpe::ControllerChangeEvent controller(int cc, float value, int staff = 0, int voice = 0)
{
    mpe::ControllerChangeEvent result;
    result.type = mpe::ControllerChangeEvent::GenericMidiController;
    result.controllerNumber = cc;
    result.val = value;
    result.layerIdx = mpe::makeLayerIdx(staff, voice);
    return result;
}

mpe::PlaybackData data(bool singleNoteDynamics = false)
{
    mpe::PlaybackData result;
    result.setupData = { mpe::SoundId::Piano, mpe::SoundCategory::Keyboards, {}, singleNoteDynamics };
    result.originEvents[1000000].emplace_back(note());
    return result;
}

void load(FluidSequencer& sequencer, const mpe::PlaybackData& playback)
{
    sequencer.init(playback.setupData, std::nullopt, playback.setupData.supportsSingleNoteDynamics, 16);
    sequencer.load(playback);
    sequencer.setActive(true);
}
}

TEST(FluidSequencerControllers, InitialPointReachesFutureNotes)
{
    auto playback = data();
    playback.originEvents[0].emplace_back(controller(7, 0.25f));
    FluidSequencer sequencer;
    load(sequencer, playback);
    const auto values = sequencer.currentGenericControllerValues();
    ASSERT_EQ(values.size(), 1);
    EXPECT_EQ(values[0].channel, 0);
    EXPECT_EQ(values[0].controller, 7);
    EXPECT_EQ(values[0].value, 32);
    const auto sequences = sequencer.movePlaybackForward(msecs_t(1));
    ASSERT_EQ(sequences.at(msecs_t(0)).size(), 1);
    EXPECT_TRUE(std::holds_alternative<GenericMidiControllerEvent>(sequences.at(msecs_t(0)).front()));
}

TEST(FluidSequencerControllers, ProtectedCommandsAreRejected)
{
    auto playback = data();
    const int protectedControllers[] = { 0, 6, 32, 38, 84, 88, 96, 97, 98, 99, 100, 101, 120, 121, 122, 123, 124, 125, 126, 127 };
    for (int cc : protectedControllers) playback.originEvents[0].emplace_back(controller(cc, 1.f));
    playback.originEvents[0].emplace_back(controller(-1, 1.f));
    playback.originEvents[0].emplace_back(controller(128, 1.f));
    FluidSequencer sequencer;
    load(sequencer, playback);
    EXPECT_TRUE(sequencer.genericControllerKeys().empty());
}

TEST(FluidSequencerControllers, InvalidValuesAndUnknownLayersAreRejected)
{
    auto playback = data(true);
    playback.originEvents[0].emplace_back(controller(7, -0.01f));
    playback.originEvents[0].emplace_back(controller(10, 1.01f));
    playback.originEvents[0].emplace_back(controller(11, 0.5f, 99, 3));
    playback.originEvents[-1].emplace_back(controller(64, 1.f));
    FluidSequencer sequencer;
    load(sequencer, playback);
    EXPECT_TRUE(sequencer.genericControllerKeys().empty());
}

TEST(FluidSequencerControllers, PermittedByteControllersKeepTheirNumbers)
{
    auto playback = data();
    for (int cc = 0; cc < 128; ++cc) playback.originEvents[0].emplace_back(controller(cc, 0.5f));
    FluidSequencer sequencer;
    load(sequencer, playback);
    const auto values = sequencer.currentGenericControllerValues();
    ASSERT_EQ(values.size(), 108);
    for (const auto& value : values) {
        EXPECT_TRUE(mpe::isSafeMidiController(value.controller));
        EXPECT_EQ(value.value, 64);
    }
}

TEST(FluidSequencerControllers, StaffAndVoiceChannelsRemainScoped)
{
    auto playback = data(true);
    playback.originEvents[1000000].emplace_back(note(1, 0));
    playback.originEvents[1000000].emplace_back(note(1, 1));
    playback.originEvents[0].emplace_back(controller(10, 1.f, 1, 1));
    FluidSequencer sequencer;
    load(sequencer, playback);
    const auto values = sequencer.currentGenericControllerValues();
    ASSERT_EQ(values.size(), 1);
    const auto& mapping = sequencer.channels().data().at(mpe::makeLayerIdx(1, 1));
    EXPECT_EQ(values[0].channel, mapping.begin()->second.first);
    EXPECT_NE(values[0].channel, sequencer.channels().data().at(mpe::makeLayerIdx(0, 0)).begin()->second.first);
}

TEST(FluidSequencerControllers, NonDynamicSoundNormalizesStaffLayers)
{
    auto playback = data();
    playback.originEvents[0].emplace_back(controller(10, 1.f, 12, 0));
    FluidSequencer sequencer;
    load(sequencer, playback);
    ASSERT_EQ(sequencer.currentGenericControllerValues().size(), 1);
    EXPECT_EQ(sequencer.currentGenericControllerValues()[0].channel, 0);
}

TEST(FluidSequencerControllers, SeekRestoresLatestStateWithoutFuturePoints)
{
    auto playback = data();
    playback.originEvents[0].emplace_back(controller(64, 0.f));
    playback.originEvents[200000].emplace_back(controller(64, 1.f));
    playback.originEvents[800000].emplace_back(controller(64, 0.f));
    FluidSequencer sequencer;
    load(sequencer, playback);
    sequencer.setPlaybackPosition(msecs_t(500000));
    ASSERT_EQ(sequencer.currentGenericControllerValues().size(), 1);
    EXPECT_EQ(sequencer.currentGenericControllerValues()[0].value, 127);
    sequencer.setPlaybackPosition(msecs_t(100000));
    EXPECT_EQ(sequencer.currentGenericControllerValues()[0].value, 0);
}

TEST(FluidSequencerControllers, OffstreamCannotApplyGenericControllers)
{
    auto playback = data();
    FluidSequencer sequencer;
    load(sequencer, playback);
    mpe::PlaybackEventsMap preview;
    preview[0].emplace_back(controller(7, 0.f));
    playback.offStream.send(preview, true);
    sequencer.setActive(false);
    const auto sequences = sequencer.movePlaybackForward(msecs_t(1000));
    for (const auto& [_, events] : sequences) {
        for (const auto& event : events) EXPECT_FALSE(std::holds_alternative<GenericMidiControllerEvent>(event));
    }
}

TEST(FluidSequencerControllers, ReloadRemovesOldControllerState)
{
    auto playback = data();
    playback.originEvents[0].emplace_back(controller(7, 0.f));
    FluidSequencer sequencer;
    load(sequencer, playback);
    ASSERT_EQ(sequencer.genericControllerKeys().size(), 1);
    playback.originEvents[0].clear();
    playback.mainStream.send(playback.originEvents, {});
    EXPECT_TRUE(sequencer.genericControllerKeys().empty());
}

TEST(FluidSequencerControllers, SameTimestampDeduplicatesLayerAliases)
{
    auto playback = data();
    playback.originEvents[0].emplace_back(controller(7, 0.25f, 0));
    playback.originEvents[0].emplace_back(controller(7, 0.75f, 1));
    FluidSequencer sequencer;
    load(sequencer, playback);
    const auto sequences = sequencer.movePlaybackForward(msecs_t(1));
    ASSERT_EQ(sequences.at(msecs_t(0)).size(), 1);
    EXPECT_EQ(sequencer.currentGenericControllerValues()[0].value, 95);
}

TEST(FluidSequencerControllers, GenericPedalOverridesNotationPedal)
{
    for (const int cc : { 64, 66 }) {
        auto playback = data();
        mpe::ArrangementContext arrangement;
        arrangement.actualTimestamp = 100000;
        arrangement.actualDuration = 500000;
        mpe::ExpressionContext expression;
        expression.nominalDynamicLevel = mpe::dynamicLevelFromType(mpe::DynamicType::Natural);
        const auto type = cc == 64 ? *SUSTAIN_PEDAL_CC_SUPPORTED_TYPES.begin() : *SOSTENUTO_PEDAL_CC_SUPPORTED_TYPES.begin();
        mpe::ArticulationAppliedData articulation;
        articulation.meta.type = type;
        articulation.meta.timestamp = 100000;
        articulation.meta.overallDuration = 500000;
        expression.articulations[type] = articulation;
        playback.originEvents[100000].emplace_back(mpe::NoteEvent(std::move(arrangement), {}, std::move(expression)));
        playback.originEvents[0].emplace_back(controller(cc, 0.f));
        FluidSequencer sequencer;
        load(sequencer, playback);
        const auto sequences = sequencer.movePlaybackForward(msecs_t(2000000));
        int genericCount = 0;
        for (const auto& [_, events] : sequences) {
            for (const auto& event : events) {
                if (const auto* native = std::get_if<midi::Event>(&event)) {
                    EXPECT_FALSE(native->opcode() == midi::Event::Opcode::ControlChange && native->index() == cc);
                } else if (std::get<GenericMidiControllerEvent>(event).event.index() == cc) {
                    ++genericCount;
                }
            }
        }
        EXPECT_EQ(genericCount, 1);
    }
}

TEST(FluidSequencerControllers, GenericExpressionOverridesNotationDynamics)
{
    auto playback = data(true);
    playback.originEvents[0].emplace_back(controller(11, 0.25f));
    playback.dynamics[0][0].outValue = 0.7;
    playback.dynamics[0][500000].outValue = 0.9;
    FluidSequencer sequencer;
    load(sequencer, playback);
    const auto sequences = sequencer.movePlaybackForward(msecs_t(1000000));
    for (const auto& [_, events] : sequences) {
        for (const auto& event : events) {
            if (const auto* native = std::get_if<midi::Event>(&event)) {
                EXPECT_FALSE(native->opcode() == midi::Event::Opcode::ControlChange && native->index() == 11);
            }
        }
    }
}
