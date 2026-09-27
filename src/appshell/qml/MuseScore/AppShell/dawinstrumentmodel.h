/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "actions/iactionsdispatcher.h"
#include "async/asyncable.h"
#include "audio/main/iplayback.h"
#include "audio/main/isoundfontinstallscenario.h"
#include "context/iglobalcontext.h"
#include "interactive/iinteractive.h"
#include "modularity/ioc.h"
#include "playback/iplaybackcontroller.h"

namespace mu::appshell {
//! Control-plane facade over the existing playback resources for one score
//! instrument track. The track is chosen by DawTracksModel (the single
//! selection source); this model never owns a track list, a plug-in instance
//! or a second copy of the assignment, which stays in ProjectAudioSettings.
class DawInstrumentModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList resources READ resources NOTIFY resourcesChanged)
    Q_PROPERTY(int currentResourceIndex READ currentResourceIndex NOTIFY currentInstrumentChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentInstrumentChanged)
    Q_PROPERTY(QString currentBackend READ currentBackend NOTIFY currentInstrumentChanged)
    Q_PROPERTY(QString currentVendor READ currentVendor NOTIFY currentInstrumentChanged)
    Q_PROPERTY(LoadState loadState READ loadState NOTIFY loadStateChanged)
    Q_PROPERTY(bool editorAvailable READ editorAvailable NOTIFY currentInstrumentChanged)
    Q_PROPERTY(bool trackAvailable READ trackAvailable NOTIFY trackChanged)

    QML_ELEMENT

public:
    //! Only states the public playback API can actually confirm. "Assigned"
    //! means the resource is selected and installed, not that a vendor plug-in
    //! finished its private initialization.
    enum class LoadState {
        NoTrack,
        Scanning,
        Switching,
        Assigned,
        Missing,
        Failed
    };
    Q_ENUM(LoadState)

    explicit DawInstrumentModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void refreshResources();
    Q_INVOKABLE void installSoundFont();
    Q_INVOKABLE bool setTrack(const QString& partId, const QString& instrumentId);
    Q_INVOKABLE bool selectInstrument(const QString& resourceId);
    Q_INVOKABLE bool openEditor();

    QVariantList resources() const;
    int currentResourceIndex() const;
    QString currentTitle() const;
    QString currentBackend() const;
    QString currentVendor() const;
    LoadState loadState() const;
    bool editorAvailable() const;
    bool trackAvailable() const;

    static QString resourceTitle(const muse::audio::AudioResourceMeta& resource);
    static QString backendTitle(const muse::audio::AudioResourceMeta& resource);

signals:
    void trackChanged();
    void resourcesChanged();
    void currentInstrumentChanged();
    void loadStateChanged();

private:
    void reloadCurrentParams();
    void updateState();
    void setLoadState(LoadState state);
    muse::audio::TrackId runtimeTrackId() const;

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::audio::IPlayback> audioPlayback = { this };
    muse::ContextInject<muse::audio::ISoundFontInstallScenario> soundFontInstallScenario = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

    muse::audio::AudioResourceMetaList m_resources;
    engraving::InstrumentTrackId m_track;
    muse::audio::AudioInputParams m_currentParams;
    LoadState m_loadState = LoadState::NoTrack;
    bool m_resourcesResolved = false;
    bool m_resourcesFailed = false;
    bool m_switching = false;
};
}
