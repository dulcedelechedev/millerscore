/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include "engraving/automation/automationdata.h"
#include "engraving/automation/midicontrollercatalog.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/tempotimeline.h"
#include "mpe/events.h"

namespace mu::engraving {
// Automation is persisted in expanded uticks. A flattened playback uses the
// first occurrence of each score tick, just like the flattened tempo curve.
inline AutomationCurve midiControllerPlaybackCurve(const AutomationCurve& curve,
                                                   const std::vector<RepeatSegmentInfo>& segments,
                                                   bool expandRepeats)
{
    if (expandRepeats) return curve;
    AutomationCurve result;
    size_t segmentIndex = 0;
    for (const auto& [utick, point] : curve) {
        while (segmentIndex < segments.size()
               && utick >= segments[segmentIndex].utick + segments[segmentIndex].endTick - segments[segmentIndex].tick) {
            if (segmentIndex + 1 == segments.size()
                && utick == segments[segmentIndex].utick + segments[segmentIndex].endTick - segments[segmentIndex].tick) break;
            ++segmentIndex;
        }
        if (segmentIndex == segments.size()) break;
        const RepeatSegmentInfo& segment = segments[segmentIndex];
        if (utick < segment.utick) continue;
        result.emplace(utick - segment.utick + segment.tick, point);
    }
    return result;
}

inline bool validMidiControllerPoint(const AutomationPoint& point)
{
    const auto valid = [](double value) { return std::isfinite(value) && value >= 0.0 && value <= 1.0; };
    if (!valid(point.value.outValue.raw())) return false;
    if (const auto* arrival = std::get_if<AutomationPoint::ExplicitArrival>(&point.value.inValue)) {
        if (!valid(arrival->value.raw()) || !valid(arrival->ease.t.raw()) || !valid(arrival->ease.value.raw())) return false;
    }
    return true;
}

inline std::map<int, int> midiControllerSamples(const AutomationCurve& curve, int endTick, bool stepped = false)
{
    std::map<int, int> result;
    if (endTick < 0 || curve.empty()) return result;
    for (const auto& [tick, point] : curve) {
        if (tick < 0 || !validMidiControllerPoint(point)) return {};
    }
    if (curve.begin()->first > endTick) return result;

    std::optional<int> previousByte;
    const auto sample = [&](int tick, double value) {
        const int byte = std::clamp(int(std::lround(value * 127.0)), 0, 127);
        if (!previousByte || byte != *previousByte) {
            result.emplace(tick, byte);
            previousByte = byte;
        }
    };
    sample(0, curve.begin()->second.value.outValue.raw());
    constexpr int64_t SAMPLE_INTERVAL_TICKS = 16;
    for (auto it = curve.begin(); it != curve.end() && it->first <= endTick; ++it) {
        sample(it->first, it->second.value.outValue.raw());
        if (stepped) continue;
        const auto next = std::next(it);
        if (next == curve.end()) break;
        const int64_t duration = int64_t(next->first) - it->first;
        const int64_t limit = std::min<int64_t>(next->first, endTick);
        for (int64_t tick = int64_t(it->first) + SAMPLE_INTERVAL_TICKS; tick < limit; tick += SAMPLE_INTERVAL_TICKS) {
            const double factor = double(tick - it->first) / double(duration);
            sample(int(tick), muse::mpe::evaluateAt(next->second.value, it->second.value.outValue, factor).raw());
        }
        if (endTick < next->first && endTick > it->first) {
            const double factor = double(int64_t(endTick) - it->first) / double(duration);
            sample(endTick, muse::mpe::evaluateAt(next->second.value, it->second.value.outValue, factor).raw());
        }
    }
    return result;
}

inline muse::mpe::PlaybackEventsMap renderMidiControllerAutomation(const AutomationData& data,
                                                                  const InstrumentTrackId& trackId,
                                                                  const std::set<muse::mpe::layer_idx_t>& layers,
                                                                  const std::vector<RepeatSegmentInfo>& expandedSegments,
                                                                  const TempoTimeline& timeline,
                                                                  bool expandRepeats, int endTick)
{
    muse::mpe::PlaybackEventsMap result;
    if (!trackId.isValid() || layers.empty()) return result;
    for (const auto& [key, curve] : data.curves()) {
        if (key.type != AutomationType::MidiLane || key.trackId() != trackId) continue;
        const auto controller = midiControllerNumber(key.laneId);
        if (!controller || !MIDI_CONTROLLER_CATALOG[*controller].isCurveEditable()
            || !muse::mpe::isSafeMidiController(*controller)) continue;
        const bool stepped = MIDI_CONTROLLER_CATALOG[*controller].behavior == MidiControllerBehavior::Switch;
        const auto samples = midiControllerSamples(midiControllerPlaybackCurve(curve, expandedSegments, expandRepeats), endTick, stepped);
        for (const auto& [tick, byte] : samples) {
            const muse::mpe::timestamp_t timestamp = timeline.utick2utime(tick) * 1000000;
            for (const auto layer : layers) {
                muse::mpe::ControllerChangeEvent event;
                event.type = muse::mpe::ControllerChangeEvent::GenericMidiController;
                event.controllerNumber = *controller;
                event.val = float(byte) / 127.0f;
                event.layerIdx = layer;
                result[timestamp].emplace_back(event);
            }
        }
    }
    return result;
}

inline bool replaceMidiControllerEvents(muse::mpe::PlaybackEventsMap& destination,
                                       const muse::mpe::PlaybackEventsMap& controllers)
{
    bool changed = !controllers.empty();
    for (auto it = destination.begin(); it != destination.end();) {
        const size_t before = it->second.size();
        std::erase_if(it->second, [](const muse::mpe::PlaybackEvent& event) {
            const auto* controller = std::get_if<muse::mpe::ControllerChangeEvent>(&event);
            return controller && controller->type == muse::mpe::ControllerChangeEvent::GenericMidiController;
        });
        changed |= it->second.size() != before;
        if (it->second.empty()) it = destination.erase(it);
        else ++it;
    }
    for (const auto& [timestamp, events] : controllers) {
        auto& list = destination[timestamp];
        // Apply controllers before notes starting at the same timestamp.
        list.insert(list.begin(), events.begin(), events.end());
    }
    return changed;
}
}
