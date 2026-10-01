/* SPDX-License-Identifier: GPL-3.0-only */
#include <gtest/gtest.h>
#include <QFile>
#include <QJSEngine>
#include <QRegularExpression>
#include <QVariant>

#include "appshell/qml/MuseScore/AppShell/dawautomationutils.h"
#include "mpe/automationpoint.h"
#include "notationautomation_fixture.h"
#include "automation/utils/automationtestutils.h"

class DawAutomationLaneTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        QFile file(QStringLiteral(MILLERSCORE_TEST_SOURCE_ROOT "/src/appshell/qml/MuseScore/AppShell/Daw/DawAutomationLane.qml"));
        ASSERT_TRUE(file.open(QIODevice::ReadOnly));
        source = QString::fromUtf8(file.readAll());
        evaluate(R"JS(
            var points=[], selectedTicks=[], selectedTickLookup=Object.create(null), written=[];
            var dragMode=0, dragValueDelta=0, dragTickDelta=0, bendTick=-1, bendPreview=-1;
            var defaultValue=0.5, width=1000, height=180, viewX=0, pixelsPerTick=1;
            var topPad=8, availableHeight=146, snapTicks=120, transport=null;
            var enabled=true, pencilTool=false, contextKey='part:1:piano:cc:7';
            var strokePoints=[], strokeBaseline=[], strokeByTick=Object.create(null), strokeLastSample=null;
            var strokeShape='step', strokeContextKey='', emittedState=[], paintRequests=0;
            var canvas={requestPaint:function(){paintRequests++;}};
            function editRequested(edits) {
                written.push(edits);
                emittedState.push({mode:dragMode,strokeCount:strokePoints.length});
            }
        )JS");
        loadFunctions({ "xForTick", "yForValue", "isSelected", "evaluateSegment", "shownPoints", "valueAtTick",
                        "pointEdit", "commitValueDrag", "commitTimeDrag" });
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

    void loadFunctions(std::initializer_list<const char*> names)
    {
        for (const auto name : names) {
            loadFunction(QString::fromLatin1(name));
        }
    }

    void loadFunction(const QString& name)
    {
        const QRegularExpression re(QString("    (function %1\\([^\\n]+\\}\\r?\\n|function %1\\(.*?\\n    \\})").arg(name),
                                    QRegularExpression::DotMatchesEverythingOption);
        const auto match = re.match(source);
        ASSERT_TRUE(match.hasMatch()) << name.toStdString();
        evaluate(match.captured(1));
    }

    void loadStrokeFunctions()
    {
        // Include production helpers as well as the gesture entry points. Only
        // root-level functions are extracted; rendered Qt input is not claimed.
        const QRegularExpression names(QStringLiteral("^    function ([A-Za-z_]\\w*)\\("),
                                       QRegularExpression::MultilineOption);
        auto matches = names.globalMatch(source);
        while (matches.hasNext()) loadFunction(matches.next().captured(1));
        for (const auto name : { "beginStroke", "appendStroke", "strokePreviewPoints", "strokeEdits", "commitStroke", "cancelDrag" }) {
            ASSERT_TRUE(engine.globalObject().property(QString::fromLatin1(name)).isCallable()) << name;
        }
    }

    void setCurve(const mu::engraving::AutomationCurve& curve)
    {
        QVariantList projected;
        double previous = 0.5;
        for (const auto& [tick, point] : curve) {
            const auto ease = muse::mpe::ease(point.value);
            projected.push_back(QVariantMap {
                {"tick", tick}, {"value", point.value.outValue.raw()},
                {"arrival", muse::mpe::resolveInValue(point.value, muse::real_t(previous)).raw()},
                {"shape", !ease ? "step" : (ease->isNone() ? "linear" : "curve")},
                {"bend", ease ? ease->value.raw() : 0.5}, {"bendTime", ease ? ease->t.raw() : 0.5}
            });
            previous = point.value.outValue.raw();
        }
        engine.globalObject().setProperty("points", engine.toScriptValue(projected));
    }

    void expectExactCurve(const mu::engraving::AutomationCurve& actual, const mu::engraving::AutomationCurve& expected)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (const auto& [tick, point] : expected) {
            const auto found = actual.find(tick);
            ASSERT_NE(found, actual.end()) << tick;
            EXPECT_EQ(found->second, point) << tick;
        }
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

    QString source;
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

TEST_F(DawAutomationLaneTests, PencilFillsFastPointerSegmentsWithoutEditingTheSavedCurve)
{
    loadStrokeFunctions();
    evaluate(R"JS(
        points=[{tick:20,value:0.7,arrival:0.3,shape:'curve',bend:0.8,bendTime:0.25},
                {tick:300,value:0.1,arrival:0.6,shape:'linear',bend:0.5,bendTime:0.5}];
        var savedPoints=JSON.stringify(points);
        beginStroke(120,yForValue(0.2),false);
        appendStroke(168,yForValue(0.8));
        var preview=strokePreviewPoints();
    )JS");
    EXPECT_EQ(evaluate("dragMode").toInt(), 7);
    EXPECT_EQ(evaluate("written.length").toInt(), 0);
    EXPECT_TRUE(evaluate("JSON.stringify(points)===savedPoints").toBool());
    EXPECT_EQ(evaluate("strokePoints.length").toInt(), 13);
    EXPECT_TRUE(evaluate("strokePoints.every(function(p,i){return i===0 || p.tick-strokePoints[i-1].tick<=4;})").toBool());
    EXPECT_TRUE(evaluate("strokePoints.every(function(p){return p.shape==='step' && p.value>=0 && p.value<=1;})").toBool());
    EXPECT_EQ(evaluate("strokePoints[0].tick").toInt(), 120);
    EXPECT_EQ(evaluate("strokePoints[12].tick").toInt(), 168);
    EXPECT_NEAR(evaluate("strokeByTick[144].value").toNumber(), 0.5, 1e-9);
    EXPECT_TRUE(evaluate("preview[0]===points[0] && preview[preview.length-1]===points[1]").toBool());
    EXPECT_TRUE(evaluate("shownPoints().filter(function(p){return p.selected;}).length===strokePoints.length").toBool());
    EXPECT_TRUE(evaluate("commitStroke()").toBool());
    EXPECT_EQ(evaluate("written.length").toInt(), 1);
    EXPECT_EQ(evaluate("emittedState[0].mode").toInt(), 0);
    EXPECT_EQ(evaluate("emittedState[0].strokeCount").toInt(), 0);
    EXPECT_EQ(evaluate("selectedTicks.length").toInt(), 13);
    EXPECT_TRUE(evaluate("JSON.stringify(points)===savedPoints").toBool());
    EXPECT_FALSE(evaluate("commitStroke()").toBool());
    EXPECT_EQ(evaluate("written.length").toInt(), 1);
}

TEST_F(DawAutomationLaneTests, PencilSnapsToUniqueOrderedTicksAndReverseMotionUsesTheLatestSample)
{
    loadStrokeFunctions();
    evaluate(R"JS(
        pixelsPerTick=0.1;
        transport={snapTick:function(tick,step){return Math.round(tick/step)*step;}};
        beginStroke(0,yForValue(0.2),false);
        appendStroke(48,yForValue(0.8));
        appendStroke(24,yForValue(0.4));
        var samples=strokePoints.slice();
        var batch=strokeEdits();
    )JS");
    ASSERT_EQ(evaluate("samples.length").toInt(), 5);
    EXPECT_TRUE(evaluate("samples.every(function(p,i){return p.tick===i*120;})").toBool());
    EXPECT_NEAR(evaluate("strokeByTick[240].value").toNumber(), 0.4, 1e-9);
    EXPECT_TRUE(evaluate("batch.every(function(p,i){return p.op==='set' && (i===0 || p.tick>batch[i-1].tick);})").toBool());
    EXPECT_TRUE(evaluate("commitStroke()").toBool());
    EXPECT_EQ(evaluate("written.length").toInt(), 1);
    EXPECT_TRUE(evaluate("written[0].every(function(p,i){return p.tick===i*120;})").toBool());
    EXPECT_TRUE(mu::appshell::dawautomation::channelAutomationEdits(evaluate("written[0]").toVariant().toList()).has_value());
}

TEST_F(DawAutomationLaneTests, PencilClampsValuesAndViewportCoordinatesWithAScrolledTimeline)
{
    loadStrokeFunctions();
    evaluate(R"JS(
        viewX=120;pixelsPerTick=0.5;
        beginStroke(-200,-500,false);
    )JS");
    ASSERT_EQ(evaluate("strokePoints.length").toInt(), 1);
    EXPECT_EQ(evaluate("strokePoints[0].tick").toInt(), 240);
    EXPECT_DOUBLE_EQ(evaluate("strokePoints[0].value").toNumber(), 1.0);
    evaluate("appendStroke(2000,10000);");
    EXPECT_EQ(evaluate("strokePoints[strokePoints.length-1].tick").toInt(), 2240);
    EXPECT_DOUBLE_EQ(evaluate("strokePoints[strokePoints.length-1].value").toNumber(), 0.0);
    EXPECT_TRUE(evaluate("strokePoints.every(function(p,i){return Number.isInteger(p.tick) && p.tick>=240 && p.tick<=2240 && p.value>=0 && p.value<=1 && (i===0 || p.tick>strokePoints[i-1].tick);})").toBool());
    EXPECT_TRUE(evaluate("commitStroke()").toBool());
    EXPECT_TRUE(mu::appshell::dawautomation::channelAutomationEdits(evaluate("written[0]").toVariant().toList()).has_value());
}

TEST_F(DawAutomationLaneTests, PencilCancelAndInvalidPointerSamplesNeverWrite)
{
    loadStrokeFunctions();
    evaluate(R"JS(
        points=[{tick:0,value:0.2,arrival:0.1,shape:'curve',bend:0.7,bendTime:0.25}];
        var savedPoints=JSON.stringify(points);
        beginStroke(120,yForValue(0.3),true);
        appendStroke(168,yForValue(0.7));
        var savedPreview=JSON.stringify(strokePoints);
        appendStroke(NaN,10);appendStroke(10,Infinity);
    )JS");
    EXPECT_TRUE(evaluate("JSON.stringify(strokePoints)===savedPreview").toBool());
    evaluate("cancelDrag();");
    EXPECT_EQ(evaluate("dragMode").toInt(), 0);
    EXPECT_EQ(evaluate("strokePoints.length+strokeBaseline.length").toInt(), 0);
    EXPECT_TRUE(evaluate("strokeLastSample===null").toBool());
    EXPECT_FALSE(evaluate("commitStroke()").toBool());
    EXPECT_EQ(evaluate("written.length").toInt(), 0);
    EXPECT_TRUE(evaluate("JSON.stringify(points)===savedPoints").toBool());
    for (const auto scale : { "0", "-1", "NaN", "Infinity" }) {
        evaluate(QStringLiteral("pixelsPerTick=%1;beginStroke(120,40,false);").arg(QString::fromLatin1(scale)));
        EXPECT_EQ(evaluate("dragMode").toInt(), 0);
        EXPECT_FALSE(evaluate("commitStroke()").toBool());
    }
    EXPECT_EQ(evaluate("written.length").toInt(), 0);
}

TEST_F(DawAutomationLaneTests, PencilCommitDiscardsDisabledOrChangedLaneContexts)
{
    loadStrokeFunctions();
    evaluate("beginStroke(120,yForValue(0.3),false);appendStroke(168,yForValue(0.7));enabled=false;");
    EXPECT_FALSE(evaluate("commitStroke()").toBool());
    EXPECT_EQ(evaluate("dragMode").toInt(), 0);
    EXPECT_EQ(evaluate("written.length").toInt(), 0);
    evaluate("enabled=true;beginStroke(120,yForValue(0.3),false);contextKey='part:2:flute:cc:7';");
    EXPECT_FALSE(evaluate("commitStroke()").toBool());
    EXPECT_EQ(evaluate("dragMode").toInt(), 0);
    EXPECT_EQ(evaluate("strokePoints.length").toInt(), 0);
    EXPECT_EQ(evaluate("written.length").toInt(), 0);
    evaluate("contextKey='part:1:piano:cc:10';");
    EXPECT_FALSE(evaluate("commitStroke()").toBool());
    EXPECT_EQ(evaluate("written.length").toInt(), 0);
    evaluate("enabled=false;beginStroke(120,40,false);");
    EXPECT_EQ(evaluate("dragMode").toInt(), 0);
}

TEST_F(DawAutomationLaneTests, PencilStepAndCurvedPreviewsMatchTheNativeIncomingEvaluator)
{
    loadStrokeFunctions();
    for (const bool curved : { false, true }) {
        evaluate(QStringLiteral("beginStroke(120,yForValue(0.2),%1);appendStroke(168,yForValue(0.8));").arg(curved ? "true" : "false"));
        auto edits = mu::appshell::dawautomation::channelAutomationEdits(evaluate("strokeEdits()").toVariant().toList());
        ASSERT_TRUE(edits.has_value());
        muse::mpe::AutomationCurve<int> curve;
        for (const auto& edit : *edits) {
            const auto& point = std::get<mu::engraving::AutomationPointEdit::SetPoint>(edit.change).point.value;
            curve.emplace(edit.tick, point);
            const auto ease = muse::mpe::ease(point);
            ASSERT_EQ(ease.has_value(), curved);
            if (curved) {
                EXPECT_DOUBLE_EQ(ease->t.raw(), 0.5);
                EXPECT_DOUBLE_EQ(ease->value.raw(), 0.5);
            }
        }
        for (const int tick : { 119, 120, 121, 122, 143, 144, 145, 167, 168, 169 }) {
            EXPECT_NEAR(evaluate(QStringLiteral("valueAtTick(%1)").arg(tick)).toNumber(),
                        muse::mpe::evaluateCurveAt(curve, tick).raw(), 1e-9) << tick << " " << curved;
        }
        EXPECT_NEAR(evaluate("valueAtTick(122)").toNumber(), curved ? 0.225 : 0.2, 1e-9);
        evaluate("cancelDrag();");
    }
    EXPECT_EQ(evaluate("written.length").toInt(), 0);
}

TEST_F(DawAutomationLaneTests, PencilBatchIsOneNativeUndoTransactionAndPreservesOutsidePointsAndOtherLanes)
{
    loadStrokeFunctions();
    using namespace mu::engraving;
    const AutomationCurve original {
        {0, generatedPoint(0.6, 0.2, {0.25, 0.8})}, {100, customPoint(0.4, 0.1, {0.3, 0.7})},
        {160, customPoint(0.3, 0.9)}, {170, customPoint(0.5, 0.7)},
        {240, customPoint(0.7, 0.4, {0.8, 0.2})}, {480, customPoint(0.2, 0.8)}
    };
    setCurve(original);
    evaluate("beginStroke(120,yForValue(0.2),false);appendStroke(200,yForValue(0.8));");
    EXPECT_EQ(evaluate("written.length").toInt(), 0);
    EXPECT_TRUE(evaluate("strokeEdits().some(function(p){return p.tick===170 && p.op==='erase';})").toBool());
    EXPECT_TRUE(evaluate("strokeEdits().every(function(p,i,a){return i===0 || p.tick>a[i-1].tick;})").toBool());
    EXPECT_TRUE(evaluate("commitStroke()").toBool());
    ASSERT_EQ(evaluate("written.length").toInt(), 1);
    auto edits = mu::appshell::dawautomation::channelAutomationEdits(evaluate("written[0]").toVariant().toList());
    ASSERT_TRUE(edits.has_value());

    MasterScore score;
    const InstrumentTrackId piano {muse::ID(400), u"piano"};
    const InstrumentTrackId flute {muse::ID(401), u"flute"};
    const auto key = AutomationCurveKey::midiLane(piano, "cc:7");
    const auto sibling = AutomationCurveKey::midiLane(piano, "cc:10");
    const auto otherTrack = AutomationCurveKey::midiLane(flute, "cc:7");
    score.controller.data->setCurves({{key, original}, {sibling, original}, {otherTrack, original}});
    auto undo = std::make_shared<mu::notation::FakeNotationUndoStack>();
    mu::notation::NotationAutomation notation(undo);
    notation.setMasterScore(&score);
    notation.editPoints(key, *edits);
    EXPECT_EQ(undo->transactions, 1);
    ASSERT_EQ(score.commands.size(), 1u);
    const auto after = notation.automationData()->curve(key);
    EXPECT_EQ(after.find(170), after.end());
    for (const int tick : {0, 100, 240, 480}) EXPECT_EQ(after.at(tick), original.at(tick)) << tick;
    for (const auto& [tick, point] : after) {
        if (tick >= 120 && tick <= 200) {
            EXPECT_FALSE(point.generated);
            EXPECT_FALSE(point.itemId.has_value());
            EXPECT_FALSE(muse::mpe::ease(point.value).has_value());
        }
    }
    expectExactCurve(notation.automationData()->curve(sibling), original);
    expectExactCurve(notation.automationData()->curve(otherTrack), original);
    score.undoLast();
    expectExactCurve(notation.automationData()->curve(key), original);
    expectExactCurve(notation.automationData()->curve(sibling), original);
    expectExactCurve(notation.automationData()->curve(otherTrack), original);
    score.redoLast();
    expectExactCurve(notation.automationData()->curve(key), after);
    expectExactCurve(notation.automationData()->curve(sibling), original);
    expectExactCurve(notation.automationData()->curve(otherTrack), original);
}

TEST_F(DawAutomationLaneTests, PencilOnATenThousandPointLaneKeepsEveryOutsidePointAndUndoesExactly)
{
    loadStrokeFunctions();
    using namespace mu::engraving;
    AutomationCurve original;
    for (int index = 0; index < 10000; ++index) original.emplace(index * 10, customPoint(0.3, 0.4, {0.25, 0.8}));
    setCurve(original);
    evaluate("pixelsPerTick=0.1;beginStroke(300,yForValue(0.2),true);appendStroke(400,yForValue(0.8));");
    EXPECT_TRUE(evaluate("strokePreviewPoints().every(function(p,i,a){return i===0 || p.tick>a[i-1].tick;})").toBool());
    const size_t sampleCount = size_t(evaluate("strokePoints.length").toInt());
    EXPECT_EQ(size_t(evaluate("strokePreviewPoints().length").toInt()), original.size() - 101 + sampleCount);
    EXPECT_TRUE(evaluate("commitStroke()").toBool());
    ASSERT_EQ(evaluate("written.length").toInt(), 1);
    auto edits = mu::appshell::dawautomation::channelAutomationEdits(evaluate("written[0]").toVariant().toList());
    ASSERT_TRUE(edits.has_value());
    MasterScore score;
    const auto key = AutomationCurveKey::midiLane({muse::ID(402), u"piano"}, "cc:11");
    score.controller.data->setCurves({{key, original}});
    auto undo = std::make_shared<mu::notation::FakeNotationUndoStack>();
    mu::notation::NotationAutomation notation(undo);
    notation.setMasterScore(&score);
    notation.editPoints(key, *edits);
    ASSERT_EQ(score.commands.size(), 1u);
    EXPECT_EQ(undo->transactions, 1);
    const auto after = notation.automationData()->curve(key);
    EXPECT_EQ(after.size(), original.size() - 101 + sampleCount);
    for (const auto& [tick, point] : original) {
        if (tick < 3000 || tick > 4000) EXPECT_EQ(after.at(tick), point) << tick;
    }
    score.undoLast();
    expectExactCurve(notation.automationData()->curve(key), original);
    score.redoLast();
    expectExactCurve(notation.automationData()->curve(key), after);
}
