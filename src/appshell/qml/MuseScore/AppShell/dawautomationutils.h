/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <QRegularExpression>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <limits>

#include "engraving/automation/automationtypes.h"
#include "engraving/automation/midicontrollercatalog.h"

namespace mu::appshell::dawautomation {

inline std::optional<engraving::AutomationCurveKey> channelAutomationKey(const QString& control,
                                                                        const engraving::InstrumentTrackId& trackId)
{
    if (!trackId.isValid()) return std::nullopt;
    if (control == QStringLiteral("channelVolume")) {
        return engraving::AutomationCurveKey::instrument(engraving::AutomationType::Volume, trackId);
    }
    if (control == QStringLiteral("channelPan")) {
        return engraving::AutomationCurveKey::instrument(engraving::AutomationType::Pan, trackId);
    }
    if (control == QStringLiteral("channelPitch")) {
        return engraving::AutomationCurveKey::instrument(engraving::AutomationType::Pitch, trackId);
    }
    if (engraving::midiControllerNumber(control.toStdString())) {
        return engraving::AutomationCurveKey::midiLane(trackId, control.toStdString());
    }
    static const QRegularExpression MIDI_LANE_ID(QStringLiteral(
        "^(pitchBend|channelPressure|programChange|bankSelect|polyPressure:[^:]+|rpn:\\d{1,3}:\\d{1,3}|nrpn:\\d{1,3}:\\d{1,3})$"));
    if (MIDI_LANE_ID.match(control).hasMatch()) {
        return engraving::AutomationCurveKey::midiLane(trackId, control.toStdString());
    }
    return std::nullopt;
}

inline bool isChannelAutomationEditable(const engraving::AutomationCurveKey& key)
{
    if (key.type != engraving::AutomationType::MidiLane) return true;
    const auto cc = engraving::midiControllerNumber(key.laneId);
    return cc && engraving::MIDI_CONTROLLER_CATALOG[*cc].isCurveEditable();
}

inline std::optional<int> automationInteger(const QVariant& value, int minimum, int maximum)
{
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number) || number < minimum || number > maximum
        || number != std::floor(number)) return std::nullopt;
    return int(number);
}

inline std::optional<int> automationTick(const QVariant& value)
{
    return automationInteger(value, 0, std::numeric_limits<int>::max());
}

inline std::optional<QMap<QString, int>> automationIntegerValues(const QVariantMap& values, int minimum, int maximum)
{
    QMap<QString, int> result;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const auto number = automationInteger(it.value(), minimum, maximum);
        if (!number) return std::nullopt;
        result.insert(it.key(), *number);
    }
    return result;
}

// Validate the whole gesture before opening a score transaction. A malformed
// trailing edit must never leave an earlier valid point partially applied.
inline std::optional<engraving::AutomationPointEdits> channelAutomationEdits(const QVariantList& edits)
{
    if (edits.isEmpty()) return std::nullopt;
    engraving::AutomationPointEdits result;
    result.reserve(size_t(edits.size()));
    for (const QVariant& item : edits) {
        const QVariantMap edit = item.toMap();
        const QString op = edit.value("op").toString();
        const auto tick = automationTick(edit.value("tick"));
        if (!tick) return std::nullopt;
        if (op == "erase") {
            result.push_back({ *tick, engraving::AutomationPointEdit::ErasePoint {} });
            continue;
        }
        bool valueOk = false;
        const double value = edit.value("value").toDouble(&valueOk);
        if (!valueOk || !std::isfinite(value) || value < 0.0 || value > 1.0) return std::nullopt;
        engraving::AutomationPoint point;
        point.value.outValue = value;
        const QString shape = edit.value("shape", QStringLiteral("linear")).toString();
        if (shape != "linear" && shape != "curve" && shape != "step") return std::nullopt;
        if (shape == "step") {
            point.value.inValue = engraving::AutomationPoint::ArrivalFromPrevious {};
        } else {
            bool arrivalOk = false;
            const double arrival = edit.value("arrival", value).toDouble(&arrivalOk);
            if (!arrivalOk || !std::isfinite(arrival) || arrival < 0.0 || arrival > 1.0) return std::nullopt;
            engraving::AutomationPoint::Ease ease = engraving::AutomationPoint::Ease::none();
            if (shape == "curve") {
                bool bendOk = false;
                const double bend = edit.value("bend", 0.5).toDouble(&bendOk);
                if (!bendOk || !std::isfinite(bend)) return std::nullopt;
                bool bendTimeOk = false;
                const double bendTime = edit.value("bendTime", 0.5).toDouble(&bendTimeOk);
                if (!bendTimeOk || !std::isfinite(bendTime) || bendTime < 0.0 || bendTime > 1.0) return std::nullopt;
                ease.t = bendTime;
                ease.value = std::clamp(bend, 0.0, 1.0);
            }
            point.value.inValue = engraving::AutomationPoint::ExplicitArrival { arrival, ease };
        }
        if (op == "move") {
            const auto from = automationTick(edit.value("from"));
            if (!from) return std::nullopt;
            result.push_back({ *tick, engraving::AutomationPointEdit::MovePoint { point, *from } });
        } else if (op == "set") {
            result.push_back({ *tick, engraving::AutomationPointEdit::SetPoint { point } });
        } else {
            return std::nullopt;
        }
    }
    return result;
}
}
