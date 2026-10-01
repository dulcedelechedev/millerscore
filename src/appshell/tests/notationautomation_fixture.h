/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

// Selected production notation and undo methods are compiled unchanged with a
// non-repeating score and a deterministic transaction service. Repeat mapping,
// score notifications/layout, and the application UndoStack need integration QA.
#include "engraving/automation/automationdata.h"
#include "engraving/types/fraction.h"
#include "global/types/translatablestring.h"
#include "global/log.h"

namespace mu::engraving {
struct ChangedRange { Fraction tickFrom; Fraction tickTo; size_t staffIdxFrom; size_t staffIdxTo; };
struct RepeatList { int utick2tick(int tick) const { return tick; } };
struct Staff { muse::ID id() const { return muse::ID(1); } };
struct Measure { Fraction endTick() const { return Fraction::fromTicks(10000); } };
struct Score {
    RepeatList repeats;
    size_t nstaves() const { return 0; }
    const Staff* staff(size_t) const { return nullptr; }
    const RepeatList& expandedRepeatList() const { return repeats; }
    const Measure* lastMeasure() const { return nullptr; }
};
struct ScoreAutomationController {
    AutomationDataPtr data = std::make_shared<AutomationData>();
    AutomationDataConstPtr automationData() const { return data; }
    void editPoints(const AutomationCurveKey& key, AutomationPointEdits& edits) { data->editPoints(key, edits); }
};
class EditAutomationPoints
{
public:
    EditAutomationPoints(Score*, ScoreAutomationController*, const AutomationCurveKey&, const AutomationPointEdits&);
    std::optional<ChangedRange> changedRange() const;
    void flip();
    Score* m_score;
    ScoreAutomationController* m_controller;
    AutomationCurveKey m_key;
    std::map<utick_t, std::optional<AutomationPoint>> m_pointStates;
    std::optional<ChangedRange> m_changedRange;
};
struct MasterScore : Score {
    ScoreAutomationController controller;
    ScoreAutomationController* m_automationController = &controller;
    std::vector<std::unique_ptr<EditAutomationPoints>> commands;
    size_t cursor = 0;
    AutomationDataConstPtr automationData() const;
    void editAutomationPoints(const AutomationCurveKey&, AutomationPointEdits&, bool undoable = true);
    void undo(EditAutomationPoints* command)
    {
        commands.resize(cursor);
        commands.emplace_back(command);
        command->flip();
        cursor = commands.size();
    }
    void undoLast() { if (cursor > 0) commands[--cursor]->flip(); }
    void redoLast() { if (cursor < commands.size()) commands[cursor++]->flip(); }
};
struct Transaction {};
}
namespace mu::notation {
using engraving::AutomationCurveKey;
using engraving::AutomationDataConstPtr;
using engraving::AutomationPointEdits;
struct FakeNotationUndoStack {
    int transactions = 0;
    muse::TranslatableString lastLabel;
    template<typename Callback> void transaction(muse::TranslatableString label, Callback callback)
    {
        ++transactions;
        lastLabel = std::move(label);
        engraving::Transaction transaction;
        callback(transaction);
    }
};
using INotationUndoStackPtr = std::shared_ptr<FakeNotationUndoStack>;
class NotationAutomation
{
public:
    explicit NotationAutomation(INotationUndoStackPtr);
    AutomationDataConstPtr automationData() const;
    void editPoints(const AutomationCurveKey&, AutomationPointEdits&);
    void setMasterScore(engraving::MasterScore*);
    engraving::MasterScore* m_masterScore = nullptr;
    const INotationUndoStackPtr m_undoStack;
};
}
