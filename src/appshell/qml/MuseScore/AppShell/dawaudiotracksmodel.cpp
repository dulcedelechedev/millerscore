/* SPDX-License-Identifier: GPL-3.0-only */
#include "dawaudiotracksmodel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>

#include "engraving/dom/masterscore.h"
#include "notation/imasternotation.h"
#include "notation/inotationplayback.h"
#include "project/inotationproject.h"

#include "log.h"

using namespace mu::appshell;
using namespace muse;
using namespace muse::audio;

namespace {
constexpr int PEAK_FRAMES = 1024;       // one waveform peak per this many frames
constexpr int RENDER_CHANNELS = 2;
constexpr double MAX_TRACK_SECONDS = 60.0 * 30; // longer tracks are cut (memory: ~690 MB at 48 kHz)

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void appendU32(ByteArray& bytes, uint32_t value)
{
    const uint8_t le[4] = { uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24) };
    bytes.push_back(le, 4);
}
}

DawAudioTracksModel::DawAudioTracksModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    m_drainTimer.setInterval(40);
    connect(&m_drainTimer, &QTimer::timeout, this, &DawAudioTracksModel::drainCapture);
}

DawAudioTracksModel::~DawAudioTracksModel()
{
    if (m_capture) {
        m_capture->active = false;
        audioDriverController()->setInputCallback(nullptr);
    }
    clearEngineTracks();
}

void DawAudioTracksModel::load()
{
    subscribeToProject();
    globalContext()->currentProjectChanged().onNotify(this, [this]() { subscribeToProject(); });

    // The score's playback clears every engine track when it (re)builds its own (setupPlayback);
    // put ours back. Our own removals clear engineId first, so they never come back here.
    if (playback()) {
        playback()->trackRemoved().onReceive(this, [this](const TrackId engineId) {
            for (Track& track : m_tracks) {
                if (track.engineId == engineId) {
                    track.engineId = -1;
                    const QString id = track.id;
                    QTimer::singleShot(0, this, [this, id]() {
                        const int row = rowOf(id);
                        if (row >= 0 && m_tracks[size_t(row)].engineId < 0) {
                            syncTrack(m_tracks[size_t(row)], true);
                        }
                    });
                }
            }
        });
    }
}

void DawAudioTracksModel::subscribeToProject()
{
    m_projectScope = std::make_unique<muse::async::Asyncable>();
    clearEngineTracks();
    readFromProject();

    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (!project || !project->masterNotation() || !project->masterNotation()->masterScore()) {
        return;
    }
    // A tempo change moves the clips in time: rebuild the tracks whose clips moved.
    project->masterNotation()->masterScore()->changesChannel().onReceive(m_projectScope.get(), [this](const engraving::ScoreChanges&) {
        syncAllIfTimingChanged();
    });
}

void DawAudioTracksModel::clearEngineTracks()
{
    for (Track& track : m_tracks) {
        const TrackId engineId = track.engineId;
        track.engineId = -1;
        if (engineId >= 0 && playback()) {
            playback()->removeTrack(engineId);
        }
        track.device.reset();
        ++track.generation;
    }
}

// ---- project storage ---------------------------------------------------------------------------

void DawAudioTracksModel::readFromProject()
{
    beginResetModel();
    m_tracks.clear();
    const project::INotationProjectPtr project = globalContext()->currentProject();
    QStringList missing;
    if (project) {
        m_loading = true;
        const QJsonObject root = QJsonDocument::fromJson(project->dawData().toQByteArrayNoCopy()).object();
        for (const QJsonValue& trackValue : root.value("audioTracks").toArray()) {
            const QJsonObject object = trackValue.toObject();
            Track track;
            track.id = object.value("id").toString(newId());
            track.name = object.value("name").toString(tr("Audio"));
            track.colorIndex = object.value("colorIndex").toInt(int(m_tracks.size()));
            track.muted = object.value("muted").toBool();
            track.volumeDb = object.value("volumeDb").toDouble();
            for (const QJsonValue& clipValue : object.value("clips").toArray()) {
                const QJsonObject clipObject = clipValue.toObject();
                Clip clip;
                clip.id = clipObject.value("id").toString(newId());
                clip.path = clipObject.value("path").toString();
                clip.startTick = std::max(0, clipObject.value("startTick").toInt());
                QString error;
                clip.audio = decodeAudioFile(clip.path, error);
                if (!clip.audio) {
                    missing << QFileInfo(clip.path).fileName();
                    continue; // kept out of playback, and out of the saved project from now on
                }
                clip.peaks = makePeaks(*clip.audio);
                track.clips.push_back(std::move(clip));
            }
            m_tracks.push_back(std::move(track));
        }
        m_loading = false;
    }
    endResetModel();
    emit countChanged();

    for (Track& track : m_tracks) {
        syncTrack(track, true);
    }
    if (!missing.isEmpty()) {
        setError(tr("These audio files could not be found or read: %1").arg(missing.join(", ")));
    }
}

void DawAudioTracksModel::writeToProject()
{
    if (m_loading) {
        return;
    }
    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    QJsonObject root = QJsonDocument::fromJson(project->dawData().toQByteArrayNoCopy()).object();
    if (root.isEmpty() && m_tracks.empty()) {
        return; // ordinary scores stay free of DAW data
    }
    QJsonArray tracks;
    for (const Track& track : m_tracks) {
        QJsonArray clips;
        for (const Clip& clip : track.clips) {
            clips.append(QJsonObject { { "id", clip.id }, { "path", clip.path }, { "startTick", clip.startTick } });
        }
        tracks.append(QJsonObject {
            { "id", track.id }, { "name", track.name }, { "colorIndex", track.colorIndex },
            { "muted", track.muted }, { "volumeDb", track.volumeDb }, { "clips", clips }
        });
    }
    root.insert("audioTracks", tracks);
    if (!root.contains("schemaVersion")) {
        root.insert("schemaVersion", 2);
    }
    project->setDawData(ByteArray::fromQByteArray(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

// ---- engine ------------------------------------------------------------------------------------

std::vector<double> DawAudioTracksModel::clipStartSeconds(const Track& track) const
{
    std::vector<double> starts;
    const notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    for (const Clip& clip : track.clips) {
        starts.push_back(master && master->playback() ? double(master->playback()->playedTickToSec(clip.startTick)) : 0.0);
    }
    return starts;
}

int DawAudioTracksModel::endTickFor(const Clip& clip) const
{
    const notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master || !master->playback() || !clip.audio) {
        return clip.startTick;
    }
    const double start = master->playback()->playedTickToSec(clip.startTick);
    return std::max(clip.startTick + 1, int(master->playback()->secToTick(start + clip.audio->seconds())));
}

//! The track's clips mixed at their times, stereo, at the highest rate among them
ByteArray DawAudioTracksModel::renderTrack(const Track& track, const std::vector<double>& starts) const
{
    int rate = 0;
    double end = 0.0;
    for (size_t index = 0; index < track.clips.size(); ++index) {
        rate = std::max(rate, track.clips[index].audio->sampleRate);
        end = std::max(end, starts[index] + track.clips[index].audio->seconds());
    }
    end = std::min(end, MAX_TRACK_SECONDS);
    const size_t totalFrames = rate > 0 ? size_t(std::ceil(end * rate)) : 0;
    std::vector<float> mix(totalFrames * RENDER_CHANNELS, 0.f);

    for (size_t index = 0; index < track.clips.size(); ++index) {
        const DecodedAudio& audio = *track.clips[index].audio;
        const double ratio = double(audio.sampleRate) / double(rate);
        const size_t sourceFrames = audio.frames();
        const int64_t offset = int64_t(std::llround(starts[index] * rate));
        const size_t clipFrames = size_t(std::floor(sourceFrames / ratio));
        for (size_t frame = 0; frame < clipFrames; ++frame) {
            const int64_t target = offset + int64_t(frame);
            if (target < 0 || size_t(target) >= totalFrames) {
                continue;
            }
            const double position = frame * ratio;
            const size_t at = std::min(size_t(position), sourceFrames - 1);
            const size_t next = std::min(at + 1, sourceFrames - 1);
            const float fraction = float(position - double(at));
            for (int channel = 0; channel < RENDER_CHANNELS; ++channel) {
                const int sourceChannel = audio.channels == 1 ? 0 : std::min(channel, audio.channels - 1);
                const float a = audio.samples[at * audio.channels + sourceChannel];
                const float b = audio.samples[next * audio.channels + sourceChannel];
                mix[size_t(target) * RENDER_CHANNELS + channel] += a + (b - a) * fraction;
            }
        }
    }

    // See PcmAudioNode (muse patch): "MSPC", version, sample rate, channels, then float32 frames
    ByteArray bytes;
    bytes.reserve(16 + mix.size() * sizeof(float));
    bytes.push_back(reinterpret_cast<const uint8_t*>("MSPC"), 4);
    appendU32(bytes, 1);
    appendU32(bytes, uint32_t(rate));
    appendU32(bytes, RENDER_CHANNELS);
    bytes.push_back(reinterpret_cast<const uint8_t*>(mix.data()), mix.size() * sizeof(float));
    return bytes;
}

void DawAudioTracksModel::syncTrack(Track& track, bool force)
{
    const std::vector<double> starts = clipStartSeconds(track);
    if (!force && starts == track.placedSeconds) {
        return;
    }
    if (track.engineId >= 0) {
        const TrackId engineId = track.engineId;
        track.engineId = -1;
        playback()->removeTrack(engineId);
    }
    track.placedSeconds = starts;
    const int generation = ++track.generation;
    track.device.reset();
    if (track.clips.empty() || !playback()) {
        return;
    }

    track.device = std::make_shared<io::Buffer>(renderTrack(track, starts));
    track.device->open(io::IODevice::ReadOnly);

    TrackParams params;
    params.control.volume = AutomatableValue<volume_db_t>(volume_db_t(track.volumeDb));
    params.control.muted = track.muted;

    const QString id = track.id;
    std::shared_ptr<io::Buffer> device = track.device;
    playback()->addTrack(track.name.toStdString(), device.get(), params)
    .onResolve(this, [this, id, generation, device](const TrackId engineId, const TrackParams&) {
        const int row = rowOf(id);
        if (row < 0 || m_tracks[size_t(row)].generation != generation) {
            playback()->removeTrack(engineId); // superseded while the engine was adding it
            return;
        }
        m_tracks[size_t(row)].engineId = engineId;
        LOGI() << "audio track " << m_tracks[size_t(row)].name << " playing as engine track " << engineId;
    })
    .onReject(this, [this](int code, const std::string& message) {
        setError(tr("The audio track could not be played (%1: %2)").arg(code).arg(QString::fromStdString(message)));
    });
}

void DawAudioTracksModel::syncAllIfTimingChanged()
{
    for (size_t row = 0; row < m_tracks.size(); ++row) {
        const std::vector<double> starts = clipStartSeconds(m_tracks[row]);
        if (starts != m_tracks[row].placedSeconds) {
            syncTrack(m_tracks[row], true);
            notifyRow(int(row), { ClipsRole }); // clip lengths in ticks follow the tempo too
        }
    }
}

// ---- editing -----------------------------------------------------------------------------------

void DawAudioTracksModel::addTrack()
{
    const int row = int(m_tracks.size());
    beginInsertRows(QModelIndex(), row, row);
    Track track;
    track.id = newId();
    track.name = tr("Audio %1").arg(row + 1);
    track.colorIndex = row;
    m_tracks.push_back(std::move(track));
    endInsertRows();
    emit countChanged();
    writeToProject();
}

void DawAudioTracksModel::removeTrack(int row)
{
    if (row < 0 || row >= int(m_tracks.size())) {
        return;
    }
    Track& track = m_tracks[size_t(row)];
    const TrackId engineId = track.engineId;
    track.engineId = -1;
    if (engineId >= 0) {
        playback()->removeTrack(engineId);
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_tracks.erase(m_tracks.begin() + row);
    endRemoveRows();
    emit countChanged();
    writeToProject();
}

void DawAudioTracksModel::importAudio(int row, int tick)
{
    if (row < 0 || row >= int(m_tracks.size())) {
        return;
    }
    const QString trackId = m_tracks[size_t(row)].id;
    const std::vector<std::string> filter = {
        tr("Audio files").toStdString() + " (*.wav *.mp3 *.flac)",
        tr("All files").toStdString() + " (*)"
    };
    interactive()->selectOpeningFile(tr("Import audio").toStdString(), io::path_t(), filter)
    .onResolve(this, [this, trackId, tick](const io::path_t& path) {
        const int current = rowOf(trackId);
        if (!path.empty() && current >= 0) {
            importAudioFile(current, path.toQString(), tick);
        }
    });
}

bool DawAudioTracksModel::importAudioFile(int row, const QString& path, int tick)
{
    if (row < 0 || row >= int(m_tracks.size())) {
        return false;
    }
    QString error;
    DecodedAudioPtr audio = decodeAudioFile(path, error);
    if (!audio) {
        setError(error);
        return false;
    }
    Clip clip;
    clip.id = newId();
    clip.path = path;
    clip.startTick = std::max(0, tick);
    clip.audio = audio;
    clip.peaks = makePeaks(*audio);
    Track& track = m_tracks[size_t(row)];
    track.clips.push_back(std::move(clip));
    syncTrack(track, true);
    notifyRow(row, { ClipsRole });
    writeToProject();
    setError(QString());
    return true;
}

void DawAudioTracksModel::moveClip(int row, const QString& clipId, int tick)
{
    if (row < 0 || row >= int(m_tracks.size())) {
        return;
    }
    Track& track = m_tracks[size_t(row)];
    for (Clip& clip : track.clips) {
        if (clip.id == clipId && clip.startTick != std::max(0, tick)) {
            clip.startTick = std::max(0, tick);
            syncTrack(track, true);
            notifyRow(row, { ClipsRole });
            writeToProject();
            return;
        }
    }
}

void DawAudioTracksModel::removeClip(int row, const QString& clipId)
{
    if (row < 0 || row >= int(m_tracks.size())) {
        return;
    }
    Track& track = m_tracks[size_t(row)];
    const auto it = std::find_if(track.clips.begin(), track.clips.end(), [&clipId](const Clip& clip) { return clip.id == clipId; });
    if (it == track.clips.end()) {
        return;
    }
    track.clips.erase(it);
    syncTrack(track, true);
    notifyRow(row, { ClipsRole });
    writeToProject();
}

void DawAudioTracksModel::toggleMute(int row)
{
    if (row < 0 || row >= int(m_tracks.size())) {
        return;
    }
    Track& track = m_tracks[size_t(row)];
    track.muted = !track.muted;
    if (track.engineId >= 0) {
        ControlParams control;
        control.volume = AutomatableValue<volume_db_t>(volume_db_t(track.volumeDb));
        control.muted = track.muted;
        playback()->setControlParams(track.engineId, control);
    }
    notifyRow(row, { MutedRole });
    writeToProject();
}

void DawAudioTracksModel::setVolume(int row, double volumeDb)
{
    if (row < 0 || row >= int(m_tracks.size())) {
        return;
    }
    Track& track = m_tracks[size_t(row)];
    track.volumeDb = std::clamp(volumeDb, -60.0, 12.0);
    if (track.engineId >= 0) {
        ControlParams control;
        control.volume = AutomatableValue<volume_db_t>(volume_db_t(track.volumeDb));
        control.muted = track.muted;
        playback()->setControlParams(track.engineId, control);
    }
    notifyRow(row, { VolumeRole });
    writeToProject();
}

void DawAudioTracksModel::renameTrack(int row, const QString& name)
{
    if (row < 0 || row >= int(m_tracks.size()) || name.trimmed().isEmpty()) {
        return;
    }
    m_tracks[size_t(row)].name = name.trimmed();
    notifyRow(row, { NameRole });
    writeToProject();
}

// ---- model -------------------------------------------------------------------------------------

QVariantList DawAudioTracksModel::makePeaks(const DecodedAudio& audio)
{
    QVariantList peaks;
    const size_t frames = audio.frames();
    peaks.reserve(int(frames / PEAK_FRAMES + 1));
    for (size_t start = 0; start < frames; start += PEAK_FRAMES) {
        float peak = 0.f;
        const size_t end = std::min(frames, start + PEAK_FRAMES);
        for (size_t frame = start; frame < end; ++frame) {
            for (int channel = 0; channel < audio.channels; ++channel) {
                peak = std::max(peak, std::abs(audio.samples[frame * audio.channels + channel]));
            }
        }
        peaks.append(std::min(1.f, peak));
    }
    return peaks;
}

QVariantList DawAudioTracksModel::clipsForQml(const Track& track) const
{
    QVariantList clips;
    for (const Clip& clip : track.clips) {
        clips.append(QVariantMap {
            { "id", clip.id },
            { "name", QFileInfo(clip.path).completeBaseName() },
            { "path", clip.path },
            { "startTick", clip.startTick },
            { "endTick", endTickFor(clip) },
            { "seconds", clip.audio ? clip.audio->seconds() : 0.0 },
            { "peaks", clip.peaks }
        });
    }
    return clips;
}

int DawAudioTracksModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_tracks.size());
}

QVariant DawAudioTracksModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_tracks.size())) {
        return {};
    }
    const Track& track = m_tracks[size_t(index.row())];
    switch (role) {
    case TrackIdRole: return track.id;
    case NameRole: return track.name;
    case ColorIndexRole: return track.colorIndex;
    case MutedRole: return track.muted;
    case VolumeRole: return track.volumeDb;
    case ClipsRole: return clipsForQml(track);
    case ArmedRole: return track.armed;
    default: return {};
    }
}

QHash<int, QByteArray> DawAudioTracksModel::roleNames() const
{
    return {
        { TrackIdRole, "trackId" }, { NameRole, "name" }, { ColorIndexRole, "colorIndex" },
        { MutedRole, "muted" }, { VolumeRole, "volumeDb" }, { ClipsRole, "clips" }, { ArmedRole, "armed" }
    };
}

int DawAudioTracksModel::count() const
{
    return int(m_tracks.size());
}

// ---- recording ---------------------------------------------------------------------------------

bool DawAudioTracksModel::recording() const
{
    return m_recording;
}

bool DawAudioTracksModel::hasArmedTrack() const
{
    return std::any_of(m_tracks.cbegin(), m_tracks.cend(), [](const Track& track) { return track.armed; });
}

double DawAudioTracksModel::recordedSeconds() const
{
    const int channels = m_capture ? m_capture->channels.load() : 0;
    const int rate = m_capture ? m_capture->sampleRate.load() : 0;
    return channels > 0 && rate > 0 ? double(m_take.size()) / channels / rate : 0.0;
}

void DawAudioTracksModel::toggleArm(int row)
{
    if (row < 0 || row >= int(m_tracks.size()) || m_recording) {
        return;
    }
    const bool arm = !m_tracks[size_t(row)].armed;
    for (size_t index = 0; index < m_tracks.size(); ++index) {
        const bool armed = arm && int(index) == row;
        if (m_tracks[index].armed != armed) {
            m_tracks[index].armed = armed;
            notifyRow(int(index), { ArmedRole });
        }
    }
    emit recordingChanged();
}

void DawAudioTracksModel::toggleRecording(int tick)
{
    if (m_recording) {
        dispatcher()->dispatch("stop"); // the playback status change finishes the take
        finishRecording();
        return;
    }

    const auto armed = std::find_if(m_tracks.cbegin(), m_tracks.cend(), [](const Track& track) { return track.armed; });
    if (armed == m_tracks.cend()) {
        setError(tr("Arm an audio track (R) to record into it."));
        return;
    }
    if (audioDriverController()->inputChannelCount() == 0) {
        setError(tr("No input to record from. In Preferences > Audio & MIDI choose the ASIO driver and an audio interface "
                    "with inputs (ASIO4ALL works with any interface)."));
        return;
    }

    m_recordTrackId = armed->id;
    m_recordStartTick = std::max(0, tick);
    m_take.clear();
    startCapture();
    m_recording = true;
    m_waitingForPlayback = true;
    setError(QString());
    emit recordingChanged();
    emit recordedSecondsChanged();

    dispatcher()->dispatch("play");
}

void DawAudioTracksModel::startCapture()
{
    if (!m_capture) {
        m_capture = std::make_shared<Capture>();
    }
    // Leftovers of an earlier take are dropped
    float discard = 0.f;
    while (m_capture->queue.tryPop(discard)) {}
    m_capture->overflow = false;

    // The driver's audio thread: push the samples, never block, never touch this QObject
    std::weak_ptr<Capture> weak = m_capture;
    audioDriverController()->setInputCallback([weak](const float* samples, muse::audio::samples_t frames,
                                                     muse::audio::audioch_t channels, muse::audio::sample_rate_t sampleRate) {
        const std::shared_ptr<Capture> capture = weak.lock();
        if (!capture || !capture->active.load(std::memory_order_acquire)) {
            return;
        }
        capture->channels.store(int(channels), std::memory_order_relaxed);
        capture->sampleRate.store(int(sampleRate), std::memory_order_relaxed);
        const size_t count = size_t(frames) * channels;
        for (size_t index = 0; index < count; ++index) {
            if (!capture->queue.tryPush(samples[index])) {
                capture->overflow.store(true, std::memory_order_relaxed);
                return;
            }
        }
    });

    // Capture runs while the score plays; playback stopping ends the take
    const context::IPlaybackStatePtr state = globalContext()->playbackState();
    if (state) {
        state->playbackStatusChanged().onReceive(this, [this](muse::audio::PlaybackStatus status) {
            if (!m_recording) {
                return;
            }
            if (status == muse::audio::PlaybackStatus::Running && m_waitingForPlayback) {
                m_waitingForPlayback = false;
                m_capture->active.store(true, std::memory_order_release);
            } else if (status != muse::audio::PlaybackStatus::Running && !m_waitingForPlayback) {
                finishRecording();
            }
        }, muse::async::Asyncable::Mode::SetReplace);
    }
    m_drainTimer.start();
}

void DawAudioTracksModel::drainCapture()
{
    if (!m_capture) {
        return;
    }
    float sample = 0.f;
    const size_t before = m_take.size();
    while (m_capture->queue.tryPop(sample)) {
        m_take.push_back(sample);
    }
    if (m_take.size() != before) {
        emit recordedSecondsChanged();
    }
}

QString DawAudioTracksModel::takePath(const QString& trackName) const
{
    // Next to the project ("<score> Audio"), or in Documents/MillerScore/Recordings for an unsaved score
    QString folder;
    const project::INotationProjectPtr project = globalContext()->currentProject();
    if (project && !project->isNewlyCreated() && !project->path().empty()) {
        const QFileInfo score(project->path().toQString());
        folder = score.absolutePath() + "/" + score.completeBaseName() + " Audio";
    } else {
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/MillerScore/Recordings";
    }
    QDir().mkpath(folder);
    QString safeName = trackName;
    safeName.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
    for (int take = 1;; ++take) {
        const QString path = QString("%1/%2 take %3.wav").arg(folder, safeName).arg(take);
        if (!QFileInfo::exists(path)) {
            return path;
        }
    }
}

void DawAudioTracksModel::finishRecording()
{
    if (!m_recording) {
        return;
    }
    m_capture->active.store(false, std::memory_order_release);
    m_drainTimer.stop();
    drainCapture();
    m_recording = false;
    m_waitingForPlayback = false;
    emit recordingChanged();

    const int row = rowOf(m_recordTrackId);
    const int channels = m_capture->channels.load();
    const int rate = m_capture->sampleRate.load();
    if (row < 0 || m_take.empty() || channels <= 0 || rate <= 0) {
        setError(tr("Nothing was recorded. Check the interface's input and that playback started."));
        return;
    }
    const QString path = takePath(m_tracks[size_t(row)].name);
    QString error;
    if (!writeWavFile(path, m_take, channels, rate, error)) {
        setError(error);
        return;
    }
    const bool overflowed = m_capture->overflow.load();
    m_take.clear();
    emit recordedSecondsChanged();
    if (importAudioFile(row, path, m_recordStartTick) && overflowed) {
        setError(tr("The recording could not keep up and has gaps; try a larger ASIO buffer."));
    }
    LOGI() << "recorded take saved: " << path;
}

QString DawAudioTracksModel::lastError() const
{
    return m_lastError;
}

void DawAudioTracksModel::setError(const QString& error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    emit lastErrorChanged();
}

void DawAudioTracksModel::notifyRow(int row, const QVector<int>& roles)
{
    emit dataChanged(index(row), index(row), roles);
}

int DawAudioTracksModel::rowOf(const QString& trackId) const
{
    for (size_t row = 0; row < m_tracks.size(); ++row) {
        if (m_tracks[row].id == trackId) {
            return int(row);
        }
    }
    return -1;
}
