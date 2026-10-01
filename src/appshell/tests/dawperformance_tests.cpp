/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtest/gtest.h>
#include <limits>
#include "dawperformance_fixture.h"

using mu::appshell::PerformanceModelFixture;

TEST(DawPerformanceTests, MalformedPitchAndTimingBatchesRejectBeforeAnyOverrideWrite)
{
    const QVariantList invalid { QVariant(QStringLiteral("invalid")), QVariant(),
        QVariant(std::numeric_limits<double>::quiet_NaN()), QVariant(std::numeric_limits<double>::infinity()),
        QVariant(std::numeric_limits<int>::min()), QVariant(std::numeric_limits<int>::max()), QVariant(0.5) };
    for (const QVariant& bad : invalid) {
        PerformanceModelFixture model;
        model.values["a"].pitchOffsetCents = 75;
        model.values["b"].pitchOffsetCents = 80;
        model.values["a"].startOffsetTicks = 100;
        model.values["b"].startOffsetTicks = 120;
        const auto before = model.values;
        EXPECT_FALSE(model.setPerformancePitchOffsets({ {"a", 10}, {"b", bad} })) << bad.toString().toStdString();
        EXPECT_FALSE(model.setPerformanceStartOffsets({ {"a", 10}, {"b", bad} })) << bad.toString().toStdString();
        EXPECT_EQ(model.writes, 0);
        EXPECT_EQ(model.values, before);
    }
}

TEST(DawPerformanceTests, VelocityBatchesRequireWholeSupportedIntegerValues)
{
    for (const QVariant& bad : QVariantList { QVariant(0.5), QVariant(0), QVariant(128), QVariant(QStringLiteral("invalid")),
                                            QVariant(std::numeric_limits<double>::infinity()) }) {
        PerformanceModelFixture model;
        model.values["a"].velocity = 64;
        model.values["b"].velocity = 80;
        const auto before = model.values;
        EXPECT_FALSE(model.setPerformanceVelocities({ {"a", 100}, {"b", bad} }));
        EXPECT_EQ(model.writes, 0);
        EXPECT_EQ(model.values, before);
    }
}

TEST(DawPerformanceTests, SupportedBoundariesAndZeroResetKeepTheirMeaning)
{
    PerformanceModelFixture model;
    EXPECT_TRUE(model.setPerformanceVelocities({ {"a", 1}, {"b", 127} }));
    EXPECT_EQ(model.values["a"].velocity, 1);
    EXPECT_EQ(model.values["b"].velocity, 127);
    EXPECT_TRUE(model.setPerformancePitchOffsets({ {"a", -1200}, {"b", 1200} }));
    EXPECT_EQ(model.values["a"].pitchOffsetCents, -1200);
    EXPECT_EQ(model.values["b"].pitchOffsetCents, 1200);
    EXPECT_TRUE(model.setPerformanceStartOffsets({ {"a", -1000000}, {"b", 1000000} }));
    EXPECT_EQ(model.values["a"].startOffsetTicks, -1000000);
    EXPECT_EQ(model.values["b"].startOffsetTicks, 1000000);
    EXPECT_TRUE(model.setPerformancePitchOffsets({ {"a", 0} }));
    EXPECT_FALSE(model.values["a"].pitchOffsetCents.has_value());
    EXPECT_TRUE(model.setPerformanceStartOffsets({ {"a", 0} }));
    EXPECT_FALSE(model.values["a"].startOffsetTicks.has_value());
}

TEST(DawPerformanceTests, SingleTimingEditRejectsIntMinAndKeepsModeProtection)
{
    PerformanceModelFixture model;
    model.values["a"].startOffsetTicks = 120;
    EXPECT_FALSE(model.setPerformanceStartOffset("a", std::numeric_limits<int>::min()));
    EXPECT_EQ(model.writes, 0);
    EXPECT_EQ(model.values["a"].startOffsetTicks, 120);
    EXPECT_TRUE(model.setPerformanceStartOffset("a", -1000000));
    EXPECT_TRUE(model.setPerformanceStartOffset("a", 1000000));
    model.m_editMode = PerformanceModelFixture::EditMode::Notation;
    EXPECT_FALSE(model.setPerformanceStartOffset("a", 10));
    EXPECT_FALSE(model.setPerformanceStartOffsets({ {"a", 10} }));
}
