/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */

#pragma once

#include <optional>
#include <unordered_map>

#include "../infrastructure/eid.h"

namespace mu::engraving {
//! Sparse playback-only adjustments for a score note. Every field is optional;
//! an absent field means "play as written". The note itself is untouched.
struct PerformanceNoteOverride {
    std::optional<int> velocity;              // MIDI semantic 1..127
    std::optional<int> startOffsetTicks;      // signed, relative to the written onset
    std::optional<int> playbackDurationTicks; // positive sounding length
    std::optional<int> pitchOffsetCents;      // signed playback-only offset, -1200..1200

    bool empty() const
    {
        return !velocity.has_value() && !startOffsetTicks.has_value() && !playbackDurationTicks.has_value()
               && !pitchOffsetCents.has_value();
    }

    bool operator==(const PerformanceNoteOverride& other) const = default;
};

//! Performance layer of a master score, keyed by the note's persistent EID.
//! It is not part of the engraving DOM or the score file; the DAW project
//! payload persists it, and playback rendering composes it with notation.
class PerformanceOverlay
{
public:
    using Map = std::unordered_map<EID, PerformanceNoteOverride>;

    const PerformanceNoteOverride* find(const EID& eid) const
    {
        const auto it = m_overrides.find(eid);
        return it == m_overrides.end() ? nullptr : &it->second;
    }

    PerformanceNoteOverride value(const EID& eid) const
    {
        const PerformanceNoteOverride* found = find(eid);
        return found ? *found : PerformanceNoteOverride();
    }

    //! Setting an empty override removes the entry, keeping the layer sparse.
    void set(const EID& eid, const PerformanceNoteOverride& value)
    {
        if (value.empty()) {
            m_overrides.erase(eid);
        } else {
            m_overrides.insert_or_assign(eid, value);
        }
    }

    const Map& overrides() const { return m_overrides; }
    bool empty() const { return m_overrides.empty(); }
    void clear() { m_overrides.clear(); }

private:
    Map m_overrides;
};
}
