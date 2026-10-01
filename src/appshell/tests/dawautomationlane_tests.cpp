/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtest/gtest.h>
#include <QFile>
#include <QJSEngine>
#include <QRegularExpression>
#include <QVariant>

#include "appshell/qml/MuseScore/AppShell/dawautomationutils.h"
#include "mpe/automationpoint.h"

class DawAutomationLaneTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        QFile file(QStringLiteral(MILLERSCORE_TEST_SOURCE_ROOT "/src/appshell/qml/MuseScore/AppShell/Daw/DawAutomationLane.qml"));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        const QString source = QString::fromUtf8(file.readAll());
        evaluate(R"JS(
            var points=[], selectedTicks=[], selectedTickLookup=Object.create(null), written=[];
            var dragMode=0, dragValueDelta=0, dragTickDelta=0, bendTick=-1, bendPreview=-1;
            var defaultValue=0.5, width=1000, height=180, viewX=0, pixelsPerTick=1;
            var topPad=8, availableHeight=146;
            function editRequested(edits) { written.push(edits); }
        )JS");
        for (const auto name : { "xForTick", "yForValue", "isSelected", "evaluateSegment", "shownPoints", "valueAtTick",
                                 "pointEdit", "commitValueDrag", "commitTimeDrag" }) {
            const QRegularExpression re(QString("    (function %1\\([^\\n]+\\}\\r?\\n|function %1\\(.*?\\n    \\})").arg(name),
                                        QRegularExpression::DotMatchesEverythingOption);
            const auto match = re.match(source);
            ASSERT_TRUE(match.hasMatch()) << name;
            evaluate(match.captured(1));
        }
        const QRegularExpression paintRe("        onPaint: \\{(.*?)\\n        \\}", QRegularExpression::DotMatchesEverythingOption);
        const auto paintMatch = paintRe.match(source);
        ASSERT_TRUE(paintMatch.hasMatch());
        evaluate("function paint() {" + paintMatch.captured(1) + "\n}");
        evaluate(R"JS(
            var strokePaths=[], path=[];
            var ctx={ reset:function(){strokePaths=[];}, beginPath:function(){path=[];},
                moveTo:function(x,y){path.push({op:'move',x:x,y:y});},
                lineTo:function(x,y){path.push({op:'line',x:x,y:y});},
                closePath:function(){}, fill:function(){}, stroke:function(){strokePaths.push(path.slice());} };
            function getContext(type) { return ctx; }
            var Qt={rgba:function(){return 'fill';}};
            var lane={shownPoints:shownPoints,xForTick:xForTick,yForValue:yForValue,evaluateSegment:evaluateSegment,
                bipolar:false,lineColor:{r:1,g:0,b:0}};
        )JS");
    }

    QJSValue evaluate(const QString& script)
    {
        const QJSValue result = engine.evaluate(script);
        EXPECT_FALSE(result.isError()) << result.toString().toStdString();
        return result;
    }

    void setTwoPoints(const muse::mpe::AutomationPoint& end)
    {
        const auto ease = muse::mpe::ease(end);
        const bool step = !ease.has_value();
        const QVariantList projected {
            QVariantMap { {"tick", 0}, {"value", 0.2}, {"arrival", 0.2}, {"shape", "linear"}, {"bend", 0.5}, {"bendTime", 0.5} },
            QVariantMap { {"tick", 480}, {"value", end.outValue.raw()},
                {"arrival", muse::mpe::resolveInValue(end, muse::real_t(0.2)).raw()},
                {"shape", step ? "step" : (ease->isNone() ? "linear" : "curve")},
                {"bend", ease ? ease->value.raw() : 0.5}, {"bendTime", ease ? ease->t.raw() : 0.5} }
        };
        engine.globalObject().setProperty("points", engine.toScriptValue(projected));
    }

    QJSEngine engine;
};

TEST_F(DawAutomationLaneTests, ValuesMatchNativeIncomingSegmentsAndExactPointJumps)
{
    muse::mpe::AutomationPoint start;
    start.outValue = 0.2;
    for (const double bendTime : { 0.0, 0.25, 0.5, 0.8, 1.0 }) {
        for (const double bend : { 0.0, 0.3, 0.8, 1.0 }) {
            muse::mpe::AutomationPoint end;
            end.outValue = 0.9;
            end.inValue = muse::mpe::AutomationPoint::ExplicitArrival { 0.4, { bendTime, bend } };
            setTwoPoints(end);
            const muse::mpe::AutomationCurve<int> curve { {0, start}, {480, end} };
            for (const int tick : { -1, 0, 1, 120, 240, 360, 479, 480, 600 }) {
                EXPECT_NEAR(evaluate(QStringLiteral("valueAtTick(%1)").arg(tick)).toNumber(),
                            muse::mpe::evaluateCurveAt(curve, tick).raw(), 1e-9) << tick << " " << bendTime << " " << bend;
            }
        }
    }
    muse::mpe::AutomationPoint end;
    end.outValue = 0.9;
    end.inValue = muse::mpe::AutomationPoint::ArrivalFromPrevious {};
    setTwoPoints(end);
    const muse::mpe::AutomationCurve<int> stepped { {0, start}, {480, end} };
    for (const int tick : { 0, 240, 479, 480, 600 }) {
        EXPECT_NEAR(evaluate(QStringLiteral("valueAtTick(%1)").arg(tick)).toNumber(),
                    muse::mpe::evaluateCurveAt(stepped, tick).raw(), 1e-9) << tick;
    }
}

TEST_F(DawAutomationLaneTests, ValueAndTimeGesturesPreserveArrivalAndSavedEaseTime)
{
    muse::mpe::AutomationPoint end;
    end.outValue = 0.9;
    end.inValue = muse::mpe::AutomationPoint::ExplicitArrival { 0.4, { 0.25, 0.8 } };
    setTwoPoints(end);
    evaluate("selectedTicks=[480];selectedTickLookup[480]=true;dragMode=2;dragValueDelta=0.05;");
    EXPECT_NEAR(evaluate("shownPoints()[1].arrival").toNumber(), 0.45, 1e-9);
    EXPECT_NEAR(evaluate("shownPoints()[1].value").toNumber(), 0.95, 1e-9);
    evaluate("commitValueDrag();");
    auto edits = mu::appshell::dawautomation::channelAutomationEdits(evaluate("written[0]").toVariant().toList());
    ASSERT_TRUE(edits.has_value());
    ASSERT_EQ(edits->size(), 1u);
    const auto& valuePoint = std::get<mu::engraving::AutomationPointEdit::SetPoint>(edits->at(0).change).point;
    const auto valueArrival = std::get<mu::engraving::AutomationPoint::ExplicitArrival>(valuePoint.value.inValue);
    EXPECT_NEAR(valuePoint.value.outValue.raw(), 0.95, 1e-9);
    EXPECT_NEAR(valueArrival.value.raw(), 0.45, 1e-9);
    EXPECT_DOUBLE_EQ(valueArrival.ease.t.raw(), 0.25);
    EXPECT_DOUBLE_EQ(valueArrival.ease.value.raw(), 0.8);

    evaluate("dragMode=3;dragTickDelta=120;written=[];commitTimeDrag();");
    edits = mu::appshell::dawautomation::channelAutomationEdits(evaluate("written[0]").toVariant().toList());
    ASSERT_TRUE(edits.has_value());
    const auto& move = std::get<mu::engraving::AutomationPointEdit::MovePoint>(edits->at(0).change);
    EXPECT_EQ(edits->at(0).tick, 600);
    EXPECT_EQ(move.from, 480);
    EXPECT_EQ(move.point.value, end);

    evaluate("points[1].shape='step';points[1].arrival=0.2;");
    EXPECT_DOUBLE_EQ(evaluate("pointEdit(points[1],{shape:'linear'}).arrival").toNumber(), 0.9);
}

TEST_F(DawAutomationLaneTests, CanvasCommandsReachArrivalBeforeTheOutgoingJump)
{
    muse::mpe::AutomationPoint end;
    end.outValue = 0.9;
    end.inValue = muse::mpe::AutomationPoint::ExplicitArrival { 0.4, muse::mpe::AutomationPoint::Ease::none() };
    setTwoPoints(end);
    evaluate("paint();");
    const QVariantList path = evaluate("strokePaths[0]").toVariant().toList();
    bool foundJump = false;
    for (int i = 1; i < path.size(); ++i) {
        const auto arrival = path[i - 1].toMap();
        const auto outgoing = path[i].toMap();
        if (arrival.value("x").toDouble() == 480 && outgoing.value("x").toDouble() == 480
            && std::abs(arrival.value("y").toDouble() - (8 + 0.6 * 146)) < 1e-9
            && std::abs(outgoing.value("y").toDouble() - (8 + 0.1 * 146)) < 1e-9) foundJump = true;
    }
    EXPECT_TRUE(foundJump);
}

TEST_F(DawAutomationLaneTests, DensePreviewAndGestureRetainExactSelectedIdentities)
{
    evaluate(R"JS(
        for(var i=0;i<10000;i++) {
            points.push({tick:i*10,value:0.4,arrival:0.3,shape:'curve',bend:0.8,bendTime:0.25});
            if(i%2===0) selectedTicks.push(i*10);
        }
        for(var index=0;index<selectedTicks.length;index++) selectedTickLookup[selectedTicks[index]]=true;
        dragMode=2;dragValueDelta=0.1;
        var shown=shownPoints();commitValueDrag();
    )JS");
    EXPECT_EQ(evaluate("shown.length").toInt(), 10000);
    EXPECT_EQ(evaluate("written[0].length").toInt(), 5000);
    EXPECT_TRUE(evaluate("shown.every(function(p,i){return p.tick===i*10 && p.selected===(i%2===0);})").toBool());
    EXPECT_TRUE(evaluate("written[0].every(function(p,i){return p.tick===i*20 && p.value===0.5 && p.arrival===0.4 && p.bendTime===0.25;})").toBool());
    EXPECT_DOUBLE_EQ(evaluate("shown[1].value").toNumber(), 0.4);
    EXPECT_DOUBLE_EQ(evaluate("shown[1].arrival").toNumber(), 0.3);
}
