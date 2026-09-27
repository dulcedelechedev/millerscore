/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <memory>

#include <QAbstractListModel>
#include <QVariantList>
#include <qqmlintegration.h>

#include "actions/iactionsdispatcher.h"
#include "async/asyncable.h"
#include "audio/main/iplayback.h"
#include "context/iglobalcontext.h"
#include "engraving/types/types.h"
#include "modularity/ioc.h"
#include "playback/iplaybackcontroller.h"

namespace mu::engraving {
class MasterScore;
class Part;
}

namespace mu::appshell {
//! One DAW track per score part, in score order. The score is the only source
//! of tracks; this model owns nothing but the current track selection, which is
//! the single selection context consumed by the arranger, piano roll and
//! inspector. Mute/solo read and write the same state as the Mixer.
class DawTracksModel : public QAbstractListModel, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool hasScore READ hasScore NOTIFY countChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedPartId READ selectedPartId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedInstrumentId READ selectedInstrumentId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged)
    Q_PROPERTY(int selectedColorIndex READ selectedColorIndex NOTIFY selectionChanged)
    Q_PROPERTY(int selectedStartTrack READ selectedStartTrack NOTIFY selectionChanged)
    Q_PROPERTY(int selectedEndTrack READ selectedEndTrack NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedDrawTargetError READ selectedDrawTargetError NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedMuted READ selectedMuted NOTIFY selectedStateChanged)
    Q_PROPERTY(bool selectedSolo READ selectedSolo NOTIFY selectedStateChanged)
    Q_PROPERTY(bool selectedSupportsPerNoteVelocity READ selectedSupportsPerNoteVelocity NOTIFY selectedStateChanged)
    //! Every selected track, in track order: { row, partId, instrumentId, name, startTrack, endTrack,
    //! colorIndex, primary }. The primary track (the selected* properties) is the one last clicked.
    Q_PROPERTY(QVariantList selectedTracks READ selectedTracks NOTIFY selectionChanged)

    QML_ELEMENT

public:
    enum Roles {
        PartIdRole = Qt::UserRole + 1,
        InstrumentIdRole,
        NameRole,
        SoundRole,
        ColorIndexRole,
        MutedRole,
        SoloRole,
        SelectedRole,
        NoteCountRole,
        ClipsRole
    };

    explicit DawTracksModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE bool selectTrack(int index);
    //! A click on a track: alone it selects only that track, Ctrl adds or removes it,
    //! Shift selects every track from the primary one to it
    Q_INVOKABLE bool clickTrack(int index, int modifiers);
    Q_INVOKABLE void toggleMute(int index);
    Q_INVOKABLE void toggleSolo(int index);
    Q_INVOKABLE void openInstrumentsDialog();
    Q_INVOKABLE void openMixer();
    //! The application's own full-screen toggle
    Q_INVOKABLE void toggleFullScreen();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    bool hasScore() const;
    int selectedIndex() const;
    QString selectedPartId() const;
    QString selectedInstrumentId() const;
    QString selectedName() const;
    int selectedColorIndex() const;
    int selectedStartTrack() const;
    int selectedEndTrack() const;
    QString selectedDrawTargetError() const;
    bool selectedMuted() const;
    bool selectedSolo() const;
    bool selectedSupportsPerNoteVelocity() const;
    QVariantList selectedTracks() const;

signals:
    void countChanged();
    void selectionChanged();
    void selectedStateChanged();

private:
    struct Track {
        engraving::InstrumentTrackId id;
        QString name;
        int startTrack = 0;
        int endTrack = 0;
        int colorIndex = 0;
        int noteCount = 0;
        QString drawTargetError;
        QVariantList clips;
    };

    void subscribeToProject();
    void scheduleRebuild();
    void rebuild();
    void refreshSoloMute();
    void refreshSounds();
    engraving::MasterScore* masterScore() const;
    const Track* selectedTrack() const;
    bool isSelected(const engraving::InstrumentTrackId& id) const;
    void setSelection(const std::vector<engraving::InstrumentTrackId>& selection, const engraving::InstrumentTrackId& primary);
    playback::IPlaybackController::SoloMuteState soloMuteState(int row) const;
    QString soundTitle(const engraving::InstrumentTrackId& id) const;
    static QVariantList buildClips(const engraving::MasterScore* score, const engraving::Part* part, int& noteCount);
    static QString drawTargetError(const engraving::MasterScore* score, const engraving::Part* part);

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::audio::IPlayback> audioPlayback = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };

    //! Subscriptions tied to the current project. Replacing this object
    //! disconnects every callback that could otherwise run against a closed score.
    std::unique_ptr<muse::async::Asyncable> m_projectScope;
    QList<Track> m_tracks;
    //! The primary track, always one of m_selection when anything is selected
    engraving::InstrumentTrackId m_selected;
    std::vector<engraving::InstrumentTrackId> m_selection;
    bool m_rebuildPending = false;
};
}
