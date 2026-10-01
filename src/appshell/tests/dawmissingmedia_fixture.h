/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

// Reduced deterministic service graph for compiling selected, unchanged
// DawAudioTracksModel methods. This fixture does not test device decoding,
// QObject signals, asynchronous engine scheduling, or actual audio output.
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "global/types/bytearray.h"
#include "global/log.h"
#include "appshell/qml/MuseScore/AppShell/audiofiledecoder.h"

namespace muse::io {
struct IODevice { enum Mode { ReadOnly }; };
struct Buffer {
    explicit Buffer(ByteArray value) : bytes(std::move(value)) {}
    void open(IODevice::Mode) {}
    ByteArray bytes;
};
}
namespace muse::audio {
using TrackId = int;
using volume_db_t = double;
template<typename T> struct AutomatableValue { explicit AutomatableValue(T v) : value(v) {} T value {}; };
struct TrackParams {
    struct Control { AutomatableValue<volume_db_t> volume {0}; bool muted = false; } control;
};
struct FakePromise {
    TrackId id;
    TrackParams params;
    template<typename Receiver, typename Callback> FakePromise& onResolve(Receiver*, Callback callback)
    { callback(id, params); return *this; }
    template<typename Receiver, typename Callback> void onReject(Receiver*, Callback) {}
};
struct FakePlayback {
    int adds = 0;
    std::vector<int> removals;
    FakePromise addTrack(const std::string&, io::Buffer*, TrackParams params) { return { ++adds, params }; }
    void removeTrack(TrackId id) { removals.push_back(id); }
};
}
namespace mu::project {
struct FakeProject {
    muse::ByteArray data;
    int writes = 0;
    muse::ByteArray dawData() const { return data; }
    void setDawData(muse::ByteArray value) { data = std::move(value); ++writes; }
};
using INotationProjectPtr = std::shared_ptr<FakeProject>;
}
namespace mu::notation {
struct FakeNotationPlayback {
    double playedTickToSec(int tick) const { return tick / 480.0; }
    int secToTick(double seconds) const { return int(std::round(seconds * 480)); }
};
struct FakeMasterNotation {
    FakeNotationPlayback timeline;
    const FakeNotationPlayback* playback() const { return &timeline; }
};
using IMasterNotationPtr = std::shared_ptr<FakeMasterNotation>;
}
namespace mu::appshell {
struct FakeContext {
    project::INotationProjectPtr project = std::make_shared<project::FakeProject>();
    notation::IMasterNotationPtr master = std::make_shared<notation::FakeMasterNotation>();
    project::INotationProjectPtr currentProject() const { return project; }
    notation::IMasterNotationPtr currentMasterNotation() const { return master; }
};

class DawAudioTracksModel
{
public:
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
        double volumeDb = 0;
        std::vector<Clip> clips;
        muse::audio::TrackId engineId = -1;
        std::shared_ptr<muse::io::Buffer> device;
        int generation = 0;
        std::vector<double> placedSeconds;
    };
    void readFromProject();
    void writeToProject();
    muse::ByteArray renderTrack(const Track&, const std::vector<double>&) const;
    void syncTrack(Track&, bool);
    std::vector<double> clipStartSeconds(const Track&) const;
    int endTickFor(const Clip&) const;
    QVariantList clipsForQml(const Track&) const;
    static QVariantList makePeaks(const DecodedAudio&);

    FakeContext* globalContext() const { return const_cast<FakeContext*>(&context); }
    muse::audio::FakePlayback* playback() const { return engineEnabled ? const_cast<muse::audio::FakePlayback*>(&engine) : nullptr; }
    static QString tr(const char* value) { return QString::fromUtf8(value); }
    void beginResetModel() {}
    void endResetModel() {}
    void countChanged() { ++resetNotifications; }
    void setError(const QString& error) { lastError = error; }
    int rowOf(const QString& id) const
    {
        for (size_t i = 0; i < m_tracks.size(); ++i) if (m_tracks[i].id == id) return int(i);
        return -1;
    }
    DecodedAudioPtr decodeAudioFile(const QString& path, QString& error) const
    {
        auto audio = decoded.value(path);
        if (!audio) error = "Missing test media";
        return audio;
    }
    FakeContext context;
    muse::audio::FakePlayback engine;
    bool engineEnabled = true;
    QHash<QString, DecodedAudioPtr> decoded;
    std::vector<Track> m_tracks;
    bool m_loading = false;
    int resetNotifications = 0;
    QString lastError;
};
}
