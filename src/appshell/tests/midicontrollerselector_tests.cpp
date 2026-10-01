/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtest/gtest.h>
#include <QFile>
#include <QJSEngine>
#include <QRegularExpression>
#include <QSet>
#include <QVariant>

// The standalone CMake fixture compiles the unchanged catalog projection
// method from DawProjectModel, without constructing its service dependencies.
QVariantList midiControllerSelectorTestCatalog();

class MidiControllerSelectorTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        QFile file(QStringLiteral(MILLERSCORE_TEST_SOURCE_ROOT "/src/appshell/qml/MuseScore/AppShell/Daw/PianoRoll.qml"));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const QString source = QString::fromUtf8(file.readAll());
        engine.globalObject().setProperty("testCatalog", engine.toScriptValue(midiControllerSelectorTestCatalog()));
        evaluate(R"JS(
            function qsTrc(context, text) { return text; }
            String.prototype.arg = function(value) { return this.replace(/%\d+/, String(value)); };
            var readCalls = [], writeCalls = 0, channelAutomationPoints = [];
            var controlLaneParameter = 'cc:0', controlLaneModel = [], midiControllerSpecs = [], startTrack = 0;
            var midiControllerBackend = 'SoundFont';
            var tracksModel = { selectedPartId: 'part1', selectedInstrumentId: 'piano' };
            var projectModel = {
                midiControllerCatalog: function() { return testCatalog; },
                channelAutomationPoints: function(part, instrument, control) {
                    readCalls.push([part, instrument, control]);
                    return [{tick:480, value:Number(control.substring(3))/127}];
                },
                editChannelAutomation: function() { writeCalls++; },
                resetChannelAutomation: function() { writeCalls++; }
            };
        )JS");
        for (const auto name : { "controlSpec", "currentControlSpec", "midiCategoryTitle", "loadMidiControllerCatalog",
                                 "buildControlLaneMenu", "refreshChannelAutomation", "displayControlValue", "midiControllerPlaybackMessage" }) {
            const QRegularExpression re(QString("    (function %1\\(.*?\\n    \\})").arg(name), QRegularExpression::DotMatchesEverythingOption);
            const auto match = re.match(source);
            ASSERT_TRUE(match.hasMatch()) << name;
            evaluate(match.captured(1));
        }
        evaluate("loadMidiControllerCatalog(); controlLaneModel = midiControllerSpecs;");
    }

    QJSValue evaluate(const QString& script)
    {
        const auto value = engine.evaluate(script);
        EXPECT_FALSE(value.isError()) << value.toString().toStdString();
        return value;
    }
    QJSEngine engine;
};

TEST_F(MidiControllerSelectorTests, MenuContainsAll128UniqueCanonicalIdsIncludingBothBankBytes)
{
    const auto menu = evaluate("buildControlLaneMenu()").toVariant().toList();
    QSet<QString> ids;
    bool sawBankGroup = false;
    for (const auto& group : menu) {
        const auto map = group.toMap();
        if (map.value("title") == "Bank and program") sawBankGroup = true;
        for (const auto& item : map.value("subitems").toList()) {
            const auto lane = item.toMap();
            const QString id = lane.value("id").toString();
            EXPECT_FALSE(ids.contains(id)) << id.toStdString();
            ids.insert(id);
            EXPECT_TRUE(lane.value("enabled").toBool()) << "Protected lanes must remain viewable";
        }
    }
    EXPECT_TRUE(sawBankGroup);
    ASSERT_EQ(ids.size(), 128);
    for (int cc = 0; cc < 128; ++cc) EXPECT_TRUE(ids.contains(QStringLiteral("lane:cc:%1").arg(cc))) << cc;
}

TEST_F(MidiControllerSelectorTests, SelectingEachLaneRetainsExactIdentityAndReadsItsSavedValuesWithoutWriting)
{
    for (int cc = 0; cc < 128; ++cc) {
        evaluate(QStringLiteral("controlLaneParameter='cc:%1'; refreshChannelAutomation();").arg(cc));
        EXPECT_EQ(evaluate("currentControlSpec().value").toString(), QStringLiteral("cc:%1").arg(cc));
        EXPECT_DOUBLE_EQ(evaluate("channelAutomationPoints[0].value").toNumber(), cc / 127.0);
        EXPECT_EQ(evaluate("readCalls[readCalls.length-1][2]").toString(), QStringLiteral("cc:%1").arg(cc));
        EXPECT_EQ(evaluate("writeCalls").toInt(), 0);
        const bool restricted = cc == 0 || cc == 6 || cc == 32 || cc == 38 || cc == 84 || cc == 88
                                || (cc >= 96 && cc <= 101) || cc >= 120;
        EXPECT_EQ(evaluate("currentControlSpec().enabled").toBool(), !restricted) << cc;
        EXPECT_EQ(evaluate("currentControlSpec().playable").toBool(), !restricted) << cc;
    }
    EXPECT_EQ(evaluate("readCalls.length").toInt(), 128);
    evaluate("tracksModel.selectedPartId='part2'; tracksModel.selectedInstrumentId='violin'; refreshChannelAutomation();");
    EXPECT_EQ(evaluate("readCalls[128][0]").toString(), "part2");
    EXPECT_EQ(evaluate("readCalls[128][1]").toString(), "violin");
    evaluate("startTrack=-1; refreshChannelAutomation();");
    EXPECT_EQ(evaluate("channelAutomationPoints.length").toInt(), 0);
    EXPECT_EQ(evaluate("readCalls.length").toInt(), 129);
}

TEST_F(MidiControllerSelectorTests, UnknownLaneDoesNotFallBackToANeighborAndMidiDisplayClamps)
{
    evaluate("controlLaneParameter='cc:007';");
    EXPECT_EQ(evaluate("currentControlSpec().value").toString(), "cc:007");
    EXPECT_FALSE(evaluate("currentControlSpec().enabled").toBool());
    evaluate("controlLaneParameter='cc:74';");
    EXPECT_EQ(evaluate("displayControlValue(-0.1)").toString(), "0");
    EXPECT_EQ(evaluate("displayControlValue(1.1)").toString(), "127");
    evaluate("controlLaneParameter='cc:64';");
    EXPECT_EQ(evaluate("displayControlValue(63/127)").toString(), "Off");
    EXPECT_EQ(evaluate("displayControlValue(64/127)").toString(), "On");
}

TEST_F(MidiControllerSelectorTests, RuntimeBackendMessageDistinguishesSoundFontUnsupportedAndProtected)
{
    evaluate("controlLaneParameter='cc:7';midiControllerBackend='SoundFont';");
    EXPECT_TRUE(evaluate("midiControllerPlaybackMessage()").toString().contains("sent to this SoundFont"));
    for (const auto backend : { "Muse Sounds", "VST3" }) {
        engine.globalObject().setProperty("midiControllerBackend", QString(backend));
        const auto message = evaluate("midiControllerPlaybackMessage()").toString();
        EXPECT_TRUE(message.contains(backend));
        EXPECT_TRUE(message.contains("does not play"));
    }
    evaluate("midiControllerBackend='';");
    EXPECT_TRUE(evaluate("midiControllerPlaybackMessage()").toString().contains("not yet confirmed"));
    evaluate("controlLaneParameter='cc:127';midiControllerBackend='SoundFont';");
    EXPECT_TRUE(evaluate("midiControllerPlaybackMessage()").toString().contains("never sent"));
    EXPECT_EQ(evaluate("writeCalls").toInt(), 0);
}
