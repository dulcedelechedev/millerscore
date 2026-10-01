/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <QAbstractListModel>
#include <QTimer>
#include <QVariantList>
#include <qqmlintegration.h>

#include "actions/iactionsdispatcher.h"
#include "async/asyncable.h"
#include "audio/iaudiodrivercontroller.h"
#include "audio/main/iplayback.h"
#include "global/concurrency/ringqueue.h"
#include "context/iglobalcontext.h"
#include "global/io/buffer.h"
#include "interactive/iinteractive.h"
#include "modularity/ioc.h"

#include "audiofiledecoder.h"

namespace mu::appshell {
//! Audio tracks, as in Reaper: audio files (WAV, MP3, FLAC) placed as clips on the score's timeline
//! and played with it. Clips are placed in ticks, so they stay on their bar when the tempo changes.
//! Each track is mixed into one buffer (clips at their times) and played by the audio engine as a
//! track of its own (volume, mute and effects like any other track). The files are referenced, not
//! copied: their paths are saved in the project's daw.json ("audioTracks").
class DawAudioTracksModel : public QAbstractListModel, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    //! Recording (phase 2): an armed track records the interface's input (ASIO) while the score plays
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool hasArmedTrack READ hasArmedTrack NOTIFY recordingChanged)
    Q_PROPERTY(double recordedSeconds READ recordedSeconds NOTIFY recordedSecondsChanged)

    QML_ELEMENT

public:
    enum Roles {
        TrackIdRole = Qt::UserRole + 1,
        NameRole,
        ColorIndexRole,
        MutedRole,
        VolumeRole,
        ClipsRole,
        ArmedRole
    };

    explicit DawAudioTracksModel(QObject* parent = nullptr);
    ~DawAudioTracksModel() override;

    Q_INVOKABLE void load();
    Q_INVOKABLE void addTrack();
    Q_INVOKABLE void removeTrack(int row);
    //! Opens a file dialog; the chosen file becomes a clip at `tick`
    Q_INVOKABLE void importAudio(int row, int tick);
    Q_INVOKABLE bool importAudioFile(int row, const QString& path, int tick);
    Q_INVOKABLE void moveClip(int row, const QString& clipId, int tick);
    Q_INVOKABLE void removeClip(int row, const QString& clipId);
    Q_INVOKABLE void toggleMute(int row);
    Q_INVOKABLE void setVolume(int row, double volumeDb);
    Q_INVOKABLE void renameTrack(int row, const QString& name);
    //! One armed track at a time: arming another disarms the previous one
    Q_INVOKABLE void toggleArm(int row);
    //! Starts playback and records the armed track from `tick`; again (or stopping playback) ends the take
    Q_INVOKABLE void toggleRecording(int tick);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    QString lastError() const;
    bool recording() const;
    bool hasArmedTrack() const;
    double recordedSeconds() const;

signals:
    void countChanged();
    void lastErrorChanged();
    void recordingChanged();
    void recordedSecondsChanged();

private:
    struct Clip {
        QString id;
        QString path;
        int startTick = 0;
        DecodedAudioPtr audio;
        QVariantList peaks;
    };

    struct Track {
        QString id;
        QString name;
        int colorIndex = 0;
        bool muted = false;
        bool armed = false;
        double volumeDb = 0.0;
        std::vector<Clip> clips;
        muse::audio::TrackId engineId = -1;
        std::shared_ptr<muse::io::Buffer> device;
        int generation = 0;
        std::vector<double> placedSeconds; // clip starts when the engine audio was built
    };

    void subscribeToProject();
    void clearEngineTracks();
    void readFromProject();
    void writeToProject();
    void syncTrack(Track& track, bool force);
    void syncAllIfTimingChanged();
    std::vector<double> clipStartSeconds(const Track& track) const;
    muse::ByteArray renderTrack(const Track& track, const std::vector<double>& starts) const;
    QVariantList clipsForQml(const Track& track) const;
    int endTickFor(const Clip& clip) const;
    void setError(const QString& error);
    void notifyRow(int row, const QVector<int>& roles);
    int rowOf(const QString& trackId) const;
    static QVariantList makePeaks(const DecodedAudio& audio);

    //! Shared with the driver's audio thread, which only pushes samples while `active`
    struct Capture {
        muse::RingQueue<float> queue { size_t(1) << 21 };
        std::atomic<bool> active { false };
        std::atomic<int> channels { 0 };
        std::atomic<int> sampleRate { 0 };
        std::atomic<bool> overflow { false };
    };

    void startCapture();
    void drainCapture();
    void finishRecording();
    QString takePath(const QString& trackName) const;

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::audio::IPlayback> playback = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::GlobalInject<muse::audio::IAudioDriverController> audioDriverController;

    std::unique_ptr<muse::async::Asyncable> m_projectScope;
    std::vector<Track> m_tracks;
    QString m_lastError;
    bool m_loading = false;

    std::shared_ptr<Capture> m_capture;
    bool m_recording = false;
    bool m_waitingForPlayback = false;
    int m_recordStartTick = 0;
    QString m_recordTrackId;
    std::vector<float> m_take;
    QTimer m_drainTimer;
};
}
