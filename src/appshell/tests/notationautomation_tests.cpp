/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtest/gtest.h>
#include "notationautomation_fixture.h"
#include "automation/utils/automationtestutils.h"

using namespace mu::engraving;
using namespace mu::notation;

TEST(NotationAutomationSourceTests, EveryCcGestureIsOneTransactionAndUndoRedoRestoresOnlyItsLane)
{
    MasterScore score;
    const InstrumentTrackId track { muse::ID(300), u"piano" };
    AutomationCurveMap original;
    for (int cc = 0; cc < 128; ++cc) {
        original[AutomationCurveKey::midiLane(track, "cc:" + std::to_string(cc))] = {
            {0, customPoint(0, 0)}, {480, customPoint(0.25, 0.25)}, {960, customPoint(1, 1)}
        };
    }
    score.controller.data->setCurves(original);
    auto undo = std::make_shared<FakeNotationUndoStack>();
    NotationAutomation notation(undo);
    notation.setMasterScore(&score);
    for (int cc = 0; cc < 128; ++cc) {
        const auto key = AutomationCurveKey::midiLane(track, "cc:" + std::to_string(cc));
        const auto untouched = AutomationCurveKey::midiLane(track, "cc:" + std::to_string((cc + 1) % 128));
        const auto neighbor = notation.automationData()->curve(untouched);
        AutomationPointEdits edits {
            {1440, AutomationPointEdit::MovePoint { customPoint(1, 1), 960 }},
            {960, AutomationPointEdit::MovePoint { customPoint(0.25, 0.25), 480 }},
            {0, AutomationPointEdit::ErasePoint {}},
            {240, AutomationPointEdit::SetPoint { customPoint(0.5, 0.5) }}
        };
        const size_t commandCount = score.commands.size();
        const auto before = notation.automationData()->curve(key);
        notation.editPoints(key, edits);
        EXPECT_EQ(score.commands.size(), commandCount + 1);
        EXPECT_EQ(undo->transactions, cc + 1);
        EXPECT_EQ(undo->lastLabel.str, muse::String(u"Edit automation points"));
        const auto after = notation.automationData()->curve(key);
        ASSERT_EQ(after.size(), 3u);
        EXPECT_EQ(after.begin()->first, 240);
        EXPECT_EQ(after.rbegin()->first, 1440);
        checkCurvesMatch(notation.automationData()->curve(untouched), neighbor);
        score.undoLast();
        checkCurvesMatch(notation.automationData()->curve(key), before);
        checkCurvesMatch(notation.automationData()->curve(untouched), neighbor);
        score.redoLast();
        checkCurvesMatch(notation.automationData()->curve(key), after);
        checkCurvesMatch(notation.automationData()->curve(untouched), neighbor);
    }
}

TEST(NotationAutomationSourceTests, DuplicateTickWritesAndCollidingMovesUndoExactPreviousData)
{
    MasterScore score;
    auto undo = std::make_shared<FakeNotationUndoStack>();
    NotationAutomation notation(undo);
    notation.setMasterScore(&score);
    const auto key = AutomationCurveKey::midiLane({muse::ID(301), u"piano"}, "cc:74");
    const AutomationCurve before {{0, customPoint(0, 0)}, {480, customPoint(1, 1)}};
    score.controller.data->setCurves({{key, before}});
    AutomationPointEdits edits {
        {480, AutomationPointEdit::MovePoint {customPoint(0, 0), 0}},
        {480, AutomationPointEdit::SetPoint {customPoint(0.5, 0.5)}},
        {480, AutomationPointEdit::SetPoint {customPoint(0.75, 0.75)}}
    };
    notation.editPoints(key, edits);
    const auto after = notation.automationData()->curve(key);
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after.at(480).value.outValue, 0.75);
    score.undoLast();
    checkCurvesMatch(notation.automationData()->curve(key), before);
    score.redoLast();
    checkCurvesMatch(notation.automationData()->curve(key), after);
}
