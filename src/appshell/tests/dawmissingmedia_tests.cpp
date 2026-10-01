/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtest/gtest.h>
#include <cstring>
#include "dawmissingmedia_fixture.h"

using namespace mu::appshell;

static QJsonObject missingMediaProject()
{
    return QJsonObject {
        {"schemaVersion", 2}, {"midiTracks", QJsonArray { QJsonObject {{"id", "keep-midi"}} }},
        {"audioTracks", QJsonArray { QJsonObject {
            {"id", "audio1"}, {"name", "Ambience"}, {"volumeDb", -6}, {"muted", true}, {"colorIndex", 3},
            {"clips", QJsonArray {
                QJsonObject {{"id", "missing1"}, {"path", "media/Unavaílable.wav"}, {"startTick", 960}},
                QJsonObject {{"id", "available1"}, {"path", "media/Available.wav"}, {"startTick", 0}}
            }}
        }}}
    };
}

static DecodedAudioPtr shortAudio()
{
    auto audio = std::make_shared<DecodedAudio>();
    audio->sampleRate = 4;
    audio->channels = 1;
    audio->samples = {0.25f, 0.5f, -0.25f, -0.5f};
    return audio;
}

TEST(DawMissingMediaTests, MissingReferencesSurviveLoadUnrelatedEditsSaveAndReload)
{
    DawAudioTracksModel model;
    const auto original = missingMediaProject();
    model.context.project->data = muse::ByteArray::fromQByteArray(QJsonDocument(original).toJson());
    model.decoded.insert("media/Available.wav", shortAudio());
    model.readFromProject();
    ASSERT_EQ(model.m_tracks.size(), 1u);
    ASSERT_EQ(model.m_tracks[0].clips.size(), 2u);
    EXPECT_FALSE(model.m_tracks[0].clips[0].audio);
    EXPECT_EQ(model.m_tracks[0].clips[0].id, "missing1");
    EXPECT_EQ(model.m_tracks[0].clips[0].path, QString::fromUtf8("media/Unavaílable.wav"));
    EXPECT_EQ(model.m_tracks[0].clips[0].startTick, 960);
    EXPECT_TRUE(model.m_tracks[0].clips[0].peaks.empty());
    EXPECT_TRUE(model.lastError.contains("Unavaílable.wav"));
    EXPECT_FALSE(model.lastError.contains("media/"));
    model.m_tracks[0].name = "Renamed track";
    model.m_tracks[0].volumeDb = -3;
    model.writeToProject();
    const auto stored = QJsonDocument::fromJson(model.context.project->data.toQByteArray()).object();
    EXPECT_EQ(stored.value("midiTracks"), original.value("midiTracks"));
    const auto track = stored.value("audioTracks").toArray()[0].toObject();
    EXPECT_EQ(track.value("clips"), original.value("audioTracks").toArray()[0].toObject().value("clips"));
    EXPECT_EQ(track.value("name").toString(), "Renamed track");
    model.readFromProject();
    ASSERT_EQ(model.m_tracks[0].clips.size(), 2u);
    EXPECT_EQ(model.m_tracks[0].clips[0].startTick, 960);
    model.decoded.insert(QString::fromUtf8("media/Unavaílable.wav"), shortAudio());
    model.readFromProject();
    EXPECT_TRUE(model.m_tracks[0].clips[0].audio);
    EXPECT_TRUE(model.lastError.isEmpty());
}

TEST(DawMissingMediaTests, NullOnlyTrackIsPreservedButNeverAddedToTheEngine)
{
    DawAudioTracksModel model;
    model.context.project->data = muse::ByteArray::fromQByteArray(QJsonDocument(missingMediaProject()).toJson());
    model.readFromProject();
    ASSERT_EQ(model.m_tracks.size(), 1u);
    ASSERT_EQ(model.m_tracks[0].clips.size(), 2u);
    EXPECT_EQ(model.engine.adds, 0);
    EXPECT_EQ(model.m_tracks[0].engineId, -1);
    EXPECT_FALSE(model.m_tracks[0].device);
    model.writeToProject();
    const auto clips = QJsonDocument::fromJson(model.context.project->data.toQByteArray()).object()
                       .value("audioTracks").toArray()[0].toObject().value("clips").toArray();
    EXPECT_EQ(clips.size(), 2);
}

TEST(DawMissingMediaTests, MissingClipsHaveSafeEnglishPlaceholdersAndBoundaryTicks)
{
    DawAudioTracksModel model;
    DawAudioTracksModel::Track track;
    track.clips.push_back({"missing", "media/Ambience.wav", 960, nullptr, {}});
    const auto clips = model.clipsForQml(track);
    ASSERT_EQ(clips.size(), 1);
    const auto clip = clips[0].toMap();
    EXPECT_EQ(clip.value("name").toString(), "Ambience [Missing]");
    EXPECT_EQ(clip.value("endTick").toInt(), 1440);
    EXPECT_EQ(clip.value("seconds").toDouble(), 0);
    track.clips[0].startTick = std::numeric_limits<int>::max();
    EXPECT_EQ(model.endTickFor(track.clips[0]), std::numeric_limits<int>::max());
}

TEST(DawMissingMediaTests, RenderSkipsNullMediaAndKeepsAvailableAudioAtItsOriginalTime)
{
    DawAudioTracksModel model;
    DawAudioTracksModel::Track track;
    track.clips.push_back({"missing", "media/Missing.wav", 960, nullptr, {}});
    track.clips.push_back({"available", "media/Available.wav", 480, shortAudio(), {}});
    const auto bytes = model.renderTrack(track, {2.0, 1.0});
    ASSERT_EQ(bytes.size(), 16u + 8u * 2u * sizeof(float));
    EXPECT_EQ(std::memcmp(bytes.constData(), "MSPC", 4), 0);
    float firstSample = 1;
    float delayedSample = 0;
    std::memcpy(&firstSample, bytes.constData() + 16, sizeof(float));
    std::memcpy(&delayedSample, bytes.constData() + 16 + 4 * 2 * sizeof(float), sizeof(float));
    EXPECT_EQ(firstSample, 0);
    EXPECT_EQ(delayedSample, 0.25f);
    track.clips.pop_back();
    EXPECT_EQ(model.renderTrack(track, {2.0}).size(), 16u);
}
