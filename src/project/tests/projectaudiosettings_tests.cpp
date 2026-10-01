/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <gtest/gtest.h>

#include <QFile>
#include <QTemporaryDir>

#include "project/internal/projectaudiosettings.h"
#include "playback/tests/mocks/playbackconfigurationmock.h"

#include "engraving/infrastructure/mscreader.h"
#include "engraving/infrastructure/mscwriter.h"

using namespace muse;
using namespace muse::audio;
using namespace mu::engraving;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

namespace mu::project {
class ProjectAudioSettingsTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_config = std::make_shared<NiceMock<playback::PlaybackConfigurationMock> >();
        ON_CALL(*m_config, compatMuseSoundsProfileName()).WillByDefault(ReturnRef(m_compatProfile));
        ON_CALL(*m_config, museSoundsProfileName()).WillByDefault(ReturnRef(m_museProfile));
        ON_CALL(*m_config, defaultProfileForNewProjects()).WillByDefault(Return(m_basicProfile));
    }

    std::unique_ptr<ProjectAudioSettings> makeSettings() const
    {
        std::unique_ptr<ProjectAudioSettings> settings(new ProjectAudioSettings(muse::modularity::globalCtx()));
        settings->playbackConfig.set(m_config);
        return settings;
    }

    Ret readJson(ProjectAudioSettings& settings, const QByteArray& json)
    {
        QFile file(m_inDir.filePath("audiosettings.json"));
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(json);
        file.close();

        MscReader::Params params;
        params.filePath = m_inDir.path();
        params.mode = MscIoMode::Dir;
        MscReader reader(params);
        EXPECT_TRUE(reader.open());
        return settings.read(reader);
    }

    QByteArray writeJson(ProjectAudioSettings& settings)
    {
        MscWriter::Params params;
        params.filePath = m_outDir.path();
        params.mode = MscIoMode::Dir;
        MscWriter writer(params);
        EXPECT_TRUE(writer.open());
        EXPECT_TRUE(settings.write(writer, nullptr));
        writer.close();

        QFile file(m_outDir.filePath("audiosettings.json"));
        EXPECT_TRUE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    }

    static QByteArray effectsProject()
    {
        return QByteArray(R"({
            "activeSoundProfile": "MuseScore Basic",
            "master": { "balance": 0, "volumeDb": -6,
                        "fxChain": { "0": { "active": true, "chainOrder": 0,
                                            "resourceMeta": { "id": "Muse Reverb", "vendor": "Muse", "type": "muse_plugin", "attributes": {} },
                                            "unitConfiguration": {} } } },
            "aux": [ { "out": { "balance": 0, "volumeDb": 0,
                                "fxChain": { "0": { "active": true, "chainOrder": 0,
                                                    "resourceMeta": { "id": "Muse Reverb", "vendor": "Muse", "type": "muse_plugin", "attributes": {} },
                                                    "unitConfiguration": {} } } },
                       "soloMuteState": { "mute": false, "solo": false } } ],
            "tracks": [ {
                "partId": "1", "instrumentId": "piano",
                "in": { "resourceMeta": { "id": "MS Basic", "vendor": "Fluid", "type": "fluid_soundfont", "attributes": {} } },
                "out": { "balance": 0, "volumeDb": 0,
                         "auxSends": [ { "active": true, "signalAmount": 0.25 } ],
                         "fxChain": {
                             "0": { "active": true, "chainOrder": 0,
                                    "resourceMeta": { "id": "Muse Reverb", "vendor": "Muse", "type": "muse_plugin", "attributes": {} },
                                    "unitConfiguration": {} },
                             "1": { "active": false, "chainOrder": 1,
                                    "resourceMeta": { "id": "Absent Effect", "vendor": "Somebody", "type": "vst_plugin", "attributes": {} },
                                    "unitConfiguration": { "componentState": "b3BhcXVlLXN0YXRl" } } } }
            } ]
        })");
    }

    std::shared_ptr<NiceMock<playback::PlaybackConfigurationMock> > m_config;
    playback::SoundProfileName m_compatProfile = u"Muse Sounds (compat)";
    playback::SoundProfileName m_museProfile = u"Muse Sounds";
    playback::SoundProfileName m_basicProfile = u"MuseScore Basic";

    QTemporaryDir m_inDir;
    QTemporaryDir m_outDir;
};

// Effects, their order, bypass and opaque plug-in state, and sends survive a
// save and reopen; an effect whose plug-in is missing is kept as well.
TEST_F(ProjectAudioSettingsTests, EffectChainRoundTrip)
{
    std::unique_ptr<ProjectAudioSettings> first = makeSettings();
    ASSERT_TRUE(readJson(*first, effectsProject()));

    const InstrumentTrackId track { ID(1), u"piano" };
    const AudioOutputParams& out = first->trackOutputParams(track);

    ASSERT_EQ(out.fxChain.size(), 2u);
    EXPECT_EQ(out.fxChain.at(0).resourceMeta.id, "Muse Reverb");
    EXPECT_TRUE(out.fxChain.at(0).active);
    EXPECT_EQ(out.fxChain.at(1).resourceMeta.id, "Absent Effect");
    EXPECT_FALSE(out.fxChain.at(1).active);
    EXPECT_EQ(out.fxChain.at(1).configuration.at("componentState"), "opaque-state");
    ASSERT_EQ(out.auxSends.size(), 1u);
    EXPECT_FLOAT_EQ(out.auxSends.at(0).signalAmount, 0.25f);

    QByteArray saved = writeJson(*first);

    std::unique_ptr<ProjectAudioSettings> second = makeSettings();
    ASSERT_TRUE(readJson(*second, saved));

    EXPECT_EQ(second->trackOutputParams(track).fxChain, out.fxChain);
    EXPECT_EQ(second->trackOutputParams(track).auxSends, out.auxSends);
    EXPECT_EQ(second->masterAudioOutputParams().fxChain, first->masterAudioOutputParams().fxChain);
    EXPECT_EQ(second->auxOutputParams(0).fxChain, first->auxOutputParams(0).fxChain);
}

// A project saved before effects existed loads with empty chains and sends.
TEST_F(ProjectAudioSettingsTests, ProjectWithoutEffectFields)
{
    std::unique_ptr<ProjectAudioSettings> settings = makeSettings();
    ASSERT_TRUE(readJson(*settings, QByteArray(R"({
        "activeSoundProfile": "MuseScore Basic",
        "master": { "balance": 0, "volumeDb": 0 },
        "tracks": [ {
            "partId": "1", "instrumentId": "piano",
            "in": { "resourceMeta": { "id": "MS Basic", "vendor": "Fluid", "type": "fluid_soundfont", "attributes": {} } },
            "out": { "balance": 0, "volumeDb": 0 }
        } ]
    })")));

    const AudioOutputParams& out = settings->trackOutputParams({ ID(1), u"piano" });
    EXPECT_TRUE(out.fxChain.empty());
    EXPECT_TRUE(out.auxSends.empty());
    EXPECT_TRUE(settings->masterAudioOutputParams().fxChain.empty());
    EXPECT_FALSE(settings->containsAuxOutputParams(0));
}
}
