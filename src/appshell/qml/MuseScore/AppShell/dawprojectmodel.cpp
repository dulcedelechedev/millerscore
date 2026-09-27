/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */

#include "dawprojectmodel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>
#include <cmath>

#include "project/inotationproject.h"
#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationselection.h"
#include "notation/inotationundostack.h"
#include "notation/inotationautomation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/drumset.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/score.h"
#include "engraving/editing/editnote.h"
#include "engraving/editing/editperformance.h"
#include "engraving/editing/noteinput.h"
#include "engraving/editing/transaction/transaction.h"
#include "engraving/infrastructure/eid.h"
#include "engraving/automation/midicontrollercatalog.h"

using namespace mu;
using namespace mu::appshell;

namespace {
constexpr int MIN_NOTE_DURATION = 1;
constexpr double MIN_TEMPO = 20.0;
constexpr double MAX_TEMPO = 400.0;

std::optional<engraving::AutomationCurveKey> channelAutomationKey(const QString& control,
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

    static const QRegularExpression CC_ID(QStringLiteral("^cc:(\\d{1,3})$"));
    const QRegularExpressionMatch ccMatch = CC_ID.match(control);
    if (ccMatch.hasMatch() && ccMatch.captured(1).toInt() <= 127) {
        return engraving::AutomationCurveKey::midiLane(trackId, control.toStdString());
    }

    static const QRegularExpression MIDI_LANE_ID(QStringLiteral(
        "^(pitchBend|channelPressure|programChange|bankSelect|polyPressure:[^:]+|rpn:\\d{1,3}:\\d{1,3}|nrpn:\\d{1,3}:\\d{1,3})$"));
    if (MIDI_LANE_ID.match(control).hasMatch()) {
        return engraving::AutomationCurveKey::midiLane(trackId, control.toStdString());
    }
    return std::nullopt;
}

engraving::InstrumentTrackId instrumentTrackId(const QString& partId, const QString& instrumentId)
{
    return { muse::ID(partId), muse::String::fromQString(instrumentId) };
}

bool isSerializedEid(const QString& value)
{
    const qsizetype separator = value.indexOf('_');
    if (separator <= 0 || separator != value.lastIndexOf('_') || separator >= value.size() - 1
        || separator > qsizetype(engraving::EID::MAX_UINT64_BASE64_SIZE)
        || value.size() - separator - 1 > qsizetype(engraving::EID::MAX_UINT64_BASE64_SIZE)) return false;
    for (qsizetype i = 0; i < value.size(); ++i) {
        if (i == separator) continue;
        const QChar c = value.at(i);
        const ushort u = c.unicode();
        if (!((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || u == '+' || u == '/')) return false;
    }
    return true;
}

template<typename T>
void sortByStartTick(QList<T>& items)
{
    std::stable_sort(items.begin(), items.end(), [](const T& left, const T& right) {
        return left.startTick < right.startTick;
    });
}
}

DawProjectModel::DawProjectModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void DawProjectModel::load()
{
    loadFromCurrentProject();
    globalContext()->currentProjectChanged().onNotify(this, [this]() { loadFromCurrentProject(); });
    connect(this, &DawProjectModel::projectChanged, this, &DawProjectModel::storeInCurrentProject, Qt::UniqueConnection);
}

void DawProjectModel::loadFromCurrentProject()
{
    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (!project) {
        m_loadingProject = true;
        clear();
        m_loadingProject = false;
        return;
    }

    m_loadingProject = true;
    if (project->dawData().empty()) {
        clear();
    } else {
        if (!deserialize(QString::fromUtf8(project->dawData().toQByteArray()))) {
            clear();
        }
    }
    m_loadingProject = false;
    subscribeToScoreChanges();
    emit scoreProjectionChanged();
}

void DawProjectModel::storeInCurrentProject()
{
    if (m_loadingProject) return;
    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (!project) return;
    // Ordinary scores stay free of DAW data until the user creates some.
    const engraving::PerformanceOverlay* overlay = performanceOverlay();
    if (project->dawData().empty() && m_tracks.isEmpty() && (!overlay || overlay->empty())) return;
    // Audio tracks live in the same daw.json, written by DawAudioTracksModel: keep them.
    QJsonObject stored = QJsonDocument::fromJson(serialize().toUtf8()).object();
    const QJsonObject previous = QJsonDocument::fromJson(project->dawData().toQByteArrayNoCopy()).object();
    if (previous.contains("audioTracks")) {
        stored.insert("audioTracks", previous.value("audioTracks"));
    }
    project->setDawData(muse::ByteArray::fromQByteArray(QJsonDocument(stored).toJson(QJsonDocument::Compact)));
}

QVariant DawProjectModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size()) {
        return {};
    }
    const Track& track = m_tracks.at(index.row());
    switch (role) {
    case TrackIdRole: return track.id;
    case NameRole: return track.name;
    case ColorRole: return track.color;
    case MutedRole: return track.muted;
    case SoloRole: return track.solo;
    case ArmedRole: return track.armed;
    case RegionsRole: return regionsForQml(track);
    default: return {};
    }
}

int DawProjectModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : m_tracks.size(); }
int DawProjectModel::trackCount() const { return m_tracks.size(); }

QHash<int, QByteArray> DawProjectModel::roleNames() const
{
    return { { TrackIdRole, "trackId" }, { NameRole, "name" }, { ColorRole, "trackColor" },
        { MutedRole, "muted" }, { SoloRole, "solo" }, { ArmedRole, "armed" }, { RegionsRole, "regions" } };
}

int DawProjectModel::playheadTick() const { return m_playheadTick; }
void DawProjectModel::setPlayheadTick(int tick)
{
    tick = std::max(0, tick);
    if (m_playheadTick == tick) return;
    m_playheadTick = tick;
    emit playheadTickChanged();
}

double DawProjectModel::tempo() const { return m_tempo; }
void DawProjectModel::setTempo(double tempo)
{
    if (!std::isfinite(tempo)) return;
    tempo = std::clamp(tempo, MIN_TEMPO, MAX_TEMPO);
    if (qFuzzyCompare(m_tempo, tempo)) return;
    m_tempo = tempo;
    emit tempoChanged();
    emit projectChanged();
}

int DawProjectModel::timeSignatureNumerator() const { return m_timeSignatureNumerator; }
void DawProjectModel::setTimeSignatureNumerator(int numerator)
{
    numerator = std::clamp(numerator, 1, 32);
    if (m_timeSignatureNumerator == numerator) return;
    m_timeSignatureNumerator = numerator;
    emit timeSignatureChanged();
    emit projectChanged();
}

int DawProjectModel::timeSignatureDenominator() const { return m_timeSignatureDenominator; }
void DawProjectModel::setTimeSignatureDenominator(int denominator)
{
    if (denominator < 1 || denominator > 64 || (denominator & (denominator - 1)) != 0) return;
    if (m_timeSignatureDenominator == denominator) return;
    m_timeSignatureDenominator = denominator;
    emit timeSignatureChanged();
    emit projectChanged();
}

int DawProjectModel::snapTicks() const { return m_snapTicks; }
void DawProjectModel::setSnapTicks(int ticks)
{
    ticks = std::max(1, ticks);
    if (m_snapTicks == ticks) return;
    m_snapTicks = ticks;
    emit snapTicksChanged();
}

QString DawProjectModel::selectedRegionId() const { return m_selectedRegionId; }
QString DawProjectModel::selectedNoteId() const { return m_selectedNoteId; }

DawProjectModel::EditMode DawProjectModel::editMode() const { return m_editMode; }

void DawProjectModel::setEditMode(EditMode mode)
{
    if (m_editMode == mode) return;
    m_editMode = mode;
    emit editModeChanged();
}

QString DawProjectModel::selectedProjectedNoteId() const { return m_selectedProjectedNoteId; }
QStringList DawProjectModel::selectedProjectedNoteIds() const { return m_selectedProjectedNoteIds; }

QVariantList DawProjectModel::projectedScoreNotes(int startTrack, int endTrack)
{
    QVariantList result;
    m_projectedNoteRefs.clear();
    int sessionOrdinal = 0;
    appendProjectedNotes(startTrack, endTrack, 0, result, sessionOrdinal);
    return result;
}

QVariantList DawProjectModel::projectedScoreNotesForRanges(const QVariantList& ranges)
{
    QVariantList result;
    // Cleared once: the note references of every range stay resolvable together.
    m_projectedNoteRefs.clear();
    int sessionOrdinal = 0;
    for (int index = 0; index < ranges.size(); ++index) {
        const QVariantMap range = ranges.at(index).toMap();
        appendProjectedNotes(range.value("startTrack").toInt(), range.value("endTrack").toInt(), index, result, sessionOrdinal);
    }
    return result;
}

void DawProjectModel::appendProjectedNotes(int startTrack, int endTrack, int rangeIndex, QVariantList& result, int& sessionOrdinal)
{
    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (!project || !project->masterNotation() || !project->masterNotation()->masterScore()
        || startTrack < 0 || endTrack <= startTrack) {
        return;
    }

    engraving::MasterScore* score = project->masterNotation()->masterScore();
    const engraving::track_idx_t firstTrack = engraving::track_idx_t(startTrack);
    const engraving::track_idx_t lastTrack = std::min(engraving::track_idx_t(endTrack), score->ntracks());
    for (engraving::Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
        for (engraving::Segment* segment = measure->first(engraving::SegmentType::ChordRest); segment;
             segment = segment->next(engraving::SegmentType::ChordRest)) {
            for (engraving::track_idx_t track = firstTrack; track < lastTrack; ++track) {
                engraving::EngravingItem* item = segment->element(track);
                if (!item || !item->isChord()) {
                    continue;
                }
                for (engraving::Note* note : static_cast<engraving::Chord*>(item)->notes()) {
                    // A tie chain sounds once; its continuation fragments are
                    // represented by the head note's duration.
                    if (note->tieBackNonPartial()) {
                        continue;
                    }
                    const engraving::EID eid = note->eid();
                    const QString id = eid.isValid()
                                       ? QString::fromStdString(eid.toStdString())
                                       : QStringLiteral("session:%1:%2:%3:%4")
                                       .arg(note->tick().ticks()).arg(int(note->track())).arg(note->pitch()).arg(sessionOrdinal++);
                    m_projectedNoteRefs.insert(id, note);
                    QVariantMap projected = projectedNoteForQml(note, id);
                    projected.insert("rangeIndex", rangeIndex);
                    result.append(projected);
                }
            }
        }
    }
}

bool DawProjectModel::selectProjectedNote(const QString& noteId)
{
    if (!noteId.isEmpty() && !resolveProjectedNote(noteId)) return false;
    const QStringList ids = noteId.isEmpty() ? QStringList() : QStringList { noteId };
    if (m_selectedProjectedNoteIds == ids) return true;
    return setProjectedNoteSelection(ids, noteId);
}

bool DawProjectModel::setProjectedNoteSelection(const QStringList& noteIds, const QString& primaryId)
{
    QStringList ids;
    std::vector<engraving::EngravingItem*> items;
    engraving::Note* primaryNote = nullptr;
    for (const QString& id : noteIds) {
        engraving::Note* note = ids.contains(id) ? nullptr : resolveProjectedNote(id);
        if (!note) continue;
        ids.append(id);
        items.push_back(note);
        if (id == primaryId) primaryNote = note;
    }
    if (!noteIds.isEmpty() && ids.isEmpty()) return false;
    if (!primaryNote && !items.empty()) primaryNote = engraving::toNote(items.back());

    setProjectedSelection(ids, primaryId);

    // The score selection follows, so the notation's own commands act on the same notes.
    // REPLACE, not ADD: with Ctrl held, adding a single note toggles it instead.
    const project::INotationProjectPtr project = globalContext()->currentProject();
    const notation::INotationPtr notation = project && project->masterNotation() ? project->masterNotation()->notation() : nullptr;
    if (notation && notation->interaction()) {
        m_selectingScoreNotes = true;
        if (items.empty()) {
            notation->interaction()->clearSelection();
        } else if (items.size() == 1) {
            notation->interaction()->select(items, engraving::SelectType::SINGLE, primaryNote->staffIdx());
        } else {
            notation->interaction()->select(items, engraving::SelectType::REPLACE, primaryNote->staffIdx());
        }
        m_selectingScoreNotes = false;
    }
    return true;
}

void DawProjectModel::setProjectedSelection(const QStringList& noteIds, const QString& primaryId)
{
    const QString primary = noteIds.contains(primaryId) ? primaryId : (noteIds.isEmpty() ? QString() : noteIds.last());
    if (noteIds == m_selectedProjectedNoteIds && primary == m_selectedProjectedNoteId) return;
    m_selectedProjectedNoteIds = noteIds;
    m_selectedProjectedNoteId = primary;
    emit projectedSelectionChanged();
}

QString DawProjectModel::addScoreNote(int startTick, int durationTicks, int pitch, int track)
{
    if (m_editMode != EditMode::Notation) {
        return tr("Switch to Notes mode to add notes to the score.");
    }
    if (startTick < 0 || durationTicks < 1 || pitch < 0 || pitch > 127 || track < 0) {
        return tr("This position is outside the score.");
    }

    const project::INotationProjectPtr project = globalContext()->currentProject();
    const notation::INotationPtr notation = project && project->masterNotation() ? project->masterNotation()->notation() : nullptr;
    engraving::MasterScore* score = project && project->masterNotation() ? project->masterNotation()->masterScore() : nullptr;
    if (!score || !notation || !notation->undoStack()) {
        return tr("No score is open.");
    }

    const notation::INotationUndoStackPtr undoStack = notation->undoStack();
    undoStack->prepareChanges(muse::TranslatableString("undoableAction", "Add note from piano roll"));
    QString error;
    engraving::Note* inserted = insertScoreNote(score, startTick, durationTicks, pitch, track, error);
    if (!inserted) {
        undoStack->rollbackChanges();
        return error;
    }
    undoStack->commitChanges();
    notation->notationChanged().send(muse::RectF());

    const QString insertedId = persistentIdFor(inserted);
    setProjectedSelection(insertedId.isEmpty() ? QStringList() : QStringList { insertedId }, insertedId);
    emit scoreProjectionChanged();
    return {};
}

engraving::Note* DawProjectModel::insertScoreNote(engraving::MasterScore* score, int startTick, int durationTicks, int pitch,
                                                  int track, QString& error)
{
    const auto refuse = [&error](const QString& reason) -> engraving::Note* {
        error = reason;
        return nullptr;
    };
    if (startTick < 0 || durationTicks < 1 || pitch < 0 || pitch > 127 || track < 0) {
        return refuse(tr("This position is outside the score."));
    }

    // Always voice 1 of the staff that contains the requested track.
    const engraving::staff_idx_t staffIndex = engraving::track2staff(engraving::track_idx_t(track));
    const engraving::track_idx_t targetTrack = engraving::staff2track(staffIndex);
    const engraving::Staff* staff = score->staff(staffIndex);
    const engraving::Fraction tick = engraving::Fraction::fromTicks(startTick);
    engraving::Measure* measure = staff ? score->tick2measure(tick) : nullptr;
    if (!staff || !measure || tick < measure->tick() || tick >= measure->endTick()) {
        return refuse(tr("There is no measure at this position."));
    }
    if (staff->isTabStaff(tick)) {
        return refuse(tr("Tablature is edited in Score mode for now."));
    }
    if (staff->timeStretch(tick) != engraving::Fraction(1, 1)) {
        return refuse(tr("Measures with a local time signature are edited in Score mode for now."));
    }
    const engraving::Instrument* instrument = staff->part() ? staff->part()->instrument(tick) : nullptr;
    if (instrument && instrument->useDrumset() && (!instrument->drumset() || !instrument->drumset()->isValid(pitch))) {
        return refuse(tr("This percussion kit has no instrument on that row."));
    }

    engraving::ChordRest* target = score->findCR(tick, targetTrack);
    if (!target || target->tick() > tick || target->endTick() <= tick) {
        return refuse(tr("There is no rhythm at this position."));
    }
    if (target->tuplet()) {
        return refuse(tr("Notes inside tuplets are edited in Score mode for now."));
    }
    if (target->isChord() && target->tick() != tick) {
        return refuse(tr("A note is already sounding here. Pick a free beat or shorten that note first."));
    }
    if (!target->isChord() && !target->isRest()) {
        return refuse(tr("This measure cannot take new notes from the piano roll."));
    }

    // This editor does not create cross-measure ties yet. Refuse the exact
    // request instead of silently shortening it at the barline.
    const engraving::Fraction requestedEnd = tick + engraving::Fraction::fromTicks(durationTicks);
    if (requestedEnd > measure->endTick()) {
        return refuse(tr("The selected duration crosses a measure boundary. Use Score mode to create the tie."));
    }

    // Never overwrite or silently truncate at following notes or tuplets.
    for (engraving::Segment* segment = measure->first(engraving::SegmentType::ChordRest); segment;
         segment = segment->next(engraving::SegmentType::ChordRest)) {
        if (segment->tick() <= tick) {
            continue;
        }
        if (segment->tick() >= requestedEnd) {
            break;
        }
        const engraving::EngravingItem* item = segment->element(targetTrack);
        if (item && (item->isChord() || (item->isChordRest() && static_cast<const engraving::ChordRest*>(item)->tuplet()))) {
            return refuse(tr("Another note begins before this duration ends. Shorten the new note first."));
        }
    }
    const engraving::Fraction duration = requestedEnd - tick;

    if (target->isChord()) {
        if (target->endTick() != requestedEnd) {
            return refuse(tr("The existing chord has a different duration. Choose the chord's duration first."));
        }
        for (const engraving::Note* existing : static_cast<engraving::Chord*>(target)->notes()) {
            if (existing->pitch() == pitch) {
                return refuse(tr("This note already exists."));
            }
        }
    }

    // Piano-roll rows are sounding pitch; do not reinterpret them as written pitch.
    const engraving::NoteVal noteValue = engraving::NoteInput::noteVal(score, pitch, staffIndex, false);

    engraving::Note* inserted = nullptr;
    if (target->isChord()) {
        // Same onset as an existing chord: extend the chord, keep its rhythm.
        inserted = engraving::NoteInput::addNote(score->transactionManager()->currentOrDummyTransaction(), score,
                                                 static_cast<engraving::Chord*>(target), noteValue);
    } else {
        if (target->tick() != tick) {
            // Split the rest so a rest begins exactly at the requested onset.
            const engraving::Fraction restStart = target->tick();
            const engraving::Fraction restEnd = target->endTick();
            score->undoRemoveElement(target);
            score->setRests(restStart, targetTrack, tick - restStart, false, nullptr);
            score->setRests(tick, targetTrack, restEnd - tick, false, nullptr);
            target = score->findCR(tick, targetTrack);
        }
        engraving::Segment* result = target && target->isRest() && target->tick() == tick
                                     ? score->setNoteRest(target->segment(), targetTrack, noteValue, duration) : nullptr;
        engraving::EngravingItem* item = result ? result->element(targetTrack) : nullptr;
        if (item && item->isChord() && !static_cast<engraving::Chord*>(item)->notes().empty()) {
            inserted = static_cast<engraving::Chord*>(item)->notes().front();
        }
    }

    if (!inserted) {
        return refuse(tr("The note could not be added here."));
    }
    return inserted;
}

QString DawProjectModel::moveScoreNotes(const QVariantList& moves)
{
    if (m_editMode != EditMode::Notation) {
        return tr("Switch to Notes mode to move notes in the score.");
    }
    const project::INotationProjectPtr project = globalContext()->currentProject();
    const notation::INotationPtr notation = project && project->masterNotation() ? project->masterNotation()->notation() : nullptr;
    engraving::MasterScore* score = project && project->masterNotation() ? project->masterNotation()->masterScore() : nullptr;
    if (!score || !notation || !notation->undoStack() || moves.isEmpty()) {
        return tr("No score is open.");
    }

    struct Move {
        engraving::Note* note = nullptr;
        int tick = 0;
        int pitch = 0;
        int durationTicks = 0;
        int track = 0;
    };
    std::vector<Move> list;
    bool timeChanges = false;
    for (const QVariant& item : moves) {
        const QVariantMap map = item.toMap();
        engraving::Note* note = resolveProjectedNote(map.value("noteId").toString());
        if (!note) {
            return tr("A note to move is no longer in the score.");
        }
        Move move;
        move.note = note;
        move.tick = map.value("tick").toInt();
        move.pitch = map.value("pitch").toInt();
        move.durationTicks = note->chord()->actualTicks().ticks();
        move.track = int(note->track());
        if (move.pitch < 0 || move.pitch > 127 || move.tick < 0) {
            return tr("This position is outside the score.");
        }
        timeChanges = timeChanges || move.tick != note->tick().ticks();
        list.push_back(move);
    }

    const notation::INotationUndoStackPtr undoStack = notation->undoStack();
    undoStack->prepareChanges(muse::TranslatableString("undoableAction", "Move notes from piano roll"));
    std::vector<engraving::Note*> result;

    if (!timeChanges) {
        // Same onsets: change the pitch in place, along each tie chain.
        for (const Move& move : list) {
            const engraving::NoteVal value = engraving::NoteInput::noteVal(score, move.pitch, move.note->staffIdx(), false);
            for (engraving::Note* note = move.note; note; note = note->tieFor() ? note->tieFor()->endNote() : nullptr) {
                engraving::EditNote::undoChangePitch(score, note, value.pitch, value.tpc1, value.tpc2);
            }
            result.push_back(move.note);
        }
    } else {
        for (const Move& move : list) {
            if (move.note->tieFor() || move.note->tieBack()) {
                undoStack->rollbackChanges();
                return tr("Tied notes are moved in time in Score mode for now.");
            }
            if (move.note->chord()->tuplet()) {
                undoStack->rollbackChanges();
                return tr("Notes inside tuplets are moved in time in Score mode for now.");
            }
            if (move.note->voice() != 0) {
                undoStack->rollbackChanges();
                return tr("Notes in voices 2–4 are moved in time in Score mode for now.");
            }
        }
        // Take every moving note out first, so they can trade places, then write each one again.
        for (const Move& move : list) {
            score->deleteItem(move.note);
        }
        for (const Move& move : list) {
            QString error;
            engraving::Note* inserted = insertScoreNote(score, move.tick, move.durationTicks, move.pitch, move.track, error);
            if (!inserted) {
                undoStack->rollbackChanges();
                return error;
            }
            result.push_back(inserted);
        }
    }

    undoStack->commitChanges();
    notation->notationChanged().send(muse::RectF());

    QStringList ids;
    for (engraving::Note* note : result) {
        const QString id = persistentIdFor(note);
        if (!id.isEmpty()) {
            m_projectedNoteRefs.insert(id, note);
            ids.append(id);
        }
    }
    setProjectedNoteSelection(ids, ids.isEmpty() ? QString() : ids.last());
    emit scoreProjectionChanged();
    return {};
}

bool DawProjectModel::setPerformanceVelocity(const QString& noteId, int velocity)
{
    // The Control Lane itself declares a performance edit, even when the note
    // grid is in Notation mode. It never rewrites the score's dynamics.
    if (velocity < 1 || velocity > 127) return false;
    return updatePerformanceOverride(noteId, muse::TranslatableString("undoableAction", "Change note velocity"),
                                     [velocity](engraving::PerformanceNoteOverride& value) { value.velocity = velocity; });
}

bool DawProjectModel::resetPerformanceVelocity(const QString& noteId)
{
    return updatePerformanceOverride(noteId, muse::TranslatableString("undoableAction", "Reset note velocity"),
                                     [](engraving::PerformanceNoteOverride& value) { value.velocity.reset(); });
}

bool DawProjectModel::setPerformancePitchOffset(const QString& noteId, int cents)
{
    if (cents < -1200 || cents > 1200) return false;
    return updatePerformanceOverride(noteId, muse::TranslatableString("undoableAction", "Change note pitch"),
                                     [cents](engraving::PerformanceNoteOverride& value) {
        if (cents == 0) value.pitchOffsetCents.reset();
        else value.pitchOffsetCents = cents;
    });
}

bool DawProjectModel::resetPerformancePitchOffset(const QString& noteId)
{
    return updatePerformanceOverride(noteId, muse::TranslatableString("undoableAction", "Reset note pitch"),
                                     [](engraving::PerformanceNoteOverride& value) { value.pitchOffsetCents.reset(); });
}

bool DawProjectModel::setPerformanceStartOffset(const QString& noteId, int offsetTicks)
{
    if (m_editMode != EditMode::Performance || std::abs(offsetTicks) > 1000000) return false;
    return updatePerformanceOverride(noteId, muse::TranslatableString("undoableAction", "Change note timing"),
                                     [offsetTicks](engraving::PerformanceNoteOverride& value) {
        if (offsetTicks == 0) value.startOffsetTicks.reset();
        else value.startOffsetTicks = offsetTicks;
    });
}

bool DawProjectModel::setPerformanceDuration(const QString& noteId, int durationTicks)
{
    if (m_editMode != EditMode::Performance || durationTicks < 1) return false;
    return updatePerformanceOverride(noteId, muse::TranslatableString("undoableAction", "Change note length"),
                                     [durationTicks](engraving::PerformanceNoteOverride& value) {
        value.playbackDurationTicks = durationTicks;
    });
}

bool DawProjectModel::resetPerformance(const QString& noteId)
{
    return updatePerformanceOverride(noteId, muse::TranslatableString("undoableAction", "Reset note performance"),
                                     [](engraving::PerformanceNoteOverride& value) { value = {}; });
}

bool DawProjectModel::setPerformanceVelocities(const QVariantMap& velocities)
{
    for (auto it = velocities.cbegin(); it != velocities.cend(); ++it) {
        const int velocity = it.value().toInt();
        if (velocity < 1 || velocity > 127) return false;
    }
    return updatePerformanceOverrides(velocities.keys(), muse::TranslatableString("undoableAction", "Change note velocity"),
                                      [&velocities](const QString& id, engraving::PerformanceNoteOverride& value) {
        value.velocity = velocities.value(id).toInt();
    });
}

bool DawProjectModel::resetPerformanceVelocities(const QStringList& noteIds)
{
    return updatePerformanceOverrides(noteIds, muse::TranslatableString("undoableAction", "Reset note velocity"),
                                      [](const QString&, engraving::PerformanceNoteOverride& value) { value.velocity.reset(); });
}

bool DawProjectModel::setPerformancePitchOffsets(const QVariantMap& centsByNote)
{
    for (auto it = centsByNote.cbegin(); it != centsByNote.cend(); ++it) {
        if (std::abs(it.value().toInt()) > 1200) return false;
    }
    return updatePerformanceOverrides(centsByNote.keys(), muse::TranslatableString("undoableAction", "Change note pitch"),
                                      [&centsByNote](const QString& id, engraving::PerformanceNoteOverride& value) {
        const int cents = centsByNote.value(id).toInt();
        if (cents == 0) value.pitchOffsetCents.reset();
        else value.pitchOffsetCents = cents;
    });
}

bool DawProjectModel::resetPerformancePitchOffsets(const QStringList& noteIds)
{
    return updatePerformanceOverrides(noteIds, muse::TranslatableString("undoableAction", "Reset note pitch"),
                                      [](const QString&, engraving::PerformanceNoteOverride& value) {
        value.pitchOffsetCents.reset();
    });
}

bool DawProjectModel::setPerformanceStartOffsets(const QVariantMap& offsetsByNote)
{
    if (m_editMode != EditMode::Performance) return false;
    for (auto it = offsetsByNote.cbegin(); it != offsetsByNote.cend(); ++it) {
        if (std::abs(it.value().toInt()) > 1000000) return false;
    }
    return updatePerformanceOverrides(offsetsByNote.keys(), muse::TranslatableString("undoableAction", "Change note timing"),
                                      [&offsetsByNote](const QString& id, engraving::PerformanceNoteOverride& value) {
        const int offsetTicks = offsetsByNote.value(id).toInt();
        if (offsetTicks == 0) value.startOffsetTicks.reset();
        else value.startOffsetTicks = offsetTicks;
    });
}

bool DawProjectModel::resetPerformanceTiming(const QStringList& noteIds)
{
    return updatePerformanceOverrides(noteIds, muse::TranslatableString("undoableAction", "Reset note position"),
                                      [](const QString&, engraving::PerformanceNoteOverride& value) {
        value.startOffsetTicks.reset();
        value.playbackDurationTicks.reset();
    });
}

bool DawProjectModel::resetPerformances(const QStringList& noteIds)
{
    return updatePerformanceOverrides(noteIds, muse::TranslatableString("undoableAction", "Reset note performance"),
                                      [](const QString&, engraving::PerformanceNoteOverride& value) { value = {}; });
}

QVariantList DawProjectModel::channelAutomationPoints(const QString& partId, const QString& instrumentId,
                                                      const QString& control) const
{
    QVariantList result;
    const engraving::InstrumentTrackId trackId = instrumentTrackId(partId, instrumentId);
    const std::optional<engraving::AutomationCurveKey> key = channelAutomationKey(control, trackId);
    const project::INotationProjectPtr project = globalContext()->currentProject();
    const notation::INotationAutomationPtr automation = project && project->masterNotation()
                                                         ? project->masterNotation()->automation() : nullptr;
    const engraving::AutomationDataConstPtr data = automation ? automation->automationData() : nullptr;
    if (!key || !data) return result;

    const engraving::AutomationCurve& curve = data->curve(*key);
    result.reserve(int(curve.size()));
    for (auto it = curve.cbegin(); it != curve.cend(); ++it) {
        const engraving::AutomationPoint& point = it->second;
        const std::optional<engraving::AutomationPoint::Ease> ease = engraving::ease(point);
        const bool step = std::holds_alternative<engraving::AutomationPoint::ArrivalFromPrevious>(point.value.inValue);
        const bool curved = ease && !ease->isNone();
        result.append(QVariantMap {
            { "tick", it->first },
            { "value", std::clamp<double>(point.value.outValue, 0.0, 1.0) },
            { "arrival", std::clamp<double>(engraving::resolveInValue(curve, it), 0.0, 1.0) },
            { "shape", step ? "step" : (curved ? "curve" : "linear") },
            { "bend", curved ? double(ease->value) : 0.5 },
            { "generated", point.generated }
        });
    }
    return result;
}

bool DawProjectModel::editChannelAutomation(const QString& partId, const QString& instrumentId,
                                            const QString& control, const QVariantList& edits)
{
    const engraving::InstrumentTrackId trackId = instrumentTrackId(partId, instrumentId);
    const std::optional<engraving::AutomationCurveKey> key = channelAutomationKey(control, trackId);
    const project::INotationProjectPtr project = globalContext()->currentProject();
    const notation::INotationAutomationPtr automation = project && project->masterNotation()
                                                         ? project->masterNotation()->automation() : nullptr;
    if (!key || !automation || edits.isEmpty()) return false;

    engraving::AutomationPointEdits pointEdits;
    pointEdits.reserve(size_t(edits.size()));
    for (const QVariant& item : edits) {
        const QVariantMap edit = item.toMap();
        const QString op = edit.value("op").toString();
        const int tick = edit.value("tick").toInt();
        if (tick < 0) return false;
        if (op == "erase") {
            pointEdits.push_back({ tick, engraving::AutomationPointEdit::ErasePoint {} });
            continue;
        }

        const double value = edit.value("value").toDouble();
        if (!std::isfinite(value) || value < 0.0 || value > 1.0) return false;
        engraving::AutomationPoint point;
        point.value.outValue = value;
        const QString shape = edit.value("shape", QStringLiteral("linear")).toString();
        if (shape == "step") {
            // Holds the previous value up to this point, then jumps.
            point.value.inValue = engraving::AutomationPoint::ArrivalFromPrevious {};
        } else {
            engraving::AutomationPoint::Ease ease = engraving::AutomationPoint::Ease::none();
            if (shape == "curve") {
                const double bend = std::clamp(edit.value("bend", 0.5).toDouble(), 0.0, 1.0);
                ease.t = 0.5;
                ease.value = bend;
            }
            point.value.inValue = engraving::AutomationPoint::ExplicitArrival { value, ease };
        }

        if (op == "move") {
            const int from = edit.value("from").toInt();
            if (from < 0) return false;
            pointEdits.push_back({ tick, engraving::AutomationPointEdit::MovePoint { point, from } });
        } else if (op == "set") {
            pointEdits.push_back({ tick, engraving::AutomationPointEdit::SetPoint { point } });
        } else {
            return false;
        }
    }
    automation->editPoints(*key, pointEdits);
    return true;
}

bool DawProjectModel::setChannelAutomationPoint(const QString& partId, const QString& instrumentId,
                                                const QString& control, int tick, double value)
{
    const engraving::InstrumentTrackId trackId = instrumentTrackId(partId, instrumentId);
    const std::optional<engraving::AutomationCurveKey> key = channelAutomationKey(control, trackId);
    const project::INotationProjectPtr project = globalContext()->currentProject();
    const notation::INotationAutomationPtr automation = project && project->masterNotation()
                                                         ? project->masterNotation()->automation() : nullptr;
    if (!key || !automation || tick < 0 || !std::isfinite(value)
        || value < 0.0 || value > 1.0) return false;

    engraving::AutomationPoint point;
    point.value.outValue = value;
    point.value.inValue = engraving::AutomationPoint::ExplicitArrival { value, {} };
    engraving::AutomationPointEdits edits {
        { tick, engraving::AutomationPointEdit::SetPoint { point } }
    };
    automation->editPoints(*key, edits);
    return true;
}

bool DawProjectModel::removeChannelAutomationPoint(const QString& partId, const QString& instrumentId,
                                                   const QString& control, int tick)
{
    const engraving::InstrumentTrackId trackId = instrumentTrackId(partId, instrumentId);
    const std::optional<engraving::AutomationCurveKey> key = channelAutomationKey(control, trackId);
    const project::INotationProjectPtr project = globalContext()->currentProject();
    const notation::INotationAutomationPtr automation = project && project->masterNotation()
                                                         ? project->masterNotation()->automation() : nullptr;
    if (!key || !automation || tick < 0) return false;

    const engraving::AutomationDataConstPtr data = automation->automationData();
    if (!data || data->curve(*key).find(tick) == data->curve(*key).end()) return true;
    engraving::AutomationPointEdits edits {
        { tick, engraving::AutomationPointEdit::ErasePoint {} }
    };
    automation->editPoints(*key, edits);
    return true;
}

bool DawProjectModel::resetChannelAutomation(const QString& partId, const QString& instrumentId,
                                             const QString& control)
{
    const engraving::InstrumentTrackId trackId = instrumentTrackId(partId, instrumentId);
    const std::optional<engraving::AutomationCurveKey> key = channelAutomationKey(control, trackId);
    const project::INotationProjectPtr project = globalContext()->currentProject();
    const notation::INotationAutomationPtr automation = project && project->masterNotation()
                                                         ? project->masterNotation()->automation() : nullptr;
    if (!key || !automation) return false;

    const engraving::AutomationDataConstPtr data = automation->automationData();
    if (!data || data->curve(*key).empty()) return true;
    engraving::AutomationPointEdits edits;
    edits.reserve(data->curve(*key).size());
    for (const auto& [tick, point] : data->curve(*key)) {
        static_cast<void>(point);
        edits.push_back({ tick, engraving::AutomationPointEdit::ErasePoint {} });
    }
    automation->editPoints(*key, edits);
    return true;
}

QVariantList DawProjectModel::midiControllerCatalog() const
{
    QVariantList result;
    result.reserve(int(engraving::MIDI_CONTROLLER_CATALOG.size()));
    for (const engraving::MidiControllerInfo& info : engraving::MIDI_CONTROLLER_CATALOG) {
        QString category;
        switch (info.category) {
        case engraving::MidiControllerCategory::BankAndProgram: category = QStringLiteral("bankAndProgram"); break;
        case engraving::MidiControllerCategory::Performance: category = QStringLiteral("performance"); break;
        case engraving::MidiControllerCategory::GeneralPurpose: category = QStringLiteral("generalPurpose"); break;
        case engraving::MidiControllerCategory::PedalAndSwitch: category = QStringLiteral("pedalAndSwitch"); break;
        case engraving::MidiControllerCategory::SoundController: category = QStringLiteral("soundController"); break;
        case engraving::MidiControllerCategory::Effects: category = QStringLiteral("effects"); break;
        case engraving::MidiControllerCategory::DataEntry: category = QStringLiteral("dataEntry"); break;
        case engraving::MidiControllerCategory::ParameterSelection: category = QStringLiteral("parameterSelection"); break;
        case engraving::MidiControllerCategory::ChannelMode: category = QStringLiteral("channelMode"); break;
        default: category = QStringLiteral("undefined"); break;
        }
        QString behavior;
        switch (info.behavior) {
        case engraving::MidiControllerBehavior::Switch: behavior = QStringLiteral("switch"); break;
        case engraving::MidiControllerBehavior::Stepped: behavior = QStringLiteral("stepped"); break;
        case engraving::MidiControllerBehavior::Trigger: behavior = QStringLiteral("trigger"); break;
        case engraving::MidiControllerBehavior::ChannelMode: behavior = QStringLiteral("channelMode"); break;
        default: behavior = QStringLiteral("continuous"); break;
        }
        result.append(QVariantMap {
            { "stableId", QStringLiteral("cc:%1").arg(int(info.number)) },
            { "number", int(info.number) },
            { "name", QString::fromUtf8(info.canonicalName.data(), qsizetype(info.canonicalName.size())) },
            { "category", category },
            { "defaultValue", info.defaultValue },
            { "minimum", int(info.minimum) },
            { "maximum", int(info.maximum) },
            { "unit", QString::fromUtf8(info.unit.data(), qsizetype(info.unit.size())) },
            { "resolution", info.resolution == engraving::MidiControllerResolution::SevenBit ? 7 : 14 },
            { "pairedMsb", int(info.pairedMsb) },
            { "pairedLsb", int(info.pairedLsb) },
            { "behavior", behavior },
            { "defined", info.isDefined() },
            { "dangerous", info.risk != engraving::MidiControllerRisk::None }
        });
    }
    return result;
}

QString DawProjectModel::newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString DawProjectModel::addMidiTrack(const QString& name)
{
    const int row = m_tracks.size();
    beginInsertRows({}, row, row);
    Track track;
    track.id = newId();
    track.name = name.trimmed().isEmpty() ? tr("MIDI Track %1").arg(row + 1) : name.trimmed();
    static const QColor colors[] = { "#d4af37", "#d98555", "#6ea7d8", "#9b7bd4" };
    track.color = colors[row % 4];
    m_tracks.append(track);
    endInsertRows();
    emit trackCountChanged();
    emit projectChanged();
    return track.id;
}

QString DawProjectModel::trackIdAt(int row) const { return row >= 0 && row < m_tracks.size() ? m_tracks.at(row).id : QString(); }

bool DawProjectModel::removeTrack(const QString& trackId)
{
    for (int row = 0; row < m_tracks.size(); ++row) {
        if (m_tracks.at(row).id != trackId) continue;
        bool selectionChangedValue = false;
        for (const Region& region : m_tracks.at(row).regions) {
            if (region.id == m_selectedRegionId
                || std::any_of(region.notes.cbegin(), region.notes.cend(), [this](const MidiNote& note) {
                    return note.id == m_selectedNoteId;
                })) {
                selectionChangedValue = true;
            }
        }
        beginRemoveRows({}, row, row);
        m_tracks.removeAt(row);
        endRemoveRows();
        if (selectionChangedValue) {
            m_selectedRegionId.clear();
            m_selectedNoteId.clear();
            emit selectionChanged();
        }
        emit trackCountChanged();
        emit projectChanged();
        return true;
    }
    return false;
}

QString DawProjectModel::addMidiRegion(const QString& trackId, int startTick, int lengthTicks, const QString& name)
{
    Track* track = trackById(trackId);
    if (!track || startTick < 0 || lengthTicks < 1) return {};
    Region region { newId(), name.trimmed().isEmpty() ? tr("MIDI Region") : name.trimmed(), startTick, lengthTicks, {} };
    const QString id = region.id;
    track->regions.append(region);
    sortByStartTick(track->regions);
    const int row = int(track - m_tracks.data());
    notifyRegionChanged(row, id);
    emit projectChanged();
    return id;
}

bool DawProjectModel::moveRegion(const QString& regionId, int startTick)
{
    int row = -1;
    Region* region = regionById(regionId, &row);
    if (!region || startTick < 0) return false;
    region->startTick = startTick;
    sortByStartTick(m_tracks[row].regions);
    notifyRegionChanged(row, regionId);
    emit projectChanged();
    return true;
}

bool DawProjectModel::moveRegionToTrack(const QString& regionId, const QString& trackId, int startTick)
{
    if (startTick < 0) return false;
    int sourceRow = -1;
    Region* region = regionById(regionId, &sourceRow);
    Track* destination = trackById(trackId);
    if (!region || !destination) return false;
    const int destinationRow = int(destination - m_tracks.data());
    if (sourceRow == destinationRow) return moveRegion(regionId, startTick);
    Region moved = *region;
    moved.startTick = startTick;
    auto& sourceRegions = m_tracks[sourceRow].regions;
    sourceRegions.erase(std::remove_if(sourceRegions.begin(), sourceRegions.end(), [&regionId](const Region& item) {
        return item.id == regionId;
    }), sourceRegions.end());
    destination->regions.append(moved);
    sortByStartTick(destination->regions);
    emit dataChanged(index(sourceRow), index(sourceRow), { RegionsRole });
    notifyRegionChanged(destinationRow, regionId);
    emit projectChanged();
    return true;
}

bool DawProjectModel::resizeRegion(const QString& regionId, int startTick, int lengthTicks)
{
    int row = -1;
    Region* region = regionById(regionId, &row);
    if (!region || startTick < 0 || lengthTicks < 1) return false;
    region->startTick = startTick;
    region->lengthTicks = lengthTicks;
    for (MidiNote& note : region->notes) {
        note.startTick = std::min(note.startTick, lengthTicks - MIN_NOTE_DURATION);
        note.durationTicks = std::min(note.durationTicks, lengthTicks - note.startTick);
    }
    sortByStartTick(m_tracks[row].regions);
    notifyRegionChanged(row, regionId);
    emit projectChanged();
    return true;
}

bool DawProjectModel::deleteRegion(const QString& regionId)
{
    int row = -1;
    Region* region = regionById(regionId, &row);
    if (!region) return false;
    const bool selected = m_selectedRegionId == regionId
                          || std::any_of(region->notes.cbegin(), region->notes.cend(), [this](const MidiNote& note) { return note.id == m_selectedNoteId; });
    auto& regions = m_tracks[row].regions;
    regions.erase(std::remove_if(regions.begin(), regions.end(), [&regionId](const Region& item) { return item.id == regionId; }), regions.end());
    emit dataChanged(index(row), index(row), { RegionsRole });
    if (selected) {
        m_selectedRegionId.clear();
        m_selectedNoteId.clear();
        emit selectionChanged();
    }
    emit projectChanged();
    return true;
}

bool DawProjectModel::selectRegion(const QString& regionId)
{
    if (!regionId.isEmpty() && !regionById(regionId)) return false;
    if (m_selectedRegionId == regionId && m_selectedNoteId.isEmpty()) return true;
    int previousRow = -1;
    regionById(m_selectedRegionId, &previousRow);
    m_selectedRegionId = regionId;
    m_selectedNoteId.clear();
    int currentRow = -1;
    regionById(m_selectedRegionId, &currentRow);
    if (previousRow >= 0) emit dataChanged(index(previousRow), index(previousRow), { RegionsRole });
    if (currentRow >= 0 && currentRow != previousRow) emit dataChanged(index(currentRow), index(currentRow), { RegionsRole });
    emit selectionChanged();
    return true;
}

QVariantMap DawProjectModel::selectedRegion() const
{
    const Region* region = regionById(m_selectedRegionId);
    return region ? regionForQml(*region) : QVariantMap {};
}

QVariantList DawProjectModel::notesForRegion(const QString& regionId) const
{
    QVariantList result;
    const Region* region = regionById(regionId);
    if (!region) return result;
    result.reserve(region->notes.size());
    for (const MidiNote& note : region->notes) {
        result.append(QVariantMap { { "noteId", note.id }, { "startTick", note.startTick },
            { "durationTicks", note.durationTicks }, { "pitch", note.pitch }, { "velocity", note.velocity },
            { "selected", note.id == m_selectedNoteId } });
    }
    return result;
}

QVariantList DawProjectModel::notesStartingBetween(int fromTick, int toTick) const
{
    QVariantList result;
    if (toTick < fromTick) return result;

    const bool anySolo = std::any_of(m_tracks.cbegin(), m_tracks.cend(), [](const Track& track) { return track.solo; });
    for (const Track& track : m_tracks) {
        if (track.muted || (anySolo && !track.solo)) continue;
        for (const Region& region : track.regions) {
            for (const MidiNote& note : region.notes) {
                const int absoluteTick = region.startTick + note.startTick;
                if (absoluteTick <= fromTick || absoluteTick > toTick) continue;
                result.append(QVariantMap { { "noteId", note.id }, { "pitch", note.pitch },
                    { "velocity", note.velocity }, { "durationTicks", note.durationTicks },
                    { "absoluteTick", absoluteTick } });
            }
        }
    }
    return result;
}

QString DawProjectModel::addMidiNote(const QString& regionId, int startTick, int durationTicks, int pitch, int velocity)
{
    int row = -1;
    Region* region = regionById(regionId, &row);
    if (!region || startTick < 0 || startTick >= region->lengthTicks || durationTicks < 1 || pitch < 0 || pitch > 127
        || velocity < 1 || velocity > 127) return {};
    durationTicks = std::min(durationTicks, region->lengthTicks - startTick);
    MidiNote note { newId(), startTick, durationTicks, pitch, velocity };
    const QString id = note.id;
    region->notes.append(note);
    sortByStartTick(region->notes);
    notifyRegionChanged(row, regionId, true);
    emit projectChanged();
    return id;
}

bool DawProjectModel::moveMidiNote(const QString& noteId, int startTick, int pitch)
{
    Region* owner = nullptr;
    int row = -1;
    MidiNote* note = noteById(noteId, &owner, &row);
    if (!note || !owner || startTick < 0 || pitch < 0 || pitch > 127 || startTick + note->durationTicks > owner->lengthTicks) return false;
    note->startTick = startTick;
    note->pitch = pitch;
    sortByStartTick(owner->notes);
    notifyRegionChanged(row, owner->id, true);
    emit projectChanged();
    return true;
}

bool DawProjectModel::resizeMidiNote(const QString& noteId, int durationTicks)
{
    Region* owner = nullptr;
    int row = -1;
    MidiNote* note = noteById(noteId, &owner, &row);
    if (!note || !owner || durationTicks < 1 || note->startTick + durationTicks > owner->lengthTicks) return false;
    note->durationTicks = durationTicks;
    notifyRegionChanged(row, owner->id, true);
    emit projectChanged();
    return true;
}

bool DawProjectModel::setMidiNoteVelocity(const QString& noteId, int velocity)
{
    Region* owner = nullptr;
    int row = -1;
    MidiNote* note = noteById(noteId, &owner, &row);
    if (!note || velocity < 1 || velocity > 127) return false;
    note->velocity = velocity;
    notifyRegionChanged(row, owner->id, true);
    emit projectChanged();
    return true;
}

bool DawProjectModel::deleteMidiNote(const QString& noteId)
{
    Region* owner = nullptr;
    int row = -1;
    MidiNote* note = noteById(noteId, &owner, &row);
    if (!note || !owner) return false;
    const QString regionId = owner->id;
    owner->notes.erase(std::remove_if(owner->notes.begin(), owner->notes.end(), [&noteId](const MidiNote& item) { return item.id == noteId; }), owner->notes.end());
    if (m_selectedNoteId == noteId) {
        m_selectedNoteId.clear();
        emit selectionChanged();
    }
    notifyRegionChanged(row, regionId, true);
    emit projectChanged();
    return true;
}

bool DawProjectModel::selectMidiNote(const QString& noteId)
{
    if (noteId.isEmpty()) {
        if (m_selectedNoteId.isEmpty()) return true;
        m_selectedNoteId.clear();
        emit selectionChanged();
        return true;
    }
    const Region* owner = nullptr;
    if (!noteById(noteId, &owner) || !owner) return false;
    int previousRow = -1;
    regionById(m_selectedRegionId, &previousRow);
    m_selectedRegionId = owner->id;
    m_selectedNoteId = noteId;
    int currentRow = -1;
    regionById(m_selectedRegionId, &currentRow);
    if (previousRow >= 0) emit dataChanged(index(previousRow), index(previousRow), { RegionsRole });
    if (currentRow >= 0 && currentRow != previousRow) emit dataChanged(index(currentRow), index(currentRow), { RegionsRole });
    emit selectionChanged();
    return true;
}

int DawProjectModel::quantizeRegion(const QString& regionId, int gridTicks)
{
    int row = -1;
    Region* region = regionById(regionId, &row);
    if (!region || gridTicks < 1) return 0;
    int changed = 0;
    for (MidiNote& note : region->notes) {
        int quantized = ((note.startTick + gridTicks / 2) / gridTicks) * gridTicks;
        quantized = std::clamp(quantized, 0, region->lengthTicks - note.durationTicks);
        if (quantized != note.startTick) {
            note.startTick = quantized;
            ++changed;
        }
    }
    if (changed) {
        sortByStartTick(region->notes);
        notifyRegionChanged(row, regionId, true);
        emit projectChanged();
    }
    return changed;
}

int DawProjectModel::snappedTick(int tick) const
{
    if (tick <= 0) return 0;
    return ((tick + m_snapTicks / 2) / m_snapTicks) * m_snapTicks;
}

QString DawProjectModel::serialize() const
{
    QJsonArray tracks;
    for (const Track& track : m_tracks) {
        QJsonArray regions;
        for (const Region& region : track.regions) {
            QJsonArray notes;
            for (const MidiNote& note : region.notes) {
                notes.append(QJsonObject { { "id", note.id }, { "startTick", note.startTick },
                    { "durationTicks", note.durationTicks }, { "pitch", note.pitch }, { "velocity", note.velocity } });
            }
            regions.append(QJsonObject { { "id", region.id }, { "name", region.name }, { "startTick", region.startTick },
                { "lengthTicks", region.lengthTicks }, { "notes", notes } });
        }
        tracks.append(QJsonObject { { "id", track.id }, { "name", track.name }, { "color", track.color.name(QColor::HexRgb) },
            { "muted", track.muted }, { "solo", track.solo }, { "armed", track.armed }, { "regions", regions } });
    }
    // Sorted by EID so saving the same overrides always produces the same file.
    QList<QJsonObject> sortedOverrides;
    if (const engraving::PerformanceOverlay* overlay = performanceOverlay()) {
        for (const auto& [eid, value] : overlay->overrides()) {
            QJsonObject object { { "sourceNoteEid", QString::fromStdString(eid.toStdString()) } };
            if (value.velocity) object.insert("velocity", *value.velocity);
            if (value.startOffsetTicks) object.insert("startOffsetTicks", *value.startOffsetTicks);
            if (value.playbackDurationTicks) object.insert("playbackDurationTicks", *value.playbackDurationTicks);
            if (value.pitchOffsetCents) object.insert("pitchOffsetCents", *value.pitchOffsetCents);
            sortedOverrides.append(object);
        }
    }
    std::sort(sortedOverrides.begin(), sortedOverrides.end(), [](const QJsonObject& left, const QJsonObject& right) {
        return left.value("sourceNoteEid").toString() < right.value("sourceNoteEid").toString();
    });
    QJsonArray noteOverrides;
    for (const QJsonObject& object : sortedOverrides) noteOverrides.append(object);
    const QJsonObject root { { "schemaVersion", 2 }, { "tempo", m_tempo },
        { "timeSignature", QJsonObject { { "numerator", m_timeSignatureNumerator }, { "denominator", m_timeSignatureDenominator } } },
        { "tracks", tracks },
        { "performance", QJsonObject { { "version", 1 }, { "noteOverrides", noteOverrides } } } };
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool DawProjectModel::deserialize(const QString& json)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject root = document.object();
    const int schemaVersion = root.value("schemaVersion").toInt(-1);
    if ((schemaVersion != 1 && schemaVersion != 2) || !root.value("tracks").isArray()) return false;
    const double tempoValue = root.value("tempo").toDouble(-1.0);
    const QJsonObject signature = root.value("timeSignature").toObject();
    const int numerator = signature.value("numerator").toInt(-1);
    const int denominator = signature.value("denominator").toInt(-1);
    if (!std::isfinite(tempoValue) || tempoValue < MIN_TEMPO || tempoValue > MAX_TEMPO || numerator < 1 || numerator > 32
        || denominator < 1 || denominator > 64 || (denominator & (denominator - 1)) != 0) return false;

    QList<Track> parsedTracks;
    QSet<QString> ids;
    for (const QJsonValue& trackValue : root.value("tracks").toArray()) {
        if (!trackValue.isObject()) return false;
        const QJsonObject object = trackValue.toObject();
        Track track;
        track.id = object.value("id").toString();
        track.name = object.value("name").toString();
        track.color = QColor(object.value("color").toString());
        track.muted = object.value("muted").toBool();
        track.solo = object.value("solo").toBool();
        track.armed = object.value("armed").toBool();
        if (track.id.isEmpty() || ids.contains(track.id) || !track.color.isValid() || !object.value("regions").isArray()) return false;
        ids.insert(track.id);
        for (const QJsonValue& regionValue : object.value("regions").toArray()) {
            const QJsonObject regionObject = regionValue.toObject();
            Region region { regionObject.value("id").toString(), regionObject.value("name").toString(),
                regionObject.value("startTick").toInt(-1), regionObject.value("lengthTicks").toInt(-1), {} };
            if (!regionValue.isObject() || region.id.isEmpty() || ids.contains(region.id) || region.startTick < 0 || region.lengthTicks < 1
                || !regionObject.value("notes").isArray()) return false;
            ids.insert(region.id);
            for (const QJsonValue& noteValue : regionObject.value("notes").toArray()) {
                const QJsonObject noteObject = noteValue.toObject();
                MidiNote note { noteObject.value("id").toString(), noteObject.value("startTick").toInt(-1),
                    noteObject.value("durationTicks").toInt(-1), noteObject.value("pitch").toInt(-1), noteObject.value("velocity").toInt(-1) };
                if (!noteValue.isObject() || note.id.isEmpty() || ids.contains(note.id) || note.startTick < 0 || note.durationTicks < 1
                    || note.startTick + note.durationTicks > region.lengthTicks || note.pitch < 0 || note.pitch > 127
                    || note.velocity < 1 || note.velocity > 127) return false;
                ids.insert(note.id);
                region.notes.append(note);
            }
            sortByStartTick(region.notes);
            track.regions.append(region);
        }
        sortByStartTick(track.regions);
        parsedTracks.append(track);
    }

    std::vector<std::pair<engraving::EID, engraving::PerformanceNoteOverride>> parsedOverrides;
    QSet<QString> seenOverrideIds;
    if (schemaVersion >= 2) {
        const QJsonObject performance = root.value("performance").toObject();
        if (performance.value("version").toInt(-1) != 1 || !performance.value("noteOverrides").isArray()) return false;
        for (const QJsonValue& overrideValue : performance.value("noteOverrides").toArray()) {
            if (!overrideValue.isObject()) return false;
            const QJsonObject object = overrideValue.toObject();
            const QString sourceEid = object.value("sourceNoteEid").toString();
            if (!isSerializedEid(sourceEid)) return false;
            const engraving::EID eid = engraving::EID::fromStdString(sourceEid.toStdString());
            if (sourceEid.isEmpty() || !eid.isValid() || seenOverrideIds.contains(sourceEid)) return false;
            seenOverrideIds.insert(sourceEid);
            engraving::PerformanceNoteOverride value;
            if (object.contains("velocity")) {
                const int velocity = object.value("velocity").toInt(-1);
                if (velocity < 1 || velocity > 127) return false;
                value.velocity = velocity;
            }
            if (object.contains("startOffsetTicks")) {
                const int offset = object.value("startOffsetTicks").toInt(1000001);
                if (std::abs(offset) > 1000000) return false;
                if (offset != 0) value.startOffsetTicks = offset;
            }
            if (object.contains("playbackDurationTicks")) {
                const int duration = object.value("playbackDurationTicks").toInt(-1);
                if (duration < 1) return false;
                value.playbackDurationTicks = duration;
            }
            if (object.contains("pitchOffsetCents")) {
                const int cents = object.value("pitchOffsetCents").toInt(1201);
                if (cents < -1200 || cents > 1200) return false;
                if (cents != 0) value.pitchOffsetCents = cents;
            }
            if (!value.empty()) parsedOverrides.emplace_back(eid, value);
        }
    }

    beginResetModel();
    m_tracks = parsedTracks;
    if (engraving::PerformanceOverlay* overlay = performanceOverlay()) {
        // Loading restores saved state; it is not an undoable edit.
        overlay->clear();
        for (const auto& [eid, value] : parsedOverrides) overlay->set(eid, value);
    }
    m_projectedNoteRefs.clear();
    m_tempo = tempoValue;
    m_timeSignatureNumerator = numerator;
    m_timeSignatureDenominator = denominator;
    m_selectedRegionId.clear();
    m_selectedNoteId.clear();
    endResetModel();
    emit trackCountChanged();
    emit tempoChanged();
    emit timeSignatureChanged();
    emit selectionChanged();
    emit scoreProjectionChanged();
    emit projectChanged();
    return true;
}

void DawProjectModel::clear()
{
    beginResetModel();
    m_tracks.clear();
    m_selectedRegionId.clear();
    m_selectedNoteId.clear();
    m_selectedProjectedNoteId.clear();
    m_selectedProjectedNoteIds.clear();
    m_projectedNoteRefs.clear();
    if (engraving::PerformanceOverlay* overlay = performanceOverlay()) overlay->clear();
    endResetModel();
    emit trackCountChanged();
    emit selectionChanged();
    emit projectedSelectionChanged();
    emit scoreProjectionChanged();
    emit projectChanged();
}

void DawProjectModel::toggleMute(const QString& id) { toggleTrackFlag(id, MutedRole, &Track::muted); }
void DawProjectModel::toggleSolo(const QString& id) { toggleTrackFlag(id, SoloRole, &Track::solo); }
void DawProjectModel::toggleArmed(const QString& id) { toggleTrackFlag(id, ArmedRole, &Track::armed); }

DawProjectModel::Track* DawProjectModel::trackById(const QString& id)
{
    auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [&id](const Track& track) { return track.id == id; });
    return it == m_tracks.end() ? nullptr : &*it;
}

const DawProjectModel::Track* DawProjectModel::trackById(const QString& id) const
{
    auto it = std::find_if(m_tracks.cbegin(), m_tracks.cend(), [&id](const Track& track) { return track.id == id; });
    return it == m_tracks.cend() ? nullptr : &*it;
}

DawProjectModel::Region* DawProjectModel::regionById(const QString& id, int* trackRow)
{
    for (int row = 0; row < m_tracks.size(); ++row) for (Region& region : m_tracks[row].regions) if (region.id == id) {
        if (trackRow) *trackRow = row;
        return &region;
    }
    return nullptr;
}

const DawProjectModel::Region* DawProjectModel::regionById(const QString& id, int* trackRow) const
{
    for (int row = 0; row < m_tracks.size(); ++row) for (const Region& region : m_tracks.at(row).regions) if (region.id == id) {
        if (trackRow) *trackRow = row;
        return &region;
    }
    return nullptr;
}

DawProjectModel::MidiNote* DawProjectModel::noteById(const QString& id, Region** owner, int* trackRow)
{
    if (id.isEmpty()) return nullptr;
    for (int row = 0; row < m_tracks.size(); ++row) for (Region& region : m_tracks[row].regions) for (MidiNote& note : region.notes) if (note.id == id) {
        if (owner) *owner = &region;
        if (trackRow) *trackRow = row;
        return &note;
    }
    return nullptr;
}

const DawProjectModel::MidiNote* DawProjectModel::noteById(const QString& id, const Region** owner) const
{
    if (id.isEmpty()) return nullptr;
    for (const Track& track : m_tracks) for (const Region& region : track.regions) for (const MidiNote& note : region.notes) if (note.id == id) {
        if (owner) *owner = &region;
        return &note;
    }
    return nullptr;
}

QVariantMap DawProjectModel::regionForQml(const Region& region) const
{
    return { { "regionId", region.id }, { "name", region.name }, { "startTick", region.startTick },
        { "lengthTicks", region.lengthTicks }, { "selected", region.id == m_selectedRegionId }, { "noteCount", region.notes.size() } };
}

QVariantList DawProjectModel::regionsForQml(const Track& track) const
{
    QVariantList result;
    result.reserve(track.regions.size());
    for (const Region& region : track.regions) result.append(regionForQml(region));
    return result;
}

void DawProjectModel::toggleTrackFlag(const QString& id, int role, bool Track::*flag)
{
    Track* track = trackById(id);
    if (!track) return;
    track->*flag = !(track->*flag);
    const int row = int(track - m_tracks.data());
    emit dataChanged(index(row), index(row), { role });
    emit projectChanged();
}

void DawProjectModel::notifyRegionChanged(int trackRow, const QString& regionId, bool notesOnly)
{
    emit dataChanged(index(trackRow), index(trackRow), { RegionsRole });
    if (notesOnly) emit notesChanged(regionId);
    else emit regionChanged(regionId);
}

engraving::Note* DawProjectModel::resolveProjectedNote(const QString& noteId) const
{
    return m_projectedNoteRefs.value(noteId, nullptr);
}

QString DawProjectModel::persistentIdFor(engraving::Note* note)
{
    if (!note) return {};
    engraving::EID eid = note->eid();
    if (!eid.isValid()) eid = note->assignNewEID();
    if (!eid.isValid()) return {};
    return QString::fromStdString(eid.toStdString());
}

QVariantMap DawProjectModel::projectedNoteForQml(engraving::Note* note, const QString& id) const
{
    const int scoreTick = note->tick().ticks();
    const int notatedDuration = std::max(1, note->playTicks());
    const engraving::PerformanceOverlay* overlay = performanceOverlay();
    const engraving::PerformanceNoteOverride value = overlay && note->eid().isValid()
                                                    ? overlay->value(note->eid()) : engraving::PerformanceNoteOverride();
    const int velocity = value.velocity.value_or(note->userVelocity() > 0 ? note->userVelocity() : 80);
    const int startOffset = value.startOffsetTicks.value_or(0);
    const int playbackDuration = value.playbackDurationTicks.value_or(notatedDuration);
    const int pitchOffsetCents = value.pitchOffsetCents.value_or(0);
    return {
        { "noteId", id }, { "sourceNoteEid", id.startsWith("session:") ? QString() : id },
        { "pitch", note->pitch() }, { "playbackPitch", note->ppitch() },
        { "scoreTick", scoreTick }, { "startTick", scoreTick + startOffset },
        { "notatedDurationTicks", notatedDuration }, { "durationTicks", playbackDuration },
        { "track", int(note->track()) }, { "staffIndex", int(note->staffIdx()) }, { "voiceIndex", int(note->voice()) },
        { "velocity", velocity }, { "startOffsetTicks", startOffset }, { "pitchOffsetCents", pitchOffsetCents },
        { "hasVelocityOverride", value.velocity.has_value() },
        { "hasPitchOffsetOverride", value.pitchOffsetCents.has_value() }, { "hasPerformanceOverride", !value.empty() },
        { "selected", note->selected() || m_selectedProjectedNoteIds.contains(id) },
        { "editMode", m_editMode == EditMode::Notation ? "NOTATION" : "PERFORMANCE" }
    };
}

engraving::PerformanceOverlay* DawProjectModel::performanceOverlay() const
{
    const project::INotationProjectPtr project = globalContext()->currentProject();
    engraving::MasterScore* score = project && project->masterNotation() ? project->masterNotation()->masterScore() : nullptr;
    return score ? &score->performanceOverlay() : nullptr;
}

bool DawProjectModel::updatePerformanceOverride(const QString& noteId, const muse::TranslatableString& actionName,
                                                const std::function<void(engraving::PerformanceNoteOverride&)>& update)
{
    return updatePerformanceOverrides({ noteId }, actionName,
                                      [&update](const QString&, engraving::PerformanceNoteOverride& value) { update(value); });
}

bool DawProjectModel::updatePerformanceOverrides(const QStringList& noteIds, const muse::TranslatableString& actionName,
                                                 const std::function<void(const QString& noteId,
                                                                          engraving::PerformanceNoteOverride&)>& update)
{
    engraving::PerformanceOverlay* overlay = performanceOverlay();
    const project::INotationProjectPtr project = globalContext()->currentProject();
    const notation::INotationPtr notation = project && project->masterNotation() ? project->masterNotation()->notation() : nullptr;
    if (!overlay || !notation || !notation->undoStack() || noteIds.isEmpty()) return false;

    struct Change {
        QString id;
        QString persistentId;
        engraving::Note* note = nullptr;
        engraving::PerformanceNoteOverride value;
    };
    std::vector<Change> changes;
    bool allResolved = true;
    for (const QString& id : noteIds) {
        engraving::Note* note = resolveProjectedNote(id);
        const QString persistentId = note ? persistentIdFor(note) : QString();
        if (persistentId.isEmpty()) {
            allResolved = false;
            continue;
        }
        engraving::PerformanceNoteOverride value = overlay->value(note->eid());
        update(id, value);
        if (!(value == overlay->value(note->eid()))) {
            changes.push_back({ id, persistentId, note, value });
        }
    }

    // One undo entry per gesture, however many notes it touches, on the same history as notation
    // edits; each command's changed range makes playback re-render just its note.
    if (!changes.empty()) {
        notation->undoStack()->prepareChanges(actionName);
        for (const Change& change : changes) {
            change.note->score()->undo(new engraving::ChangePerformanceOverride(change.note, change.note->eid(), change.value));
        }
        notation->undoStack()->commitChanges();
    }

    // Notes that had a session id now have a persistent one.
    QStringList selection = m_selectedProjectedNoteIds;
    QString primary = m_selectedProjectedNoteId;
    for (const Change& change : changes) {
        if (change.id == change.persistentId) continue;
        m_projectedNoteRefs.remove(change.id);
        m_projectedNoteRefs.insert(change.persistentId, change.note);
        const qsizetype index = selection.indexOf(change.id);
        if (index >= 0) selection[index] = change.persistentId;
        if (primary == change.id) primary = change.persistentId;
    }
    setProjectedSelection(selection, primary);
    return allResolved;
}

void DawProjectModel::subscribeToScoreChanges()
{
    // Dropping the previous scope disconnects callbacks bound to an older project.
    m_projectScope = std::make_unique<muse::async::Asyncable>();
    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (!project || !project->masterNotation() || !project->masterNotation()->masterScore()) return;
    engraving::MasterScore* score = project->masterNotation()->masterScore();
    score->changesChannel().onReceive(m_projectScope.get(), [this](const engraving::ScoreChanges&) {
        m_projectedNoteRefs.clear();
        // Undo/redo may have changed the performance layer; keep the saved
        // payload in step (unchanged data does not mark the project modified).
        storeInCurrentProject();
        emit scoreProjectionChanged();
    });
    const notation::INotationAutomationPtr automation = project->masterNotation()->automation();
    const engraving::AutomationDataConstPtr automationData = automation ? automation->automationData() : nullptr;
    if (automationData) {
        automationData->changed().onReceive(m_projectScope.get(), [this](const engraving::AutomationChanges&) {
            emit channelAutomationChanged();
        });
    }
    const notation::INotationPtr notation = project->masterNotation()->notation();
    if (!notation || !notation->interaction()) return;
    // The callback must never keep a closed notation alive.
    const std::weak_ptr<notation::INotation> weakNotation = notation;
    notation->interaction()->selectionChanged().onNotify(m_projectScope.get(), [this, weakNotation]() {
        // setProjectedNoteSelection() already knows the selection, and its primary note.
        if (m_selectingScoreNotes) return;
        const notation::INotationPtr current = weakNotation.lock();
        if (!current || !current->interaction()) return;
        QHash<engraving::Note*, QString> idsByNote;
        for (auto it = m_projectedNoteRefs.cbegin(); it != m_projectedNoteRefs.cend(); ++it) {
            idsByNote.insert(it.value(), it.key());
        }
        QStringList selectedIds;
        for (engraving::Note* selected : current->interaction()->selection()->notes()) {
            engraving::Note* note = selected->firstTiedNote();
            QString id = idsByNote.value(note);
            if (id.isEmpty() && note->eid().isValid()) {
                id = QString::fromStdString(note->eid().toStdString());
            }
            if (!id.isEmpty() && !selectedIds.contains(id)) {
                selectedIds.append(id);
            }
        }
        // Selection only changes highlight; it never requires re-projecting the score.
        setProjectedSelection(selectedIds, selectedIds.contains(m_selectedProjectedNoteId)
                              ? m_selectedProjectedNoteId : (selectedIds.isEmpty() ? QString() : selectedIds.first()));
    });
}
