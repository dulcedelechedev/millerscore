/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 */

#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QHash>
#include <QList>
#include <QString>
#include <functional>
#include <memory>
#include <optional>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "modularity/ioc.h"
#include "types/translatablestring.h"

namespace mu::engraving {
class MasterScore;
class Note;
class PerformanceOverlay;
struct PerformanceNoteOverride;
}

namespace mu::appshell {
class DawProjectModel : public QAbstractListModel, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    Q_PROPERTY(int trackCount READ trackCount NOTIFY trackCountChanged)
    Q_PROPERTY(int playheadTick READ playheadTick WRITE setPlayheadTick NOTIFY playheadTickChanged)
    Q_PROPERTY(double tempo READ tempo WRITE setTempo NOTIFY tempoChanged)
    Q_PROPERTY(int timeSignatureNumerator READ timeSignatureNumerator WRITE setTimeSignatureNumerator NOTIFY timeSignatureChanged)
    Q_PROPERTY(int timeSignatureDenominator READ timeSignatureDenominator WRITE setTimeSignatureDenominator NOTIFY timeSignatureChanged)
    Q_PROPERTY(int snapTicks READ snapTicks WRITE setSnapTicks NOTIFY snapTicksChanged)
    Q_PROPERTY(QString selectedRegionId READ selectedRegionId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedNoteId READ selectedNoteId NOTIFY selectionChanged)
    Q_PROPERTY(EditMode editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)
    Q_PROPERTY(QString selectedProjectedNoteId READ selectedProjectedNoteId NOTIFY projectedSelectionChanged)
    Q_PROPERTY(QStringList selectedProjectedNoteIds READ selectedProjectedNoteIds NOTIFY projectedSelectionChanged)

    QML_ELEMENT

public:
    enum class EditMode {
        Notation,
        Performance
    };
    Q_ENUM(EditMode)

    explicit DawProjectModel(QObject* parent = nullptr);
    Q_INVOKABLE void load();

    enum Roles {
        TrackIdRole = Qt::UserRole + 1,
        NameRole,
        ColorRole,
        MutedRole,
        SoloRole,
        ArmedRole,
        RegionsRole
    };

    QVariant data(const QModelIndex& index, int role) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

    int trackCount() const;
    int playheadTick() const;
    void setPlayheadTick(int tick);
    double tempo() const;
    void setTempo(double tempo);
    int timeSignatureNumerator() const;
    void setTimeSignatureNumerator(int numerator);
    int timeSignatureDenominator() const;
    void setTimeSignatureDenominator(int denominator);
    int snapTicks() const;
    void setSnapTicks(int ticks);
    QString selectedRegionId() const;
    QString selectedNoteId() const;
    EditMode editMode() const;
    void setEditMode(EditMode mode);
    //! The primary selected note (the last one clicked); always one of selectedProjectedNoteIds()
    QString selectedProjectedNoteId() const;
    QStringList selectedProjectedNoteIds() const;

    Q_INVOKABLE QString addMidiTrack(const QString& name = QString());
    Q_INVOKABLE QString trackIdAt(int row) const;
    Q_INVOKABLE bool removeTrack(const QString& trackId);
    Q_INVOKABLE QString addMidiRegion(const QString& trackId, int startTick, int lengthTicks,
                                      const QString& name = QString());
    Q_INVOKABLE bool moveRegion(const QString& regionId, int startTick);
    Q_INVOKABLE bool moveRegionToTrack(const QString& regionId, const QString& trackId, int startTick);
    Q_INVOKABLE bool resizeRegion(const QString& regionId, int startTick, int lengthTicks);
    Q_INVOKABLE bool deleteRegion(const QString& regionId);
    Q_INVOKABLE bool selectRegion(const QString& regionId);
    Q_INVOKABLE QVariantMap selectedRegion() const;
    Q_INVOKABLE QVariantList notesForRegion(const QString& regionId) const;
    Q_INVOKABLE QVariantList notesStartingBetween(int fromTick, int toTick) const;
    // Score-linked piano-roll API. Legacy region APIs above remain temporarily for standalone MIDI regions.
    //! Notes of the notation tracks [startTrack, endTrack) - normally one part,
    //! all staves and voices. A tie chain is projected once, at its head note.
    Q_INVOKABLE QVariantList projectedScoreNotes(int startTrack, int endTrack);
    //! Several parts at once (several selected tracks): `ranges` holds { startTrack, endTrack }
    //! maps; each note also gets "rangeIndex", its range's index in `ranges`.
    Q_INVOKABLE QVariantList projectedScoreNotesForRanges(const QVariantList& ranges);
    Q_INVOKABLE bool selectProjectedNote(const QString& noteId);
    //! Selects several notes (score selection follows); `primaryId` becomes the
    //! primary note when it is among them, otherwise the last one is.
    Q_INVOKABLE bool setProjectedNoteSelection(const QStringList& noteIds, const QString& primaryId);
    //! Adds a sounding-pitch note to voice 1 of the staff containing `track`.
    //! Returns an empty string on success, otherwise a user-facing reason; a
    //! refused request never modifies the score.
    Q_INVOKABLE QString addScoreNote(int startTick, int durationTicks, int pitch, int track);
    //! Notes mode: moves score notes, as one undo step. `moves` holds { noteId, tick, pitch } (the
    //! written onset and sounding pitch they go to). Pitch-only moves change the notes in place;
    //! moves in time take the notes out and write them at their new onsets with the same duration.
    //! Returns an empty string on success, otherwise why nothing was changed.
    Q_INVOKABLE QString moveScoreNotes(const QVariantList& moves);
    Q_INVOKABLE bool setPerformanceVelocity(const QString& noteId, int velocity);
    Q_INVOKABLE bool resetPerformanceVelocity(const QString& noteId);
    Q_INVOKABLE bool setPerformancePitchOffset(const QString& noteId, int cents);
    Q_INVOKABLE bool resetPerformancePitchOffset(const QString& noteId);
    Q_INVOKABLE bool setPerformanceStartOffset(const QString& noteId, int offsetTicks);
    Q_INVOKABLE bool setPerformanceDuration(const QString& noteId, int durationTicks);
    Q_INVOKABLE bool resetPerformance(const QString& noteId);
    //! Several notes at once, as one undo step. The maps go from note id to value.
    Q_INVOKABLE bool setPerformanceVelocities(const QVariantMap& velocities);
    Q_INVOKABLE bool resetPerformanceVelocities(const QStringList& noteIds);
    Q_INVOKABLE bool setPerformancePitchOffsets(const QVariantMap& centsByNote);
    Q_INVOKABLE bool resetPerformancePitchOffsets(const QStringList& noteIds);
    Q_INVOKABLE bool setPerformanceStartOffsets(const QVariantMap& offsetsByNote);
    //! Back to the written position and length (timing and duration overrides removed)
    Q_INVOKABLE bool resetPerformanceTiming(const QStringList& noteIds);
    Q_INVOKABLE bool resetPerformances(const QStringList& noteIds);
    //! Instrument automation shown by the channel control lanes. Values are
    //! normalized to [0, 1] and persisted by the score's automation.json.
    Q_INVOKABLE QVariantList channelAutomationPoints(const QString& partId, const QString& instrumentId,
                                                     const QString& control) const;
    Q_INVOKABLE bool setChannelAutomationPoint(const QString& partId, const QString& instrumentId,
                                               const QString& control, int tick, double value);
    Q_INVOKABLE bool removeChannelAutomationPoint(const QString& partId, const QString& instrumentId,
                                                  const QString& control, int tick);
    Q_INVOKABLE bool resetChannelAutomation(const QString& partId, const QString& instrumentId,
                                            const QString& control);
    //! Several point edits as one undo step. Each edit: { op: "set" | "move" | "erase", tick, from (move),
    //! value [0, 1], shape: "linear" | "curve" | "step" (how the curve arrives at the point), bend [0, 1]
    //! (curve: the value halfway along the segment, as a fraction of its rise; 0.5 is a straight line) }.
    //! Points read by channelAutomationPoints() carry the same shape and bend.
    Q_INVOKABLE bool editChannelAutomation(const QString& partId, const QString& instrumentId,
                                           const QString& control, const QVariantList& edits);
    //! Typed MIDI 1.0 CC registry for searchable lane pickers. The stableId is
    //! persisted; translated display text is never used as an identity key.
    Q_INVOKABLE QVariantList midiControllerCatalog() const;
    Q_INVOKABLE QString addMidiNote(const QString& regionId, int startTick, int durationTicks,
                                    int pitch, int velocity = 80);
    Q_INVOKABLE bool moveMidiNote(const QString& noteId, int startTick, int pitch);
    Q_INVOKABLE bool resizeMidiNote(const QString& noteId, int durationTicks);
    Q_INVOKABLE bool setMidiNoteVelocity(const QString& noteId, int velocity);
    Q_INVOKABLE bool deleteMidiNote(const QString& noteId);
    Q_INVOKABLE bool selectMidiNote(const QString& noteId);
    Q_INVOKABLE int quantizeRegion(const QString& regionId, int gridTicks);
    Q_INVOKABLE int snappedTick(int tick) const;
    Q_INVOKABLE QString serialize() const;
    Q_INVOKABLE bool deserialize(const QString& json);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void toggleMute(const QString& trackId);
    Q_INVOKABLE void toggleSolo(const QString& trackId);
    Q_INVOKABLE void toggleArmed(const QString& trackId);

signals:
    void trackCountChanged();
    void playheadTickChanged();
    void tempoChanged();
    void timeSignatureChanged();
    void snapTicksChanged();
    void selectionChanged();
    void regionChanged(const QString& regionId);
    void notesChanged(const QString& regionId);
    void projectChanged();
    void editModeChanged();
    void projectedSelectionChanged();
    void scoreProjectionChanged();
    void channelAutomationChanged();

private:
    struct MidiNote {
        QString id;
        int startTick = 0;
        int durationTicks = 1;
        int pitch = 60;
        int velocity = 80;
    };

    struct Region {
        QString id;
        QString name;
        int startTick = 0;
        int lengthTicks = 0;
        QList<MidiNote> notes;
    };

    struct Track {
        QString id;
        QString name;
        QColor color;
        bool muted = false;
        bool solo = false;
        bool armed = false;
        QList<Region> regions;
    };

    Track* trackById(const QString& id);
    const Track* trackById(const QString& id) const;
    Region* regionById(const QString& id, int* trackRow = nullptr);
    const Region* regionById(const QString& id, int* trackRow = nullptr) const;
    MidiNote* noteById(const QString& id, Region** owner = nullptr, int* trackRow = nullptr);
    const MidiNote* noteById(const QString& id, const Region** owner = nullptr) const;
    QVariantMap regionForQml(const Region& region) const;
    QVariantList regionsForQml(const Track& track) const;
    void toggleTrackFlag(const QString& trackId, int role, bool Track::*flag);
    void notifyRegionChanged(int trackRow, const QString& regionId, bool notesOnly = false);
    static QString newId();
    void loadFromCurrentProject();
    void storeInCurrentProject();
    engraving::Note* resolveProjectedNote(const QString& noteId) const;
    QString persistentIdFor(engraving::Note* note);
    QVariantMap projectedNoteForQml(engraving::Note* note, const QString& id) const;
    void appendProjectedNotes(int startTrack, int endTrack, int rangeIndex, QVariantList& result, int& sessionOrdinal);
    //! Writes a note into the score inside the undo transaction the caller opened; nullptr and a
    //! user-facing `error` when the note cannot go there (the caller then rolls back)
    engraving::Note* insertScoreNote(engraving::MasterScore* score, int startTick, int durationTicks, int pitch, int track,
                                     QString& error);
    bool updatePerformanceOverride(const QString& noteId, const muse::TranslatableString& actionName,
                                   const std::function<void(engraving::PerformanceNoteOverride&)>& update);
    bool updatePerformanceOverrides(const QStringList& noteIds, const muse::TranslatableString& actionName,
                                    const std::function<void(const QString& noteId, engraving::PerformanceNoteOverride&)>& update);
    void setProjectedSelection(const QStringList& noteIds, const QString& primaryId);
    //! The master score's performance layer: the single owner of overrides.
    engraving::PerformanceOverlay* performanceOverlay() const;
    void subscribeToScoreChanges();

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

    //! Subscriptions tied to the current project; replaced (and thereby
    //! disconnected) whenever the project changes.
    std::unique_ptr<muse::async::Asyncable> m_projectScope;
    QList<Track> m_tracks;
    int m_playheadTick = 0;
    double m_tempo = 120.0;
    int m_timeSignatureNumerator = 4;
    int m_timeSignatureDenominator = 4;
    int m_snapTicks = 120;
    QString m_selectedRegionId;
    QString m_selectedNoteId;
    bool m_loadingProject = false;
    EditMode m_editMode = EditMode::Notation;
    QString m_selectedProjectedNoteId;
    QStringList m_selectedProjectedNoteIds;
    bool m_selectingScoreNotes = false;
    mutable QHash<QString, engraving::Note*> m_projectedNoteRefs;
};
}
