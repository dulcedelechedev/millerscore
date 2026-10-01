/* SPDX-License-Identifier: GPL-3.0-only */
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "engraving/automation/midicontrollercatalog.h"

using namespace mu::engraving;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2 || MIDI_CONTROLLER_CATALOG.size() != 128) return 1;
    for (size_t i = 0; i < MIDI_CONTROLLER_CATALOG.size(); ++i) {
        if (MIDI_CONTROLLER_CATALOG[i].number != i) return 1;
    }
    const QStringList categories { "bankAndProgram", "performance", "generalPurpose", "pedalAndSwitch", "soundController",
                                   "effects", "dataEntry", "parameterSelection", "undefined", "channelMode" };
    const QStringList behaviors { "continuous", "switch", "stepped", "trigger", "channelMode" };
    const QStringList risks { "none", "bankState", "noteState", "parameterState", "canSilenceNotes", "resetsControllers", "changesChannelMode" };
    const QStringList safety { "safe", "conditional", "stateful", "destructive", "restricted" };
    const auto str = [](std::string_view value) { return QString::fromUtf8(value.data(), qsizetype(value.size())); };
    QJsonArray controllers;
    for (const auto& info : MIDI_CONTROLLER_CATALOG) {
        controllers.append(QJsonObject {
            { "number", info.number }, { "stableId", QStringLiteral("cc:%1").arg(info.number) },
            { "name", str(info.canonicalName) }, { "category", categories[int(info.category)] },
            { "minimum", info.minimum }, { "maximum", info.maximum }, { "defaultValue", info.defaultValue },
            { "defaultMeaning", "Empty-lane editing default; never emitted when viewing" },
            { "wireByteBits", 7 }, { "resolution", info.resolution == MidiControllerResolution::SevenBit ? 7 : 14 },
            { "pairedMsb", info.pairedMsb }, { "pairedLsb", info.pairedLsb },
            { "behavior", behaviors[int(info.behavior)] }, { "risk", risks[int(info.risk)] },
            { "editSafety", safety[int(info.editSafety())] }, { "editable", info.isCurveEditable() },
            { "selectable", true }, { "defined", info.isDefined() },
            { "semanticValueDomain", str(info.semanticValueDomain()) }, { "tooltip", str(info.editingTooltip()) },
            { "displayFormat", info.behavior == MidiControllerBehavior::Switch ? "Off/On at byte64" : "Rounded 0-127 byte" },
            { "persistence", "AutomationType::MidiLane, instrument(partId,instrumentId), laneId=cc:N, normalized JSON outValue/inValue" },
            { "backendRouting", QJsonObject { {"SoundFont", info.isPlaybackRouted()}, {"VST3", false}, {"MuseSounds", false}, {"externalMidi", false} } },
            { "testStatus", QJsonObject {
                {"registryAndIdentity", "notRun"}, {"serializationRoundTrip", "notRun"},
                {"dataEditMoveDelete", "notRun"}, {"dataTrackIsolation", "notRun"},
                {"readOnlyNoMutation", "notRun"}, {"selectorFunctions", "notRun"},
                {"guiGestures", "notRun"}, {"scoreSaveCloseReopen", "notRun"},
                {"scoreUndoRedo", "notRun"}, {"SMFImportExport", "unsupported"},
                {"tickChannelDispatch", info.isPlaybackRouted() ? "notRun" : "blockedProtectedController"}, {"backendAudible", "notRun"}
            } },
            { "limitations", "Safe CC bytes dispatched only to MS Basic/SoundFont; instrument response varies. Muse Sounds/VST3/external MIDI unsupported. Independent paired byte lanes; no semantic sequence editor. GUI/score lifecycle pending." }
        });
    }
    QFile out(QString::fromLocal8Bit(argv[1]));
    if (!out.open(QIODevice::WriteOnly)) return 2;
    const QJsonObject root {
        { "schemaVersion", 1 }, { "registryCount", controllers.size() },
        { "nameReference", "https://midi.org/midi-1-0-control-change-messages" },

        { "controllers", controllers }
    };
    out.write(QJsonDocument(root).toJson());
}
