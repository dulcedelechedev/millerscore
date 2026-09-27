/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2024 MuseScore Limited and others
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

#include "noterenderer.h"

#include <cmath>

#include "dom/arpeggio.h"
#include "dom/note.h"
#include "dom/staff.h"
#include "dom/swing.h"
#include "dom/tremolosinglechord.h"
#include "dom/tremolotwochord.h"

#include "glissandosrenderer.h"

#include "playback/metaparsers/chordarticulationsparser.h"
#include "playback/metaparsers/notearticulationsparser.h"

#include "playback/utils/repeatutils.h"
#include "playback/utils/arrangementutils.h"

#include "dom/masterscore.h"
#include "dom/utils.h"
#include "automation/automationdata.h"

using namespace mu::engraving;
using namespace muse;
using namespace muse::mpe;

namespace {
constexpr int CHANNEL_PITCH_RANGE_SEMITONES = 2;

static pitch_level_t channelPitchLevel(double normalized)
{
    const double semitones = (std::clamp(normalized, 0.0, 1.0) * 2.0 - 1.0) * CHANNEL_PITCH_RANGE_SEMITONES;
    return static_cast<pitch_level_t>(std::lround(semitones * PITCH_LEVEL_STEP));
}

static pitch_level_t pitchCurveValueAt(const PitchCurve& curve, duration_percentage_t position)
{
    if (curve.empty()) {
        return 0;
    }

    auto next = curve.upper_bound(position);
    if (next == curve.begin()) {
        return next->second;
    }
    const auto previous = std::prev(next);
    if (next == curve.end()) {
        return previous->second;
    }

    const double factor = static_cast<double>(position - previous->first) / (next->first - previous->first);
    return static_cast<pitch_level_t>(std::lround(previous->second + factor * (next->second - previous->second)));
}

static PitchCurve combinedPitchCurve(const PitchCurve& first, const PitchCurve& second)
{
    if (first.empty()) {
        return second;
    }
    if (second.empty()) {
        return first;
    }

    PitchCurve result;
    for (const auto& [position, value] : first) {
        result.insert_or_assign(position, value + pitchCurveValueAt(second, position));
    }
    for (const auto& [position, value] : second) {
        result.insert_or_assign(position, value + pitchCurveValueAt(first, position));
    }
    return result;
}

static PitchCurve pitchCurveSlice(const PitchCurve& curve, duration_percentage_t from, duration_percentage_t to)
{
    if (curve.empty()) {
        return {};
    }

    from = std::clamp(from, 0, HUNDRED_PERCENT);
    to = std::clamp(to, from, HUNDRED_PERCENT);
    PitchCurve result;
    if (from == to) {
        const pitch_level_t value = pitchCurveValueAt(curve, from);
        result.insert_or_assign(0, value);
        result.insert_or_assign(HUNDRED_PERCENT, value);
        return result;
    }

    result.insert_or_assign(0, pitchCurveValueAt(curve, from));
    for (const auto& [position, value] : curve) {
        if (position <= from || position >= to) {
            continue;
        }
        const float factor = static_cast<float>(position - from) / (to - from);
        result.insert_or_assign(percentageFromFactor(factor), value);
    }
    result.insert_or_assign(HUNDRED_PERCENT, pitchCurveValueAt(curve, to));
    return result;
}

static mpe::NoteEvent withAdditionalPitchCurve(mpe::NoteEvent event, const PitchCurve& curve)
{
    if (curve.empty()) {
        return event;
    }

    ArrangementContext arrangement = event.arrangementCtx();
    PitchContext pitch = event.pitchCtx();
    ExpressionContext expression = event.expressionCtx();
    pitch.pitchCurve = combinedPitchCurve(pitch.pitchCurve, curve);

    // All current synth adapters already consume a Multibend pitch curve. Add
    // a playback-only marker when notation did not provide one, keeping the
    // feature self-contained in the host without requiring backend forks.
    if (!expression.articulations.contains(ArticulationType::Multibend)) {
        ArticulationMeta meta(ArticulationType::Multibend);
        meta.timestamp = arrangement.actualTimestamp;
        meta.overallDuration = arrangement.actualDuration;
        expression.articulations.emplace(ArticulationType::Multibend,
                                         ArticulationAppliedData(std::move(meta), 0, HUNDRED_PERCENT));
    }
    return mpe::NoteEvent(std::move(arrangement), std::move(pitch), std::move(expression));
}

static double automationValueAt(const mu::engraving::AutomationCurve& curve, int tick)
{
    auto next = curve.upper_bound(tick);
    if (next == curve.begin()) {
        return curve.begin()->second.value.outValue.raw();
    }
    const auto previous = std::prev(next);
    if (next == curve.end()) {
        return previous->second.value.outValue.raw();
    }
    const double factor = static_cast<double>(tick - previous->first) / (next->first - previous->first);
    return mpe::evaluateAt(next->second.value, previous->second.value.outValue, factor).raw();
}

//! Channel-pitch automation is stored with the other score automation rather
//! than in daw.json. It is projected into each note's continuous pitch curve,
//! which lets the sequencers consume one shared representation.
static PitchCurve channelPitchAutomationCurve(const Note* note, const NominalNoteCtx& noteCtx)
{
    PitchCurve result;
    const MasterScore* master = note->masterScore();
    const AutomationDataConstPtr data = master ? master->automationData() : nullptr;
    if (!data) {
        return result;
    }

    const InstrumentTrackId trackId = makeInstrumentTrackId(note);
    if (!trackId.isValid()) {
        return result;
    }

    const mu::engraving::AutomationCurve& curve = data->curve(AutomationCurveKey::instrument(AutomationType::Pitch, trackId));
    if (curve.empty()) {
        return result;
    }

    const int startTick = noteCtx.chordCtx.nominalPositionStartTick + noteCtx.chordCtx.positionTickOffset;
    const int durationTicks = std::max(1, noteCtx.chordCtx.nominalDurationTicks);
    const int endTick = startTick + durationTicks;
    result.insert_or_assign(0, channelPitchLevel(automationValueAt(curve, startTick)));
    for (const auto& [tick, point] : curve) {
        static_cast<void>(point);
        if (tick <= startTick || tick >= endTick) {
            continue;
        }
        const float factor = static_cast<float>(tick - startTick) / durationTicks;
        result.insert_or_assign(percentageFromFactor(factor), channelPitchLevel(automationValueAt(curve, tick)));
    }
    result.insert_or_assign(HUNDRED_PERCENT, channelPitchLevel(automationValueAt(curve, endTick)));
    return result;
}
}

//! Composes the note's performance override (DAW Performance mode) with the
//! notation-derived context. Timing is converted through the tempo map at the
//! note's own repeat position, so every repeat occurrence moves consistently.
static void applyPerformanceOverride(const Note* note, NominalNoteCtx& noteCtx)
{
    const MasterScore* master = note->masterScore();
    if (!master || master->performanceOverlay().empty()) {
        return;
    }

    const EID eid = note->eid();
    const PerformanceNoteOverride* value = eid.isValid() ? master->performanceOverlay().find(eid) : nullptr;
    if (!value) {
        return;
    }

    const RenderingContext& chordCtx = noteCtx.chordCtx;
    const Score* score = chordCtx.score ? chordCtx.score : note->score();

    if (value->startOffsetTicks || value->playbackDurationTicks) {
        const int writtenTick = chordCtx.nominalPositionStartTick;
        const int startTick = std::max(0, writtenTick + value->startOffsetTicks.value_or(0));
        if (value->playbackDurationTicks) {
            const TimestampAndDuration timing = timestampAndDurationFromStartAndDurationTicks(
                score, startTick, std::max(1, *value->playbackDurationTicks), chordCtx.positionTickOffset);
            noteCtx.timestamp = timing.timestamp;
            noteCtx.duration = std::max<duration_t>(1, timing.duration);
        } else {
            // Keep the written sounding length (including ties); move the attack only.
            noteCtx.timestamp = timestampFromTicks(score, startTick + chordCtx.positionTickOffset);
        }
    }

    if (value->velocity) {
        noteCtx.userVelocityFraction = std::clamp(*value->velocity, 1, 127) / 127.f;
    }

    if (value->pitchOffsetCents) {
        const double pitchLevels = *value->pitchOffsetCents * mpe::PITCH_LEVEL_STEP / 100.0;
        noteCtx.pitchLevel += static_cast<mpe::pitch_level_t>(std::lround(pitchLevels));
    }
}

bool NoteRenderer::shouldRender(const Note* note, const RenderingContext& ctx, const muse::mpe::ArticulationMap& articulations)
{
    if (!note->play()) {
        return false;
    }

    const Tie* tie = note->tieBack();

    if (tie && tie->playSpanner()) {
        const Note* startNote = tie->startNote();
        const Note* endNote = tie->endNote();
        if (!startNote || !endNote) {
            return false;
        }

        if (tie->isPartialTie()) {
            // Play the partially tied note if there is no outgoing note in the previous repeat
            if (!findOutgoingNoteInPreviousRepeat(note, ctx).isValid()) {
                return true;
            }
        }

        const Chord* startChord = startNote->chord();
        const Chord* endChord = endNote->chord();

        // Helper function to check if tremolo should play
        auto shouldTremoloPlay = [](const Chord* chord) -> bool {
            if (chord->tremoloType() == TremoloType::INVALID_TREMOLO) {
                return false;
            }

            const TremoloSingleChord* singleTremolo = chord->tremoloSingleChord();
            if (singleTremolo) {
                return singleTremolo->playTremolo();
            }

            const TremoloTwoChord* twoTremolo = chord->tremoloTwoChord();
            if (twoTremolo) {
                return twoTremolo->playTremolo();
            }

            return false;
        };

        // Only render tied notes if tremolo is actually enabled for playback
        if (shouldTremoloPlay(startChord) || shouldTremoloPlay(endChord)) {
            return true;
        }

        if (startChord->arpeggio() && endChord->arpeggio()) {
            return false;
        }

        //!Note Checking whether the tied note has any multi-note articulation attached
        //!     If so, we can't ignore such note
        for (const auto& pair : articulations) {
            if (muse::mpe::isMultiNoteArticulation(pair.first) && !muse::mpe::isRangedArticulation(pair.first)) {
                return true;
            }
        }

        const auto& intervals = startChord->score()->spannerMap().findOverlapping(startChord->tick().ticks(),
                                                                                  startChord->endTick().ticks(),
                                                                                  /*excludeCollisions*/ true);
        for (const auto& interval : intervals) {
            const Spanner* sp = interval.value;
            if (sp->isTrill() && sp->playSpanner() && sp->endElement() == startChord) {
                return true;
            }
        }

        return false;
    }

    return true;
}

void NoteRenderer::render(const Note* note, const RenderingContext& ctx, mpe::PlaybackEventList& result)
{
    IF_ASSERT_FAILED(note) {
        return;
    }

    NominalNoteCtx noteCtx = buildNominalNoteCtx(note, ctx);
    if (!shouldRender(note, ctx, noteCtx.articulations)) {
        return;
    }

    const Tie* tieFor = note->tieFor();
    if (tieFor && tieFor->playSpanner()) {
        if (tieFor->isPartialTie()) {
            renderPartialTie(note, noteCtx);
        } else if (!tieFor->isLaissezVib()) {
            renderNormalTie(note, noteCtx);
        }
    }

    applySwingIfNeed(note, noteCtx);
    const PitchCurve channelPitchCurve = channelPitchAutomationCurve(note, noteCtx);
    applyPerformanceOverride(note, noteCtx);

    if (noteCtx.articulations.contains(ArticulationType::DiscreteGlissando)) {
        const size_t firstEvent = result.size();
        const mpe::NoteEvent baseEvent = buildNoteEvent(noteCtx);
        GlissandosRenderer::renderDiscreteGlissando(note, noteCtx, result);
        const mpe::ArrangementContext& baseArrangement = baseEvent.arrangementCtx();
        for (size_t index = firstEvent; index < result.size(); ++index) {
            mpe::NoteEvent* event = std::get_if<mpe::NoteEvent>(&result[index]);
            if (!event) {
                continue;
            }

            PitchCurve slice = channelPitchCurve;
            if (baseArrangement.actualDuration > 0) {
                const mpe::ArrangementContext& arrangement = event->arrangementCtx();
                const float fromFactor = static_cast<float>(arrangement.actualTimestamp - baseArrangement.actualTimestamp)
                                         / baseArrangement.actualDuration;
                const float toFactor = static_cast<float>(arrangement.actualTimestamp + arrangement.actualDuration
                                                          - baseArrangement.actualTimestamp)
                                       / baseArrangement.actualDuration;
                slice = pitchCurveSlice(channelPitchCurve, percentageFromFactor(fromFactor), percentageFromFactor(toFactor));
            }
            *event = withAdditionalPitchCurve(std::move(*event), slice);
        }
        return;
    }

    if (noteCtx.articulations.contains(ArticulationType::ContinuousGlissando)) {
        result.emplace_back(withAdditionalPitchCurve(buildNoteEvent(noteCtx), channelPitchCurve));
        return;
    }

    mpe::NoteEvent ev = withAdditionalPitchCurve(buildNoteEvent(noteCtx), channelPitchCurve);

    if (ev.arrangementCtx().actualTimestamp >= 0) {
        result.emplace_back(std::move(ev));
    } else {
        ArrangementContext arrCtx = ev.arrangementCtx();
        arrCtx.actualDuration = arrCtx.actualDuration + arrCtx.actualTimestamp;
        arrCtx.actualTimestamp = 0;

        PitchContext pitchCtx = ev.pitchCtx();
        ExpressionContext expCtx = ev.expressionCtx();

        result.emplace_back(mpe::NoteEvent(std::move(arrCtx), std::move(pitchCtx), std::move(expCtx)));
    }
}

void NoteRenderer::renderPartialTie(const Note* outgoingNote, NominalNoteCtx& outgoingNoteCtx)
{
    const RenderingContext& outgoingChordCtx = outgoingNoteCtx.chordCtx;
    const PartiallyTiedNoteInfo incomingNoteInfo = findIncomingNoteInNextRepeat(outgoingNote, outgoingChordCtx);
    if (!incomingNoteInfo.isValid()) {
        return;
    }

    const int incomingNotePositionTickOffset = incomingNoteInfo.repeat->utick - incomingNoteInfo.repeat->tick;
    RenderingContext incomingChordCtx = buildRenderingCtx(incomingNoteInfo.note->chord(), incomingNotePositionTickOffset,
                                                          outgoingChordCtx.profile, outgoingChordCtx.playbackCtx);

    ChordArticulationsParser::buildChordArticulationMap(incomingNoteInfo.note->chord(), incomingChordCtx,
                                                        incomingChordCtx.commonArticulations);

    NominalNoteCtx incomingNoteCtx = buildNominalNoteCtx(incomingNoteInfo.note, incomingChordCtx);

    if (shouldRender(incomingNoteInfo.note, incomingChordCtx, incomingNoteCtx.articulations)) {
        return;
    }

    const Tie* tieFor = incomingNoteInfo.note->tieFor();
    if (tieFor && tieFor->playSpanner() && !tieFor->isLaissezVib()) {
        renderNormalTie(incomingNoteInfo.note, incomingNoteCtx);
    }

    addTiedNote(incomingNoteCtx, outgoingNoteCtx);
    updateArticulationBoundaries(outgoingNoteCtx.timestamp, outgoingNoteCtx.duration, outgoingNoteCtx.articulations);
}

void NoteRenderer::renderNormalTie(const Note* firstNote, NominalNoteCtx& firstNoteCtx)
{
    std::unordered_set<const Note*> renderedNotes { firstNote };

    const RenderingContext& firstChordCtx = firstNoteCtx.chordCtx;
    const Tie* currTie = firstNote->tieFor();

    while (currTie && currTie->playSpanner()) {
        const Note* currNote = currTie->endNote();
        if (!currNote || !currNote->play()) {
            break;
        }

        if (muse::contains(renderedNotes, currNote)) {
            break; // prevents infinite loop
        }

        if (!notesInSameRepeat(firstChordCtx.score, firstNote, currNote, firstChordCtx.positionTickOffset)) {
            const TieJumpPointList* jumpPoints = firstNote->tieJumpPoints();
            if (jumpPoints && !jumpPoints->empty()) {
                renderPartialTie(firstNote, firstNoteCtx);
            }
            break;
        }

        const Chord* chord = currNote->chord();
        if (!chord) {
            break;
        }

        RenderingContext currChordCtx = buildRenderingCtx(chord, firstChordCtx.positionTickOffset,
                                                          firstChordCtx.profile, firstChordCtx.playbackCtx);
        ChordArticulationsParser::buildChordArticulationMap(chord, currChordCtx, currChordCtx.commonArticulations);

        const NominalNoteCtx currNoteCtx = buildNominalNoteCtx(currNote, currChordCtx);
        if (shouldRender(currNote, currChordCtx, currNoteCtx.articulations)) {
            if (currNoteCtx.articulations.contains(ArticulationType::DiscreteGlissando)) {
                firstNoteCtx.duration += GlissandosRenderer::discreteGlissandoStepDuration(currNote, currNoteCtx.duration);
            }

            break;
        }

        addTiedNote(currNoteCtx, firstNoteCtx);

        currTie = currNote->tieFor();
        renderedNotes.insert(currNote);
    }

    if (firstNoteCtx.articulations.size() > 1) {
        firstNoteCtx.articulations.erase(mpe::ArticulationType::Standard);
    }

    updateArticulationBoundaries(firstNoteCtx.timestamp, firstNoteCtx.duration, firstNoteCtx.articulations);
}

void NoteRenderer::addTiedNote(const NominalNoteCtx& tiedNoteCtx, NominalNoteCtx& firstNoteCtx)
{
    if (tiedNoteCtx.articulations.size() == 1) {
        if (tiedNoteCtx.articulations.begin()->first == mpe::ArticulationType::Standard) {
            firstNoteCtx.duration += tiedNoteCtx.duration;
            return;
        }
    }

    const float avgDurationFactor = percentageToFactor(tiedNoteCtx.articulations.averageDurationFactor());
    firstNoteCtx.duration += tiedNoteCtx.duration * avgDurationFactor;

    // Ignore these articulations so we won't re-apply them to the total duration
    static const ArticulationTypeSet ARTICULATION_TO_IGNORE_TYPES {
        ArticulationType::Staccato,
        ArticulationType::Staccatissimo,
    };

    for (const auto& pair : tiedNoteCtx.articulations) {
        if (!muse::contains(ARTICULATION_TO_IGNORE_TYPES, pair.first)) {
            firstNoteCtx.articulations.insert(pair);
        }
    }
}

void NoteRenderer::updateArticulationBoundaries(const timestamp_t noteTimestamp, const duration_t noteDuration,
                                                ArticulationMap& articulations)
{
    const timestamp_t noteTimestampTo = noteTimestamp + noteDuration;
    IF_ASSERT_FAILED(noteTimestampTo > 0) {
        return;
    }

    for (const auto& pair : articulations) {
        const ArticulationAppliedData& articulation = pair.second;

        const duration_percentage_t occupiedFrom = mpe::occupiedPercentage(articulation.meta.timestamp,
                                                                           noteTimestampTo);
        const duration_percentage_t occupiedTo = mpe::occupiedPercentage(articulation.meta.timestamp + articulation.meta.overallDuration,
                                                                         noteTimestampTo);

        articulations.updateOccupiedRange(pair.first, occupiedFrom, occupiedTo);
    }
}

void NoteRenderer::applySwingIfNeed(const Note* note, NominalNoteCtx& noteCtx)
{
    const Chord* chord = note->chord();
    if (!chord || chord->tuplet()) {
        return;
    }

    const SwingParameters swing = chord->staff()->swing(chord->tick());
    if (!swing.isOn()) {
        return;
    }

    //! NOTE: Swing must be applied to the "raw" note duration, but not to the additional duration (e.g, from a tied note)
    const Swing::ChordDurationAdjustment swingDurationAdjustment = Swing::applySwing(chord, swing);
    const duration_t additionalDuration = noteCtx.duration - noteCtx.chordCtx.nominalDuration;
    noteCtx.timestamp = noteCtx.timestamp + noteCtx.chordCtx.nominalDuration * swingDurationAdjustment.remainingDurationMultiplier;
    noteCtx.duration = noteCtx.chordCtx.nominalDuration * swingDurationAdjustment.durationMultiplier + additionalDuration;
}

NominalNoteCtx NoteRenderer::buildNominalNoteCtx(const Note* note, const RenderingContext& ctx)
{
    NominalNoteCtx noteCtx(note, ctx);
    NoteArticulationsParser::buildNoteArticulationMap(note, ctx, noteCtx.articulations);

    return noteCtx;
}
