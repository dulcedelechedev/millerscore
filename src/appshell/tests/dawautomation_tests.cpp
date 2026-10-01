/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtest/gtest.h>

#include <limits>
#include "appshell/qml/MuseScore/AppShell/dawautomationutils.h"

using namespace mu::engraving;
using namespace mu::appshell::dawautomation;

TEST(DawAutomationTests, EveryControllerHasOneIdentityAndAnIndependentInstrumentScope)
{
    const InstrumentTrackId first { muse::ID(100), u"piano" };
    const InstrumentTrackId second { muse::ID(101), u"piano" };
    for (int cc = 0; cc < 128; ++cc) {
        const QString id = QStringLiteral("cc:%1").arg(cc);
        const auto key = channelAutomationKey(id, first);
        ASSERT_TRUE(key.has_value()) << cc;
        EXPECT_EQ(key->laneId, id.toStdString());
        EXPECT_EQ(key->trackId(), first);
        EXPECT_NE(key, channelAutomationKey(id, second));
        EXPECT_NE(key, channelAutomationKey(QStringLiteral("cc:%1").arg((cc + 1) % 128), first));
        EXPECT_NE(key, channelAutomationKey(QStringLiteral("channelVolume"), first));
    }
    for (const auto id : { "cc:00", "cc:007", "cc:128", "cc:-1", "cc:1x", "cc: 1" }) {
        EXPECT_FALSE(channelAutomationKey(QString::fromUtf8(id), first).has_value()) << id;
    }
    EXPECT_FALSE(channelAutomationKey(QStringLiteral("cc:64"), {}).has_value());
}

TEST(DawAutomationTests, EveryProtectedControllerAndNonCcSequenceRejectsCurveEditing)
{
    const InstrumentTrackId track { muse::ID(100), u"piano" };
    for (int cc = 0; cc < 128; ++cc) {
        const auto key = channelAutomationKey(QStringLiteral("cc:%1").arg(cc), track);
        ASSERT_TRUE(key.has_value());
        const bool protectedCc = cc == 0 || cc == 6 || cc == 32 || cc == 38 || cc == 84 || cc == 88
                                 || (cc >= 96 && cc <= 101) || cc >= 120;
        EXPECT_EQ(isChannelAutomationEditable(*key), !protectedCc) << cc;
    }
    for (const auto id : { "bankSelect", "programChange", "rpn:0:0", "nrpn:12:34" }) {
        const auto key = channelAutomationKey(QString::fromUtf8(id), track);
        ASSERT_TRUE(key.has_value());
        EXPECT_FALSE(isChannelAutomationEditable(*key));
    }
}

TEST(DawAutomationTests, PreservesBoundaryValuesAndMoveEraseOrdering)
{
    const QVariantList input {
        QVariantMap { {"op", "set"}, {"tick", 0}, {"value", 0.0} },
        QVariantMap { {"op", "move"}, {"from", 480}, {"tick", std::numeric_limits<int>::max()}, {"value", 1.0} },
        QVariantMap { {"op", "erase"}, {"tick", 0} }
    };
    const auto edits = channelAutomationEdits(input);
    ASSERT_TRUE(edits.has_value());
    ASSERT_EQ(edits->size(), 3u);
    EXPECT_EQ(std::get<AutomationPointEdit::SetPoint>(edits->at(0).change).point.value.outValue, 0.0);
    EXPECT_EQ(edits->at(1).tick, std::numeric_limits<int>::max());
    const auto& move = std::get<AutomationPointEdit::MovePoint>(edits->at(1).change);
    EXPECT_EQ(move.from, 480);
    EXPECT_EQ(move.point.value.outValue, 1.0);
    EXPECT_TRUE(std::holds_alternative<AutomationPointEdit::ErasePoint>(edits->at(2).change));
}

TEST(DawAutomationTests, RejectsMalformedWholeGestureBeforeAnyPointCanBeWritten)
{
    const QVariantMap good { {"op", "set"}, {"tick", 480}, {"value", 0.5} };
    const QVariantList badEdits {
        QVariantMap { {"op", "set"}, {"value", 0.5} },
        QVariantMap { {"op", "set"}, {"tick", 0} },
        QVariantMap { {"op", "set"}, {"tick", "invalid"}, {"value", 0.5} },
        QVariantMap { {"op", "set"}, {"tick", -1}, {"value", 0.5} },
        QVariantMap { {"op", "set"}, {"tick", 0.5}, {"value", 0.5} },
        QVariantMap { {"op", "set"}, {"tick", 2147483648.0}, {"value", 0.5} },
        QVariantMap { {"op", "set"}, {"tick", 0}, {"value", "invalid"} },
        QVariantMap { {"op", "set"}, {"tick", 0}, {"value", -0.01} },
        QVariantMap { {"op", "set"}, {"tick", 0}, {"value", 1.01} },
        QVariantMap { {"op", "set"}, {"tick", 0}, {"value", std::numeric_limits<double>::quiet_NaN()} },
        QVariantMap { {"op", "set"}, {"tick", 0}, {"value", 0.5}, {"shape", "unknown"} },
        QVariantMap { {"op", "set"}, {"tick", 0}, {"value", 0.5}, {"shape", "curve"}, {"bend", std::numeric_limits<double>::infinity()} },
        QVariantMap { {"op", "set"}, {"tick", 0}, {"value", 0.5}, {"arrival", "invalid"} },
        QVariantMap { {"op", "set"}, {"tick", 0}, {"value", 0.5}, {"arrival", 1.01} },
        QVariantMap { {"op", "set"}, {"tick", 0}, {"value", 0.5}, {"shape", "curve"}, {"bendTime", -0.01} },
        QVariantMap { {"op", "set"}, {"tick", 0}, {"value", 0.5}, {"shape", "curve"}, {"bendTime", std::numeric_limits<double>::quiet_NaN()} },
        QVariantMap { {"op", "move"}, {"tick", 960}, {"value", 0.5} },
        QVariantMap { {"op", "unknown"}, {"tick", 0}, {"value", 0.5} },
        QVariant(42)
    };
    for (const auto& bad : badEdits) {
        EXPECT_FALSE(channelAutomationEdits({ good, bad }).has_value()) << bad.toMap().size();
    }
    EXPECT_FALSE(channelAutomationEdits({}).has_value());
}

TEST(DawAutomationTests, CurveBendClampsWithoutWrappingAndStepsStayDiscrete)
{
    for (double input : { -1.0, 2.0 }) {
        const auto edits = channelAutomationEdits({ QVariantMap {
            {"op", "set"}, {"tick", 0}, {"value", 0.5}, {"shape", "curve"}, {"bend", input}
        } });
        ASSERT_TRUE(edits.has_value());
        const auto& point = std::get<AutomationPointEdit::SetPoint>(edits->at(0).change).point;
        const auto arrival = std::get<AutomationPoint::ExplicitArrival>(point.value.inValue);
        EXPECT_EQ(arrival.ease.value, input < 0 ? 0.0 : 1.0);
    }
    const auto edits = channelAutomationEdits({ QVariantMap {
        {"op", "set"}, {"tick", 0}, {"value", 0.5}, {"shape", "step"}
    } });
    ASSERT_TRUE(edits.has_value());
    EXPECT_TRUE(std::holds_alternative<AutomationPoint::ArrivalFromPrevious>(
        std::get<AutomationPointEdit::SetPoint>(edits->at(0).change).point.value.inValue));
}
