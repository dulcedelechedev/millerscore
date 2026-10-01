/* SPDX-License-Identifier: GPL-3.0-only */

#include "dawtracksmodel.h"

#include <algorithm>

#include <QMetaObject>

#include "dawinstrumentmodel.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "project/inotationproject.h"
#include "project/iprojectaudiosettings.h"

using namespace muse;
using namespace mu;
using namespace mu::appshell;

namespace {
//! Upper bound for preview notes per clip; keeps arranger delegates cheap on
//! dense orchestral parts. Previews are sampled, editing always uses the roll.
constexpr int MAX_PREVIEW_NOTES_PER_CLIP = 600;
const engraving::Fraction SCORE_START(0, 1);
}

DawTracksModel::DawTracksModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void DawTracksModel::load()
{
    subscribeToProject();
    globalContext()->currentMasterNotationChanged().onNotify(this, [this]() { subscribeToProject(); });
    // Parts (excerpts) keep their own mute/solo state, exactly as in the Mixer.
    globalContext()->currentNotationChanged().onNotify(this, [this]() { subscribeToProject(); });

    playbackController()->trackAdded().onReceive(this, [this](audio::TrackId) { refreshSounds(); });
    audioPlayback()->sourceParamsChanged().onReceive(this, [this](audio::TrackId, const audio::AudioSourceParams&) {
        refreshSounds();
    });
}

void DawTracksModel::subscribeToProject()
{
    m_projectScope = std::make_unique<muse::async::Asyncable>();
    rebuild();

    const notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* score = masterScore();
    if (!master || !score) {
        return;
    }

    score->changesChannel().onReceive(m_projectScope.get(), [this](const engraving::ScoreChanges&) { scheduleRebuild(); });

    const notation::INotationPtr notation = globalContext()->currentNotation();
    if (notation && notation->soloMuteState()) {
        notation->soloMuteState()->trackSoloMuteStateChanged().onReceive(
            m_projectScope.get(), [this](const engraving::InstrumentTrackId&, const notation::INotationSoloMuteState::SoloMuteState&) {
            refreshSoloMute();
        });
    }
}

void DawTracksModel::scheduleRebuild()
{
    if (m_rebuildPending) {
        return;
    }
    // Coalesce the several change notifications one edit can produce.
    m_rebuildPending = true;
    QMetaObject::invokeMethod(this, [this]() {
        m_rebuildPending = false;
        rebuild();
    }, Qt::QueuedConnection);
}

engraving::MasterScore* DawTracksModel::masterScore() const
{
    const notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    return master ? master->masterScore() : nullptr;
}

void DawTracksModel::rebuild()
{
    const engraving::MasterScore* score = masterScore();
    const int previousCount = int(m_tracks.size());

    QList<Track> tracks;
    if (score) {
        int colorIndex = 0;
        for (const engraving::Part* part : score->parts()) {
            if (!part || part->instruments().empty()) {
                continue;
            }
            const engraving::Instrument* instrument = part->instrument(SCORE_START);
            if (!instrument) {
                continue;
            }
            Track track;
            track.id = { part->id(), instrument->id() };
            track.name = part->partName().toQString();
            if (track.name.isEmpty()) {
                track.name = instrument->trackName().toQString();
            }
            track.startTrack = int(part->trackRange().startTrack);
            track.endTrack = int(part->trackRange().endTrack);
            track.colorIndex = colorIndex++;
            track.clips = buildClips(score, part, track.noteCount);
            track.drawTargetError = drawTargetError(score, part);
            tracks.append(track);
        }
    }

    beginResetModel();
    m_tracks = tracks;
    endResetModel();

    const auto exists = [this](const engraving::InstrumentTrackId& id) {
        return std::any_of(m_tracks.cbegin(), m_tracks.cend(), [&id](const Track& track) { return track.id == id; });
    };
    m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(),
                                     [&exists](const engraving::InstrumentTrackId& id) { return !exists(id); }),
                      m_selection.end());
    if (!exists(m_selected)) {
        m_selected = !m_selection.empty() ? m_selection.back()
                     : (m_tracks.isEmpty() ? engraving::InstrumentTrackId() : m_tracks.first().id);
    }
    if (m_selected.isValid() && !isSelected(m_selected)) {
        m_selection.push_back(m_selected);
    }

    if (previousCount != int(m_tracks.size())) {
        emit countChanged();
    }
    emit selectionChanged();
    emit selectedStateChanged();
}

QVariantList DawTracksModel::buildClips(const engraving::MasterScore* score, const engraving::Part* part, int& noteCount)
{
    QVariantList clips;
    noteCount = 0;
    if (!score || !part) {
        return clips;
    }

    const engraving::track_idx_t startTrack = part->trackRange().startTrack;
    const engraving::track_idx_t endTrack = part->trackRange().endTrack;

    int clipStart = -1;
    int clipEnd = -1;
    QString firstBar;
    QString lastBar;
    int minPitch = 127;
    int maxPitch = 0;
    QVariantList notes;

    auto flush = [&]() {
        if (clipStart < 0) {
            return;
        }
        // Sample very dense clips uniformly instead of truncating their end.
        const qsizetype noteTotal = notes.size() / 3;
        if (noteTotal > MAX_PREVIEW_NOTES_PER_CLIP) {
            QVariantList sampled;
            sampled.reserve(MAX_PREVIEW_NOTES_PER_CLIP * 3);
            const double step = double(noteTotal) / MAX_PREVIEW_NOTES_PER_CLIP;
            for (int i = 0; i < MAX_PREVIEW_NOTES_PER_CLIP; ++i) {
                const qsizetype first = qsizetype(i * step) * 3;
                sampled << notes.at(first) << notes.at(first + 1) << notes.at(first + 2);
            }
            notes = sampled;
        }
        clips.append(QVariantMap {
            { "startTick", clipStart }, { "endTick", clipEnd }, { "firstBar", firstBar }, { "lastBar", lastBar },
            { "minPitch", minPitch }, { "maxPitch", maxPitch }, { "notes", notes }
        });
        clipStart = -1;
        minPitch = 127;
        maxPitch = 0;
        notes.clear();
    };

    for (const engraving::Measure* measure = score->firstMeasure(); measure; measure = measure->nextMeasure()) {
        bool measureHasNotes = false;
        for (const engraving::Segment* segment = measure->first(engraving::SegmentType::ChordRest); segment;
             segment = segment->next(engraving::SegmentType::ChordRest)) {
            for (engraving::track_idx_t track = startTrack; track < endTrack; ++track) {
                const engraving::EngravingItem* item = segment->element(track);
                if (!item || !item->isChord()) {
                    continue;
                }
                const engraving::Chord* chord = static_cast<const engraving::Chord*>(item);
                const int tick = chord->tick().ticks();
                const int duration = std::max(1, chord->actualTicks().ticks());
                for (const engraving::Note* note : chord->notes()) {
                    if (!measureHasNotes) {
                        measureHasNotes = true;
                        if (clipStart < 0) {
                            clipStart = measure->tick().ticks();
                            firstBar = QString::number(measure->measureNumber() + 1);
                        }
                    }
                    const int pitch = std::clamp(note->pitch(), 0, 127);
                    minPitch = std::min(minPitch, pitch);
                    maxPitch = std::max(maxPitch, pitch);
                    // Flat triples (offset, duration, pitch) keep the preview payload small.
                    notes << (tick - clipStart) << duration << pitch;
                    ++noteCount;
                }
            }
        }
        if (measureHasNotes) {
            clipEnd = measure->endTick().ticks();
            lastBar = QString::number(measure->measureNumber() + 1);
        } else {
            flush();
        }
    }
    flush();
    return clips;
}

QString DawTracksModel::drawTargetError(const engraving::MasterScore* score, const engraving::Part* part)
{
    if (!score || !part || part->staves().empty()) {
        return QObject::tr("This part has no staff to write to.");
    }
    const engraving::Staff* staff = part->staves().front();
    if (staff->isTabStaff(SCORE_START)) {
        return QObject::tr("Tablature is edited in Score mode for now.");
    }
    return {};
}

void DawTracksModel::refreshSoloMute()
{
    if (!m_tracks.isEmpty()) {
        emit dataChanged(index(0), index(rowCount() - 1), { MutedRole, SoloRole });
    }
    emit selectedStateChanged();
}

void DawTracksModel::refreshSounds()
{
    if (!m_tracks.isEmpty()) {
        emit dataChanged(index(0), index(rowCount() - 1), { SoundRole });
    }
    emit selectedStateChanged();
}

bool DawTracksModel::selectTrack(int row)
{
    return clickTrack(row, Qt::NoModifier);
}

bool DawTracksModel::clickTrack(int row, int modifiers)
{
    if (row < 0 || row >= m_tracks.size()) {
        return false;
    }
    const engraving::InstrumentTrackId clicked = m_tracks.at(row).id;

    if ((modifiers & Qt::ShiftModifier) && m_selected.isValid()) {
        const int anchor = selectedIndex();
        std::vector<engraving::InstrumentTrackId> range;
        for (int index = std::min(anchor, row); index <= std::max(anchor, row); ++index) {
            range.push_back(m_tracks.at(index).id);
        }
        setSelection(range, clicked);
        return true;
    }

    if (modifiers & Qt::ControlModifier) {
        std::vector<engraving::InstrumentTrackId> selection = m_selection;
        const auto it = std::find(selection.begin(), selection.end(), clicked);
        if (it == selection.end()) {
            selection.push_back(clicked);
            setSelection(selection, clicked);
        } else if (selection.size() > 1) {
            // The last selected track is never removed: there is always a track to show and draw into.
            selection.erase(it);
            setSelection(selection, clicked == m_selected ? selection.back() : m_selected);
        }
        return true;
    }

    setSelection({ clicked }, clicked);
    return true;
}

void DawTracksModel::setSelection(const std::vector<engraving::InstrumentTrackId>& selection,
                                  const engraving::InstrumentTrackId& primary)
{
    if (selection == m_selection && primary == m_selected) {
        return;
    }
    const bool primaryChanged = primary != m_selected;
    m_selection = selection;
    m_selected = primary;
    emit dataChanged(index(0), index(rowCount() - 1), { SelectedRole });
    emit selectionChanged();
    if (primaryChanged) {
        emit selectedStateChanged();
    }
}

bool DawTracksModel::isSelected(const engraving::InstrumentTrackId& id) const
{
    return std::find(m_selection.cbegin(), m_selection.cend(), id) != m_selection.cend();
}

QVariantList DawTracksModel::selectedTracks() const
{
    QVariantList result;
    for (int row = 0; row < m_tracks.size(); ++row) {
        const Track& track = m_tracks.at(row);
        if (!isSelected(track.id)) {
            continue;
        }
        result.append(QVariantMap {
            { "row", row },
            { "partId", track.id.partId.toQString() },
            { "instrumentId", track.id.instrumentId.toQString() },
            { "name", track.name },
            { "startTrack", track.startTrack },
            { "endTrack", track.endTrack },
            { "colorIndex", track.colorIndex },
            { "primary", track.id == m_selected },
        });
    }
    return result;
}

playback::IPlaybackController::SoloMuteState DawTracksModel::soloMuteState(int row) const
{
    const notation::INotationPtr notation = globalContext()->currentNotation();
    if (row < 0 || row >= m_tracks.size() || !notation || !notation->soloMuteState()) {
        return {};
    }
    return notation->soloMuteState()->trackSoloMuteState(m_tracks.at(row).id);
}

void DawTracksModel::toggleMute(int row)
{
    if (row < 0 || row >= m_tracks.size() || !globalContext()->currentNotation()) {
        return;
    }
    playback::IPlaybackController::SoloMuteState state = soloMuteState(row);
    state.mute = !state.mute;
    playbackController()->setTrackSoloMuteState(m_tracks.at(row).id, state);
    refreshSoloMute();
}

void DawTracksModel::toggleSolo(int row)
{
    if (row < 0 || row >= m_tracks.size() || !globalContext()->currentNotation()) {
        return;
    }
    playback::IPlaybackController::SoloMuteState state = soloMuteState(row);
    state.solo = !state.solo;
    playbackController()->setTrackSoloMuteState(m_tracks.at(row).id, state);
    refreshSoloMute();
}

void DawTracksModel::openInstrumentsDialog()
{
    dispatcher()->dispatch("instruments");
}

void DawTracksModel::openMixer()
{
    dispatcher()->dispatch("toggle-mixer");
}

void DawTracksModel::toggleFullScreen()
{
    dispatcher()->dispatch("fullscreen");
}

QString DawTracksModel::soundTitle(const engraving::InstrumentTrackId& id) const
{
    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (!project || !project->audioSettings()) {
        return {};
    }
    return DawInstrumentModel::resourceTitle(project->audioSettings()->trackInputParams(id).resourceMeta);
}

int DawTracksModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_tracks.size());
}

QVariant DawTracksModel::data(const QModelIndex& modelIndex, int role) const
{
    if (!modelIndex.isValid() || modelIndex.row() < 0 || modelIndex.row() >= rowCount()) {
        return {};
    }
    const Track& track = m_tracks.at(modelIndex.row());
    switch (role) {
    case PartIdRole: return track.id.partId.toQString();
    case InstrumentIdRole: return track.id.instrumentId.toQString();
    case NameRole: return track.name;
    case SoundRole: return soundTitle(track.id);
    case ColorIndexRole: return track.colorIndex;
    case MutedRole: return soloMuteState(modelIndex.row()).mute;
    case SoloRole: return soloMuteState(modelIndex.row()).solo;
    case SelectedRole: return isSelected(track.id);
    case NoteCountRole: return track.noteCount;
    case ClipsRole: return track.clips;
    default: return {};
    }
}

QHash<int, QByteArray> DawTracksModel::roleNames() const
{
    return {
        { PartIdRole, "partId" }, { InstrumentIdRole, "instrumentId" }, { NameRole, "name" },
        { SoundRole, "sound" }, { ColorIndexRole, "colorIndex" }, { MutedRole, "muted" },
        { SoloRole, "solo" }, { SelectedRole, "selected" }, { NoteCountRole, "noteCount" }, { ClipsRole, "clips" }
    };
}

int DawTracksModel::count() const { return int(m_tracks.size()); }
bool DawTracksModel::hasScore() const { return masterScore() != nullptr; }

const DawTracksModel::Track* DawTracksModel::selectedTrack() const
{
    const auto it = std::find_if(m_tracks.cbegin(), m_tracks.cend(), [this](const Track& track) { return track.id == m_selected; });
    return it == m_tracks.cend() ? nullptr : &*it;
}

int DawTracksModel::selectedIndex() const
{
    const Track* track = selectedTrack();
    return track ? int(track - m_tracks.data()) : -1;
}

QString DawTracksModel::selectedPartId() const
{
    const Track* track = selectedTrack();
    return track ? track->id.partId.toQString() : QString();
}

QString DawTracksModel::selectedInstrumentId() const
{
    const Track* track = selectedTrack();
    return track ? track->id.instrumentId.toQString() : QString();
}

bool DawTracksModel::selectedSupportsPerNoteVelocity() const
{
    const Track* track = selectedTrack();
    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (!track || !project || !project->audioSettings()) {
        return false;
    }
    const audio::AudioResourceMeta& resource = project->audioSettings()->trackInputParams(track->id).resourceMeta;
    return !audio::isResourceType(resource, audio::AudioResourceType::MuseSamplerSoundPack);
}

QString DawTracksModel::selectedName() const
{
    const Track* track = selectedTrack();
    return track ? track->name : QString();
}

int DawTracksModel::selectedColorIndex() const
{
    const Track* track = selectedTrack();
    return track ? track->colorIndex : 0;
}

int DawTracksModel::selectedStartTrack() const
{
    const Track* track = selectedTrack();
    return track ? track->startTrack : -1;
}

int DawTracksModel::selectedEndTrack() const
{
    const Track* track = selectedTrack();
    return track ? track->endTrack : -1;
}

QString DawTracksModel::selectedDrawTargetError() const
{
    const Track* track = selectedTrack();
    return track ? track->drawTargetError : QString();
}

bool DawTracksModel::selectedMuted() const
{
    return soloMuteState(selectedIndex()).mute;
}

bool DawTracksModel::selectedSolo() const
{
    return soloMuteState(selectedIndex()).solo;
}
