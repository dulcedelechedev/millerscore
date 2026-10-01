/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */

#pragma once

#include "transaction/undoablecommand.h"

#include "../dom/note.h"
#include "../dom/performanceoverlay.h"

namespace mu::engraving {
//! Changes one note's playback-only override. It lives on the score's undo
//! stack, so notation and performance edits share one history, and reports the
//! affected range so playback re-renders only that region.
class ChangePerformanceOverride : public UndoableCommand
{
    OBJECT_ALLOCATOR(engraving, ChangePerformanceOverride)

    Note* m_note = nullptr;
    EID m_eid;
    PerformanceNoteOverride m_value;

    void flip() override;

public:
    ChangePerformanceOverride(Note* note, const EID& eid, const PerformanceNoteOverride& value);

    UNDO_NAME("ChangePerformanceOverride")
    UNDO_CHANGED_OBJECTS({ m_note })

    std::optional<ChangedRange> changedRange() const override;
};
}
