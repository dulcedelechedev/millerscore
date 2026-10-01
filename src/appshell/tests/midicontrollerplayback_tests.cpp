/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtest/gtest.h>
#include <limits>
#include "engraving/playback/utils/midicontrollerautomation.h"

using namespace mu::engraving;
using muse::mpe::ControllerChangeEvent;

namespace {
AutomationPoint point(double outgoing, double incoming = -1.0, double bendTime = 0.5, double bend = 0.5)
{
    AutomationPoint result;
    result.value.outValue = outgoing;
    if (incoming >= 0.0) result.value.inValue = AutomationPoint::ExplicitArrival { incoming, { bendTime, bend } };
    return result;
}
const InstrumentTrackId piano { muse::ID(1), muse::String(u"piano") };
const InstrumentTrackId other { muse::ID(2), muse::String(u"piano") };
TempoTimeline timeline()
{
    TempoTimeline result;
    result.rebuild(TempoValues { {0, 2.0} }, {});
    return result;
}
const ControllerChangeEvent& controller(const muse::mpe::PlaybackEvent& value)
{
    return std::get<ControllerChangeEvent>(value);
}
}

TEST(MidiControllerPlaybackTests, ExactSafeSetMatchesRegistryAndNeverEmitsProtectedOrAliases)
{
    AutomationData data;
    AutomationCurveMap curves;
    for (int cc = 0; cc < 128; ++cc) {
        EXPECT_EQ(muse::mpe::isSafeMidiController(cc), MIDI_CONTROLLER_CATALOG[cc].isCurveEditable()) << cc;
        curves[AutomationCurveKey::midiLane(piano, "cc:" + std::to_string(cc))][0] = point(cc / 127.0);
    }
    curves[AutomationCurveKey::midiLane(piano, "cc:007")][0] = point(1.0);
    curves[AutomationCurveKey::midiLane(piano, "rpn:0:0")][0] = point(1.0);
    curves[AutomationCurveKey::midiLane(other, "cc:7")][0] = point(1.0);
    data.setCurves(curves);
    const auto events = renderMidiControllerAutomation(data, piano, {0}, {}, timeline(), true, 960);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.begin()->second.size(), 108u);
    std::set<int> numbers;
    for (const auto& value : events.begin()->second) {
        const auto& event = controller(value);
        EXPECT_TRUE(muse::mpe::isSafeMidiController(event.controllerNumber));
        EXPECT_EQ(std::lround(event.val.raw() * 127), event.controllerNumber);
        numbers.insert(event.controllerNumber);
    }
    EXPECT_EQ(numbers.size(), 108u);
    EXPECT_FALSE(muse::mpe::isSafeMidiController(-1));
    EXPECT_FALSE(muse::mpe::isSafeMidiController(128));
}

TEST(MidiControllerPlaybackTests, NativeInterpolationKeepsExplicitArrivalEaseAndOutgoingJump)
{
    AutomationCurve curve { {0, point(0.0)}, {128, point(0.9, 1.0, 0.25, 0.8)} };
    const auto samples = midiControllerSamples(curve, 128);
    ASSERT_TRUE(samples.contains(32));
    EXPECT_EQ(samples.at(32), std::lround(0.8 * 127));
    EXPECT_EQ(samples.at(128), std::lround(0.9 * 127));
    curve = { {0, point(0.2)}, {128, point(0.9, 0.4)} };
    EXPECT_EQ(midiControllerSamples(curve, 128).at(64), std::lround(0.3 * 127));
    curve = { {0, point(0.0)}, {128, point(1.0)} };
    const auto stepped = midiControllerSamples(curve, 128);
    ASSERT_EQ(stepped.size(), 2u);
    EXPECT_EQ(stepped.at(0), 0);
    EXPECT_EQ(stepped.at(128), 127);
}

TEST(MidiControllerPlaybackTests, PedalSwitchesHoldSavedBytesWithoutIntermediateRamp)
{
    AutomationData data;
    data.setCurves({ {AutomationCurveKey::midiLane(piano, "cc:64"), { {0, point(0.0)}, {480, point(1.0, 1.0)}, {960, point(0.0, 0.0)} }} });
    const auto events = renderMidiControllerAutomation(data, piano, {0}, {}, timeline(), true, 960);
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(controller(events.at(0).front()).val.raw(), 0.0f);
    EXPECT_EQ(controller(events.at(500000).front()).val.raw(), 1.0f);
    EXPECT_EQ(controller(events.at(1000000).front()).val.raw(), 0.0f);
}

TEST(MidiControllerPlaybackTests, ActualTempoTimelineConvertsTempoChangePauseAndEveryPartLayer)
{
    TempoTimeline tempo;
    tempo.rebuild(TempoValues { {0, 2.0}, {480, 1.0} }, { {960, 2.0} });
    AutomationData data;
    data.setCurves({ {AutomationCurveKey::midiLane(piano, "cc:7"), { {0, point(0.0)}, {480, point(0.5)}, {960, point(1.0)} }} });
    const std::set<muse::mpe::layer_idx_t> layers {0,1,2,3,4,5,6,7};
    const auto events = renderMidiControllerAutomation(data, piano, layers, {}, tempo, true, 960);
    EXPECT_TRUE(events.contains(0));
    EXPECT_TRUE(events.contains(500000));
    EXPECT_TRUE(events.contains(3500000));
    for (const auto& [timestamp, list] : events) {
        EXPECT_EQ(list.size(), 8u);
        std::set<muse::mpe::layer_idx_t> actual;
        for (const auto& value : list) actual.insert(controller(value).layerIdx);
        EXPECT_EQ(actual, layers);
    }
    EXPECT_TRUE(renderMidiControllerAutomation(data, other, layers, {}, tempo, true, 960).empty());
    EXPECT_TRUE(renderMidiControllerAutomation(data, piano, {}, {}, tempo, true, 960).empty());
}

TEST(MidiControllerPlaybackTests, ExpandedVersusFlattenedVoltaAndJumpUseFirstOccurrenceAndRealTimestamps)
{
    const std::vector<RepeatSegmentInfo> segments { {0,960,0}, {0,480,960}, {960,1440,1440}, {480,960,1920} };
    AutomationCurve curve { {0,point(0.1)}, {480,point(0.2)}, {960,point(0.9)}, {1440,point(0.3)}, {1800,point(0.4)}, {1920,point(0.8)}, {2400,point(0.5)} };
    const auto flattened = midiControllerPlaybackCurve(curve, segments, false);
    EXPECT_EQ(flattened.at(0).value.outValue.raw(), 0.1);
    EXPECT_EQ(flattened.at(480).value.outValue.raw(), 0.2);
    EXPECT_EQ(flattened.at(960).value.outValue.raw(), 0.3);
    EXPECT_EQ(flattened.at(1320).value.outValue.raw(), 0.4);
    AutomationData data;
    data.setCurves({ {AutomationCurveKey::midiLane(piano, "cc:7"), curve} });
    TempoTimeline expanded;
    expanded.rebuild(TempoValues { {0,2.0}, {1440,1.0} }, {});
    TempoTimeline flat;
    flat.rebuild(TempoValues { {0,2.0}, {960,1.0} }, {});
    const auto withRepeats = renderMidiControllerAutomation(data, piano, {0}, segments, expanded, true, 2400);
    const auto withoutRepeats = renderMidiControllerAutomation(data, piano, {0}, segments, flat, false, 1440);
    EXPECT_EQ(controller(withRepeats.at(1500000).front()).val.raw(), float(std::lround(0.3*127))/127.0f);
    EXPECT_EQ(controller(withRepeats.at(2500000).front()).val.raw(), float(std::lround(0.8*127))/127.0f);
    EXPECT_EQ(controller(withoutRepeats.at(1000000).front()).val.raw(), float(std::lround(0.3*127))/127.0f);
    EXPECT_EQ(controller(withoutRepeats.at(1750000).front()).val.raw(), float(std::lround(0.4*127))/127.0f);
}

TEST(MidiControllerPlaybackTests, ReplacingAndDeletingControllersPreservesNotesAndPrecedesCoincidentOnsets)
{
    muse::mpe::PlaybackEventsMap events;
    events[0].emplace_back(muse::mpe::NoteEvent());
    ControllerChangeEvent old; old.type = ControllerChangeEvent::GenericMidiController; old.controllerNumber = 7; old.val = 0;
    events[0].emplace_back(old);
    events[50].emplace_back(old);
    ControllerChangeEvent liveInput; liveInput.type = ControllerChangeEvent::Modulation; liveInput.val = 0.5;
    events[0].emplace_back(liveInput);
    auto updated = old; updated.val = 1;
    muse::mpe::PlaybackEventsMap replacement; replacement[0].emplace_back(updated);
    EXPECT_TRUE(replaceMidiControllerEvents(events, replacement));
    ASSERT_EQ(events.size(), 1u);
    ASSERT_EQ(events.at(0).size(), 3u);
    EXPECT_EQ(controller(events.at(0)[0]).val.raw(), 1.0f);
    EXPECT_TRUE(std::holds_alternative<muse::mpe::NoteEvent>(events.at(0)[1]));
    EXPECT_EQ(controller(events.at(0)[2]).type, ControllerChangeEvent::Modulation);
    EXPECT_TRUE(replaceMidiControllerEvents(events, {}));
    EXPECT_EQ(events.at(0).size(), 2u);
    EXPECT_FALSE(replaceMidiControllerEvents(events, {}));
}

TEST(MidiControllerPlaybackTests, EmptyInvalidAndDenseCurvesNeverEmitDefaultsOrUnsafeNumbers)
{
    EXPECT_TRUE(midiControllerSamples({}, 960).empty());
    auto invalid = point(0.0);
    invalid.value.outValue.raw() = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(midiControllerSamples({ {0,invalid} },960).empty());
    EXPECT_TRUE(midiControllerSamples({ {-1,point(1.0)} },960).empty());
    EXPECT_TRUE(midiControllerSamples({ {std::numeric_limits<int>::max(),point(1.0)} },960).empty());
    AutomationCurve dense;
    for (int tick = 0; tick < 10002; ++tick) dense[tick] = point(double(tick%128)/127.0);
    const auto samples = midiControllerSamples(dense, 10001);
    EXPECT_EQ(samples.size(), 10002u);
    EXPECT_EQ(samples.rbegin()->first, 10001);
    dense = { {0,point(0.0)}, {std::numeric_limits<int>::max(),point(1.0,1.0)} };
    const auto bounded = midiControllerSamples(dense,960);
    EXPECT_EQ(bounded.size(), 1u);
}
