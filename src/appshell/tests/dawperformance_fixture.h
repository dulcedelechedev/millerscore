/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

// Actual numeric edit methods run with a deterministic override sink. This
// fixture does not construct score notes, playback services, or the UndoStack.
#include <QMap>
#include <QStringList>
#include <QVariant>
#include <functional>
#include "engraving/dom/performanceoverlay.h"
#include "global/types/translatablestring.h"
#include "appshell/qml/MuseScore/AppShell/dawautomationutils.h"

namespace mu::appshell {
class PerformanceModelFixture
{
public:
    enum class EditMode { Notation, Performance };
    EditMode m_editMode = EditMode::Performance;
    QMap<QString, engraving::PerformanceNoteOverride> values;
    int writes = 0;

    bool setPerformanceVelocity(const QString&, int);
    bool setPerformanceStartOffset(const QString&, int);
    bool setPerformanceVelocities(const QVariantMap&);
    bool setPerformancePitchOffsets(const QVariantMap&);
    bool setPerformanceStartOffsets(const QVariantMap&);
    bool updatePerformanceOverrides(const QStringList& ids, const muse::TranslatableString&,
                                   const std::function<void(const QString&, engraving::PerformanceNoteOverride&)>& update)
    {
        if (ids.isEmpty()) return false;
        ++writes;
        for (const QString& id : ids) update(id, values[id]);
        return true;
    }
    bool updatePerformanceOverride(const QString& id, const muse::TranslatableString&,
                                  const std::function<void(engraving::PerformanceNoteOverride&)>& update)
    {
        ++writes;
        update(values[id]);
        return true;
    }
};
}
