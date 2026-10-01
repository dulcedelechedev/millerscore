/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */

#include "editperformance.h"

#include <algorithm>

#include "../dom/masterscore.h"

using namespace mu::engraving;

ChangePerformanceOverride::ChangePerformanceOverride(Note* note, const EID& eid, const PerformanceNoteOverride& value)
    : m_note(note), m_eid(eid), m_value(value)
{
}

void ChangePerformanceOverride::flip()
{
    PerformanceOverlay& overlay = m_note->masterScore()->performanceOverlay();
    const PerformanceNoteOverride previous = overlay.value(m_eid);
    overlay.set(m_eid, m_value);
    m_value = previous;
}

std::optional<ChangedRange> ChangePerformanceOverride::changedRange() const
{
    // Cover the written note and both the previous and the new performed
    // positions, whichever is currently applied.
    const PerformanceNoteOverride current = m_note->masterScore()->performanceOverlay().value(m_eid);
    const int tick = m_note->tick().ticks();
    const int writtenTicks = m_note->playTicks();

    const auto startOf = [tick](const PerformanceNoteOverride& value) {
        return tick + value.startOffsetTicks.value_or(0);
    };
    const auto endOf = [&startOf, writtenTicks](const PerformanceNoteOverride& value) {
        return startOf(value) + value.playbackDurationTicks.value_or(writtenTicks);
    };

    const int from = std::max(0, std::min({ tick, startOf(current), startOf(m_value) }));
    const int to = std::max({ tick + writtenTicks, endOf(current), endOf(m_value) });

    ChangedRange range;
    range.tickFrom = Fraction::fromTicks(from);
    range.tickTo = Fraction::fromTicks(to);
    range.staffIdxFrom = m_note->staffIdx();
    range.staffIdxTo = m_note->staffIdx();
    return range;
}
