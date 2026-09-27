/* SPDX-License-Identifier: GPL-3.0-only */
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Muse.Ui
import Muse.UiComponents
import MuseScore.AppShell

//! Piano roll for the selected score part. Notes are projections of score
//! notes: Notes mode edits the score, Performance mode edits only playback.
Rectangle {
    id: root

    DawTheme { id: dawTheme }

    required property var projectModel
    required property var transport
    required property var tracksModel
    property real timelineContentX: 0
    property real pixelsPerTick: 0.1
    property int snapTicks: 120

    property NavigationSection navigationSection: null
    property int navigationOrderStart: 0

    signal timelineScrollRequested(real value)
    //! Horizontal zoom is the workspace's (shared with the tracks); `viewX` stays put
    signal zoomRequested(real factor, real viewX)
    signal maximizeRequested()

    property bool canZoomIn: true
    property bool canZoomOut: true
    property bool maximized: false

    property var notes: []
    property var visibleNotes: []
    property real windowStartTick: 0
    property real windowEndTick: -1
    property bool drawTool: false
    property int newNoteTicks: 480
    property bool autoFitPitch: true
    property int highestPitch: 84
    property int lowestPitch: 36
    property string feedbackText: ""
    property bool feedbackIsError: false
    property int cancelToken: 0
    property real controlLaneHeight: 132
    property string controlLaneParameter: "noteVelocity"
    property var channelAutomationPoints: []
    property var midiControllerSpecs: []

    //! Several notes can be selected; the model keeps the score selection in step with them.
    //! selectedNotes holds the selected notes of this part, in `notes` order.
    property var selectedLookup: ({})
    property var selectedNotes: []
    property var selectedPitches: ({})
    //! Where a Shift+click range starts: the last note clicked without Shift
    property string selectionAnchorId: ""
    property var timeOrderedNotes: []
    //! A drag moves (notes, Performance mode) or changes (control lane) every selected note by the
    //! same amount, previewed until release and then committed as one undo step
    property bool noteDragActive: false
    property real noteDragTicks: 0
    //! Notes mode: semitones the dragged notes move up (negative: down)
    property int noteDragPitch: 0
    property bool laneDragActive: false
    property real laneDragDelta: 0

    readonly property var baseControlLaneModel: [
        { text: qsTrc("appshell", "Note pan — unavailable"), value: "notePan", enabled: false, channel: false, bipolar: false,
          reason: qsTrc("appshell", "Per-note pan is unavailable because the current playback backends do not expose an isolated pan control for each simultaneous note.") },
        { text: root.tracksModel.selectedSupportsPerNoteVelocity
                ? qsTrc("appshell", "Note velocity") : qsTrc("appshell", "Note velocity — unavailable for Muse Sounds"),
          value: "noteVelocity", enabled: root.tracksModel.selectedSupportsPerNoteVelocity, channel: false, bipolar: false,
          reason: qsTrc("appshell", "Muse Sounds does not expose independent velocity for simultaneous notes. Choose a SoundFont or VST3 instrument to edit per-note velocity.") },
        { text: qsTrc("appshell", "Note release — unavailable"), value: "noteRelease", enabled: false, channel: false, bipolar: false,
          reason: qsTrc("appshell", "Release velocity is unavailable because Muse Sounds and SoundFont playback do not expose a per-note release value.") },
        { text: qsTrc("appshell", "Note filter cutoff frequency — unavailable"), value: "noteFilterCutoff", enabled: false, channel: false,
          bipolar: false,
          reason: qsTrc("appshell", "Per-note filter cutoff is unavailable in the shared playback contract.") },
        { text: qsTrc("appshell", "Note filter resonance (Q) — unavailable"), value: "noteFilterResonance", enabled: false, channel: false,
          bipolar: false,
          reason: qsTrc("appshell", "Per-note filter resonance is unavailable in the shared playback contract.") },
        { text: qsTrc("appshell", "Note fine pitch"), value: "notePitch", enabled: true, channel: false, bipolar: true },
        { text: qsTrc("appshell", "Channel panning"), value: "channelPan", enabled: true, bipolar: true, channel: true },
        { text: qsTrc("appshell", "Channel volume"), value: "channelVolume", enabled: true, bipolar: false, channel: true },
        { text: qsTrc("appshell", "Channel pitch"), value: "channelPitch", enabled: true, bipolar: true, channel: true }
    ]
    readonly property var controlLaneModel: baseControlLaneModel.concat(midiControllerSpecs)

    //! Left column width; the workspace passes the track-header width so bars
    //! line up vertically between arranger and piano roll.
    property int keyWidth: 224
    readonly property int keyLength: 64
    //! Vertical zoom: the height of one pitch row
    property int noteHeight: 14
    readonly property int minimumNoteHeight: 6
    readonly property int maximumNoteHeight: 36
    readonly property int minimumPitchSpan: 36
    readonly property int pitchPadding: 12
    readonly property bool performanceMode: projectModel.editMode === DawProjectModel.Performance
    readonly property color trackColor: dawTheme.trackColor(tracksModel.selectedColorIndex)
    //! Every selected track (Ctrl/Shift+click in the tracks): their notes are shown together, each in
    //! its track's colour, and all can be edited; drawing goes to the primary (last clicked) track.
    readonly property var selectedTracks: tracksModel.selectedTracks
    readonly property bool multiTrack: selectedTracks.length > 1
    readonly property int startTrack: tracksModel.selectedStartTrack
    readonly property int endTrack: tracksModel.selectedEndTrack
    readonly property string drawBlockedReason: tracksModel.selectedDrawTargetError
    readonly property int staffCount: startTrack >= 0 ? Math.max(1, (endTrack - startTrack) / 4) : 0
    //! Split point for multi-staff parts (piano, harp, organ): below middle C
    //! goes to the second staff, like the score's own note input.
    readonly property int splitPitch: 60
    readonly property bool drawActive: drawTool && !performanceMode && startTrack >= 0 && drawBlockedReason.length === 0
    readonly property var selectedNote: findNote(projectModel.selectedProjectedNoteId, notes)
    readonly property real controlLaneExtent: Math.min(controlLaneHeight, Math.max(92, body.height - 120))

    color: dawTheme.workspace

    function refreshNotes() {
        const tracks = selectedTracks
        const projected = startTrack >= 0 && tracks.length > 0 ? projectModel.projectedScoreNotesForRanges(tracks) : []
        for (let index = 0; index < projected.length; ++index) {
            const track = tracks[projected[index].rangeIndex]
            projected[index].colorIndex = track ? track.colorIndex : tracksModel.selectedColorIndex
            projected[index].primaryTrack = track ? track.primary : true
        }
        notes = projected
        timeOrderedNotes = notes.slice().sort((a, b) => (a.startTick - b.startTick) || (a.pitch - b.pitch))
        refreshSelection()
        updateWindow(true)
        if (autoFitPitch)
            Qt.callLater(fitNotes)
    }

    function findNote(noteId, list) {
        if (!noteId || noteId.length === 0)
            return null
        for (let index = 0; index < list.length; ++index) {
            if (list[index].noteId === noteId)
                return list[index]
        }
        return null
    }

    function selectRelativeNote(step) {
        if (notes.length === 0)
            return
        let current = -1
        for (let index = 0; index < notes.length; ++index) {
            if (notes[index].noteId === projectModel.selectedProjectedNoteId) {
                current = index
                break
            }
        }
        const next = current < 0 ? (step < 0 ? notes.length - 1 : 0)
                                 : Math.max(0, Math.min(notes.length - 1, current + step))
        projectModel.selectProjectedNote(notes[next].noteId)
    }

    function refreshSelection() {
        const ids = projectModel.selectedProjectedNoteIds
        const lookup = {}
        for (let index = 0; index < ids.length; ++index)
            lookup[ids[index]] = true
        const result = []
        for (let index = 0; index < notes.length; ++index) {
            if (lookup[notes[index].noteId])
                result.push(notes[index])
        }
        const pitches = {}
        for (let index = 0; index < result.length; ++index)
            pitches[result[index].pitch] = true
        selectedLookup = lookup
        selectedNotes = result
        selectedPitches = pitches
        if (ids.length === 0)
            selectionAnchorId = ""
        else if (!lookup[selectionAnchorId])
            selectionAnchorId = projectModel.selectedProjectedNoteId
    }

    function isNoteSelected(noteId) {
        return selectedLookup[noteId] === true
    }

    function selectedNoteIds() {
        return selectedNotes.map(note => note.noteId)
    }

    function selectNotes(ids, primaryId) {
        projectModel.setProjectedNoteSelection(ids, primaryId)
    }

    //! A click on a note or its stem: alone it selects just that note, Ctrl adds or removes it,
    //! Shift selects every note from the anchor to it (in time order).
    function clickSelect(noteId, modifiers) {
        if ((modifiers & Qt.ShiftModifier) && findNote(selectionAnchorId, timeOrderedNotes)) {
            let from = -1
            let to = -1
            for (let index = 0; index < timeOrderedNotes.length; ++index) {
                if (timeOrderedNotes[index].noteId === selectionAnchorId)
                    from = index
                if (timeOrderedNotes[index].noteId === noteId)
                    to = index
            }
            const ids = []
            for (let index = Math.min(from, to); index <= Math.max(from, to); ++index)
                ids.push(timeOrderedNotes[index].noteId)
            const anchor = selectionAnchorId
            selectNotes(ids, noteId)
            selectionAnchorId = anchor
            return
        }
        if (modifiers & Qt.ControlModifier) {
            const ids = selectedNoteIds()
            const at = ids.indexOf(noteId)
            if (at >= 0)
                ids.splice(at, 1)
            else
                ids.push(noteId)
            const primary = at >= 0 ? (ids.length > 0 ? ids[ids.length - 1] : "") : noteId
            selectNotes(ids, primary)
            selectionAnchorId = primary
            return
        }
        selectNotes([noteId], noteId)
        selectionAnchorId = noteId
    }

    //! Notes touching a rectangle of the note grid (content coordinates)
    function selectInRect(x0, y0, x1, y1, additive) {
        const fromTick = Math.min(x0, x1) / pixelsPerTick
        const toTick = Math.max(x0, x1) / pixelsPerTick
        const top = Math.min(y0, y1)
        const bottom = Math.max(y0, y1)
        const hits = []
        for (let index = 0; index < timeOrderedNotes.length; ++index) {
            const note = timeOrderedNotes[index]
            const noteTop = (highestPitch - note.pitch) * noteHeight
            if (note.startTick + note.durationTicks >= fromTick && note.startTick <= toTick
                    && noteTop + noteHeight >= top && noteTop <= bottom)
                hits.push(note.noteId)
        }
        applyAreaSelection(hits, additive)
    }

    //! Notes whose stem (start) lies in a tick range, for the control lane
    function selectInTickRange(fromTick, toTick, additive) {
        const hits = []
        for (let index = 0; index < timeOrderedNotes.length; ++index) {
            const tick = timeOrderedNotes[index].startTick
            if (tick >= fromTick && tick <= toTick)
                hits.push(timeOrderedNotes[index].noteId)
        }
        applyAreaSelection(hits, additive)
    }

    function applyAreaSelection(hits, additive) {
        const ids = additive ? selectedNoteIds() : []
        for (let index = 0; index < hits.length; ++index) {
            if (ids.indexOf(hits[index]) < 0)
                ids.push(hits[index])
        }
        selectNotes(ids, hits.length > 0 ? hits[hits.length - 1] : projectModel.selectedProjectedNoteId)
        if (hits.length > 0)
            selectionAnchorId = hits[0]
    }

    function cancelDrags() {
        noteDragActive = false
        noteDragTicks = 0
        noteDragPitch = 0
        laneDragActive = false
        laneDragDelta = 0
    }

    //! Performance mode: every selected note keeps its own offset and moves by `deltaTicks`
    function commitNoteMove(deltaTicks) {
        const offsets = {}
        for (let index = 0; index < selectedNotes.length; ++index) {
            const note = selectedNotes[index]
            offsets[note.noteId] = note.startTick + deltaTicks - note.scoreTick
        }
        if (!projectModel.setPerformanceStartOffsets(offsets))
            showFeedback(qsTrc("appshell", "The notes could not be moved."), true)
    }

    //! Notes mode: the selected notes move in the score itself, as one undo step
    function commitScoreMove(deltaTicks, deltaPitch) {
        const moves = []
        for (let index = 0; index < selectedNotes.length; ++index) {
            const note = selectedNotes[index]
            moves.push({ noteId: note.noteId, tick: Math.max(0, note.scoreTick + deltaTicks),
                         pitch: Math.max(0, Math.min(127, note.pitch + deltaPitch)) })
        }
        const error = projectModel.moveScoreNotes(moves)
        if (error.length > 0)
            showFeedback(error, true)
    }

    function selectedHasTimingOverride() {
        for (let index = 0; index < selectedNotes.length; ++index) {
            const note = selectedNotes[index]
            if (note.startOffsetTicks !== 0 || note.durationTicks !== note.notatedDurationTicks)
                return true
        }
        return false
    }

    function resetSelectedPosition() {
        const count = selectedNotes.length
        if (count === 0)
            return
        if (!projectModel.resetPerformanceTiming(selectedNoteIds()))
            showFeedback(qsTrc("appshell", "The notes could not be reset."), true)
        else
            showFeedback(count === 1 ? qsTrc("appshell", "The note is back at its written position")
                                     : qsTrc("appshell", "%1 notes are back at their written position").arg(count), false)
    }

    function adjustSelectedVelocity(delta) {
        if (selectedNotes.length === 0)
            return
        const velocities = {}
        for (let index = 0; index < selectedNotes.length; ++index) {
            const note = selectedNotes[index]
            velocities[note.noteId] = Math.max(1, Math.min(127, note.velocity + delta))
        }
        if (!projectModel.setPerformanceVelocities(velocities))
            showFeedback(qsTrc("appshell", "Velocity could not be changed."), true)
    }

    function controlSpec(value) {
        for (let index = 0; index < controlLaneModel.length; ++index) {
            if (controlLaneModel[index].value === value)
                return controlLaneModel[index]
        }
        return controlLaneModel[1]
    }

    function currentControlSpec() {
        return controlSpec(controlLaneParameter)
    }

    function midiCategoryTitle(category) {
        switch (category) {
        case "bankAndProgram": return qsTrc("appshell", "Bank and program")
        case "performance": return qsTrc("appshell", "Common CC")
        case "generalPurpose": return qsTrc("appshell", "Effects and general purpose")
        case "pedalAndSwitch": return qsTrc("appshell", "Pedals and switches")
        case "soundController": return qsTrc("appshell", "Sound controllers")
        case "effects": return qsTrc("appshell", "Effects and general purpose")
        case "dataEntry":
        case "parameterSelection": return qsTrc("appshell", "RPN/NRPN")
        case "channelMode": return qsTrc("appshell", "Channel mode")
        default: return qsTrc("appshell", "Undefined and reserved CC")
        }
    }

    function loadMidiControllerCatalog() {
        const source = projectModel.midiControllerCatalog()
        const specs = []
        for (let index = 0; index < source.length; ++index) {
            const cc = source[index]
            const dangerous = Boolean(cc.dangerous)
            const resolution = Number(cc.resolution) === 14
                             ? qsTrc("appshell", "14-bit pair")
                             : qsTrc("appshell", "7-bit")
            specs.push({
                text: dangerous
                      ? qsTrc("appshell", "CC%1 · %2 · %3 · protected")
                        .arg(cc.number).arg(cc.name).arg(resolution)
                      : qsTrc("appshell", "CC%1 · %2 · %3 · %4")
                        .arg(cc.number).arg(cc.name).arg(resolution).arg(cc.unit),
                value: cc.stableId,
                // Generic CC lanes are editable and persisted even before a
                // playback backend can consume them. Stateful RPN/NRPN and
                // channel-mode commands remain protected until they have a
                // discrete-event editor (they must never behave like curves).
                enabled: !dangerous,
                channel: true,
                bipolar: false,
                category: cc.category,
                behavior: cc.behavior,
                playable: false,
                reason: dangerous
                        ? qsTrc("appshell", "This stateful or channel-mode controller is preserved, but it needs a protected discrete-event editor before it can be changed safely.")
                        : qsTrc("appshell", "This CC lane can be edited and saved. Generic CC automation playback is not connected yet, so changing it will not alter the sound.")
            })
        }
        midiControllerSpecs = specs
    }

    function buildControlLaneMenu() {
        const order = [
            qsTrc("appshell", "Note properties"),
            qsTrc("appshell", "Per-note expression/MPE"),
            qsTrc("appshell", "Channel voice"),
            qsTrc("appshell", "Common CC"),
            qsTrc("appshell", "Pedals and switches"),
            qsTrc("appshell", "Sound controllers"),
            qsTrc("appshell", "Effects and general purpose"),
            qsTrc("appshell", "RPN/NRPN"),
            qsTrc("appshell", "Channel mode"),
            qsTrc("appshell", "Undefined and reserved CC")
        ]
        const grouped = {}
        for (let index = 0; index < order.length; ++index)
            grouped[order[index]] = []

        for (let index = 0; index < controlLaneModel.length; ++index) {
            const spec = controlLaneModel[index]
            let group = spec.category ? midiCategoryTitle(spec.category)
                                      : (spec.channel ? qsTrc("appshell", "Channel voice")
                                                      : (spec.value === "noteVelocity"
                                                         ? qsTrc("appshell", "Note properties")
                                                         : qsTrc("appshell", "Per-note expression/MPE")))
            grouped[group].push({
                id: "lane:" + spec.value,
                title: spec.text,
                enabled: spec.enabled,
                checkable: true,
                checked: spec.value === controlLaneParameter,
                subitems: [],
                includeInFilteredLists: true,
                isFilterCategory: false
            })
        }

        const menu = []
        for (let index = 0; index < order.length; ++index) {
            const title = order[index]
            if (grouped[title].length === 0)
                continue
            menu.push({
                id: "lane-category:" + index,
                title: title,
                enabled: true,
                checkable: false,
                subitems: grouped[title],
                includeInFilteredLists: false,
                isFilterCategory: true
            })
        }
        return menu
    }

    function refreshChannelAutomation() {
        const spec = currentControlSpec()
        channelAutomationPoints = spec.channel && startTrack >= 0
                                  ? projectModel.channelAutomationPoints(tracksModel.selectedPartId,
                                                                         tracksModel.selectedInstrumentId,
                                                                         controlLaneParameter)
                                  : []
    }

    function normalizedNoteControlValue(note) {
        if (controlLaneParameter === "noteVelocity")
            return (Number(note.velocity) - 1) / 126
        if (controlLaneParameter === "notePitch")
            return (Number(note.pitchOffsetCents) + 1200) / 2400
        return 0.5
    }

    function displayControlValue(normalized) {
        const value = Math.max(0, Math.min(1, normalized))
        if (controlLaneParameter === "noteVelocity")
            return String(Math.round(1 + value * 126))
        if (controlLaneParameter === "notePitch") {
            const cents = Math.round((value * 2 - 1) * 1200)
            return (cents > 0 ? "+" : "") + cents + " cents"
        }
        if (controlLaneParameter === "channelPitch") {
            const cents = Math.round((value * 2 - 1) * 200)
            return (cents > 0 ? "+" : "") + cents + " cents"
        }
        if (controlLaneParameter === "channelPan") {
            const pan = Math.round(value * 200 - 100)
            return pan === 0 ? qsTrc("appshell", "Center")
                             : (pan < 0 ? qsTrc("appshell", "%1 left").arg(-pan)
                                        : qsTrc("appshell", "%1 right").arg(pan))
        }
        if (controlLaneParameter === "channelVolume") {
            const db = -60 + value * 72
            return (db > 0 ? "+" : "") + db.toFixed(1) + " dB"
        }
        if (controlLaneParameter.indexOf("cc:") === 0) {
            const spec = currentControlSpec()
            if (spec.behavior === "switch")
                return value < 0.5 ? qsTrc("appshell", "Off") : qsTrc("appshell", "On")
            return String(Math.round(value * 127))
        }
        return ""
    }

    //! Every selected note changes by the same normalized amount (each clamped to the range)
    function commitSelectedControlDelta(delta) {
        const values = {}
        for (let index = 0; index < selectedNotes.length; ++index) {
            const note = selectedNotes[index]
            const value = Math.max(0, Math.min(1, normalizedNoteControlValue(note) + delta))
            values[note.noteId] = controlLaneParameter === "noteVelocity" ? Math.round(1 + value * 126)
                                                                          : Math.round((value * 2 - 1) * 1200)
        }
        const ok = controlLaneParameter === "noteVelocity" ? projectModel.setPerformanceVelocities(values)
                 : controlLaneParameter === "notePitch" ? projectModel.setPerformancePitchOffsets(values) : false
        if (!ok)
            showFeedback(qsTrc("appshell", "The note control could not be changed."), true)
    }

    function resetNoteControl(note) {
        if (controlLaneParameter === "noteVelocity")
            projectModel.resetPerformanceVelocity(note.noteId)
        else if (controlLaneParameter === "notePitch")
            projectModel.resetPerformancePitchOffset(note.noteId)
    }

    function resetSelectedControl() {
        const ids = selectedNoteIds()
        if (ids.length === 0)
            return
        if (controlLaneParameter === "noteVelocity")
            projectModel.resetPerformanceVelocities(ids)
        else if (controlLaneParameter === "notePitch")
            projectModel.resetPerformancePitchOffsets(ids)
    }

    function selectedControlHasOverride() {
        for (let index = 0; index < selectedNotes.length; ++index) {
            const note = selectedNotes[index]
            if ((controlLaneParameter === "noteVelocity" && note.hasVelocityOverride)
                    || (controlLaneParameter === "notePitch" && note.hasPitchOffsetOverride))
                return true
        }
        return false
    }

    function selectedControlText() {
        if (selectedNotes.length === 0)
            return qsTrc("appshell", "Drag a stem to edit")
        if (selectedNotes.length === 1)
            return qsTrc("appshell", "Selected: %1").arg(displayControlValue(normalizedNoteControlValue(selectedNotes[0])))
        let lowest = 1
        let highest = 0
        for (let index = 0; index < selectedNotes.length; ++index) {
            const value = normalizedNoteControlValue(selectedNotes[index])
            lowest = Math.min(lowest, value)
            highest = Math.max(highest, value)
        }
        const range = displayControlValue(lowest) === displayControlValue(highest)
                      ? displayControlValue(lowest) : displayControlValue(lowest) + "–" + displayControlValue(highest)
        return qsTrc("appshell", "%1 selected: %2").arg(selectedNotes.length).arg(range)
    }

    function adjustSelectedLaneControl(direction) {
        if (selectedNotes.length === 0)
            return
        if (controlLaneParameter === "noteVelocity") {
            adjustSelectedVelocity(direction)
        } else if (controlLaneParameter === "notePitch") {
            const cents = {}
            for (let index = 0; index < selectedNotes.length; ++index) {
                const note = selectedNotes[index]
                cents[note.noteId] = Math.max(-1200, Math.min(1200, note.pitchOffsetCents + direction * 10))
            }
            projectModel.setPerformancePitchOffsets(cents)
        }
    }

    //! Only notes near the viewport get interactive delegates.
    function updateWindow(force) {
        if (pixelsPerTick <= 0 || editor.width <= 0)
            return
        const viewStart = editor.contentX / pixelsPerTick
        const viewEnd = (editor.contentX + editor.width) / pixelsPerTick
        if (!force && viewStart >= windowStartTick && viewEnd <= windowEndTick)
            return
        const span = Math.max(1, viewEnd - viewStart)
        windowStartTick = viewStart - span
        windowEndTick = viewEnd + span
        const result = []
        for (let index = 0; index < notes.length; ++index) {
            const note = notes[index]
            const first = Math.min(note.startTick, note.scoreTick)
            const last = Math.max(note.startTick + note.durationTicks, note.scoreTick + note.notatedDurationTicks)
            if (last >= windowStartTick && first <= windowEndTick)
                result.push(note)
        }
        visibleNotes = result
    }

    function fitNotes() {
        autoFitPitch = true
        if (notes.length === 0) {
            lowestPitch = 36
            highestPitch = 84
            editor.contentY = Math.max(0, (highestPitch - 72) * noteHeight)
            return
        }
        let minimumPitch = 127
        let maximumPitch = 0
        for (let index = 0; index < notes.length; ++index) {
            const pitch = Math.max(0, Math.min(127, Number(notes[index].pitch)))
            minimumPitch = Math.min(minimumPitch, pitch)
            maximumPitch = Math.max(maximumPitch, pitch)
        }
        let lower = Math.max(0, minimumPitch - pitchPadding)
        let upper = Math.min(127, maximumPitch + pitchPadding)
        const missing = Math.max(0, minimumPitchSpan - (upper - lower + 1))
        lower -= Math.floor(missing / 2)
        upper += Math.ceil(missing / 2)
        if (lower < 0) {
            upper = Math.min(127, upper - lower)
            lower = 0
        }
        if (upper > 127) {
            lower = Math.max(0, lower - (upper - 127))
            upper = 127
        }
        lowestPitch = lower
        highestPitch = upper
        const center = (minimumPitch + maximumPitch) / 2
        const targetY = (highestPitch - center + 0.5) * noteHeight - editor.height * 0.5
        editor.contentY = Math.max(0, Math.min(Math.max(0, editor.contentHeight - editor.height), targetY))
    }

    //! Keeps the pitch at `viewY` (the view's centre when not given) in place
    function zoomVertical(factor, viewY) {
        const anchorY = viewY === undefined ? editor.height / 2 : viewY
        const pitchAtAnchor = (editor.contentY + anchorY) / noteHeight
        const newHeight = Math.max(minimumNoteHeight, Math.min(maximumNoteHeight, Math.round(noteHeight * factor)))
        if (newHeight === noteHeight)
            return
        autoFitPitch = false
        noteHeight = newHeight
        editor.contentY = Math.max(0, Math.min(Math.max(0, editor.contentHeight - editor.height),
                                               pitchAtAnchor * noteHeight - anchorY))
    }

    function noteName(pitch) {
        return ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"][pitch % 12] + Math.floor(pitch / 12 - 1)
    }

    function showFeedback(text, isError) {
        feedbackText = text
        feedbackIsError = isError
        feedbackTimer.restart()
    }

    function targetTrackForPitch(pitch) {
        return staffCount > 1 && pitch < splitPitch ? startTrack + 4 : startTrack
    }

    function pitchAtY(y) {
        return Math.max(lowestPitch, Math.min(highestPitch, highestPitch - Math.floor(y / noteHeight)))
    }

    Timer {
        // Clears transient editing feedback; it never drives playback state.
        id: feedbackTimer
        interval: 6000
        onTriggered: root.feedbackText = ""
    }

    Component.onCompleted: {
        loadMidiControllerCatalog()
        refreshNotes()
        refreshChannelAutomation()
    }
    onStartTrackChanged: {
        autoFitPitch = true
        refreshNotes()
        refreshChannelAutomation()
    }
    onEndTrackChanged: refreshNotes()
    onControlLaneParameterChanged: refreshChannelAutomation()
    onPixelsPerTickChanged: updateWindow(true)
    onPerformanceModeChanged: if (performanceMode) drawTool = false
    onTimelineContentXChanged: {
        if (Math.abs(editor.contentX - timelineContentX) > 0.5)
            editor.contentX = timelineContentX
    }

    Connections {
        target: root.projectModel
        function onScoreProjectionChanged() {
            // One edit can emit several notifications; project once.
            Qt.callLater(root.refreshNotes)
        }
        function onChannelAutomationChanged() {
            Qt.callLater(root.refreshChannelAutomation)
        }
        function onProjectedSelectionChanged() {
            root.refreshSelection()
        }
    }

    Connections {
        target: root.tracksModel
        function onSelectionChanged() {
            Qt.callLater(root.refreshChannelAutomation)
            Qt.callLater(root.refreshNotes)
        }
    }

    Keys.onEscapePressed: {
        root.cancelToken++
        root.cancelDrags()
        if (root.drawTool)
            root.drawTool = false
    }

    // In Notes mode the application's own editing shortcuts (arrows, Delete,
    // duration keys) act on the synchronized score selection. In Performance
    // mode Delete must not remove the written note, so it resets the note's
    // performance instead.
    Keys.onShortcutOverride: function(event) {
        if (root.performanceMode && (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)) {
            event.accepted = true
        }
    }
    Keys.onPressed: function(event) {
        if (root.performanceMode && (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)) {
            if (root.selectedNotes.length > 0) {
                root.projectModel.resetPerformances(root.selectedNoteIds())
            }
            event.accepted = true
        }
    }

    NavigationPanel {
        id: navPanel
        name: "DawPianoRoll"
        section: root.navigationSection
        order: root.navigationOrderStart
        direction: NavigationPanel.Horizontal
        accessible.name: qsTrc("appshell", "Piano roll")
    }

    NavigationPanel {
        id: notesNavPanel
        name: "DawPianoRollNotes"
        section: root.navigationSection
        order: root.navigationOrderStart + 1
        direction: NavigationPanel.Both
        accessible.name: qsTrc("appshell", "Piano roll notes")
    }

    NavigationPanel {
        id: controlLaneNavPanel
        name: "DawControlLane"
        section: root.navigationSection
        order: root.navigationOrderStart + 2
        direction: NavigationPanel.Both
        accessible.name: qsTrc("appshell", "%1 control lane").arg(root.currentControlSpec().text)
    }

    NavigationControl {
        id: notesNavCtrl
        name: "DawPianoRollNotes"
        enabled: root.visible && root.notes.length > 0
        panel: notesNavPanel
        row: 0
        column: 0
        accessible.role: MUAccessible.Button
        accessible.name: root.selectedNote
                         ? qsTrc("appshell", "Selected note %1").arg(root.noteName(root.selectedNote.pitch))
                         : qsTrc("appshell", "Select the first piano roll note")
        accessible.visualItem: editor
        onActiveChanged: function(active) {
            if (active)
                root.forceActiveFocus()
        }
        onTriggered: {
            if (!root.selectedNote)
                root.selectRelativeNote(1)
        }
        onNavigationEvent: function(event) {
            if (event.type === NavigationEvent.Left) {
                root.selectRelativeNote(-1)
                event.accepted = true
            } else if (event.type === NavigationEvent.Right) {
                root.selectRelativeNote(1)
                event.accepted = true
            }
        }
    }

    NavigationControl {
        id: controlLaneNavCtrl
        name: "DawControlLane"
        enabled: root.visible && root.currentControlSpec().enabled
        panel: controlLaneNavPanel
        row: 0
        column: 0
        accessible.role: MUAccessible.Button
        accessible.name: root.currentControlSpec().text
        accessible.visualItem: controlLane
        onActiveChanged: function(active) {
            if (active)
                root.forceActiveFocus()
        }
        onTriggered: {
            if (!root.selectedNote)
                root.selectRelativeNote(1)
        }
        onNavigationEvent: function(event) {
            if (event.type === NavigationEvent.Left) {
                root.selectRelativeNote(-1)
                event.accepted = true
            } else if (event.type === NavigationEvent.Right) {
                root.selectRelativeNote(1)
                event.accepted = true
            } else if (event.type === NavigationEvent.Up) {
                root.adjustSelectedLaneControl(1)
                event.accepted = true
            } else if (event.type === NavigationEvent.Down) {
                root.adjustSelectedLaneControl(-1)
                event.accepted = true
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header: what is being edited and how.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: dawTheme.headerHeight
            color: dawTheme.panel

            SeparatorLine { anchors.bottom: parent.bottom; orientation: Qt.Horizontal }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                Row {
                    spacing: 3
                    Repeater {
                        model: root.startTrack >= 0 ? root.selectedTracks : [null]
                        delegate: Rectangle {
                            required property var modelData
                            width: 10
                            height: 10
                            radius: 5
                            color: modelData ? dawTheme.trackColor(modelData.colorIndex) : dawTheme.line
                            border.width: modelData && modelData.primary && root.multiTrack ? 2 : 0
                            border.color: ui.theme.fontPrimaryColor
                        }
                    }
                }

                StyledTextLabel {
                    Layout.maximumWidth: 220
                    text: root.startTrack < 0 ? qsTrc("appshell", "No track selected")
                          : root.multiTrack ? qsTrc("appshell", "%1 tracks · drawing into %2").arg(root.selectedTracks.length).arg(root.tracksModel.selectedName)
                                            : root.tracksModel.selectedName
                    font: ui.theme.bodyBoldFont
                    displayTruncatedTextOnHover: true
                }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: dawTheme.line }

                RadioButtonGroup {
                    id: modeGroup
                    Layout.preferredWidth: 210
                    Layout.preferredHeight: 28
                    model: [
                        { text: qsTrc("appshell", "Notes"), value: DawProjectModel.Notation,
                          hint: qsTrc("appshell", "Edits change the score") },
                        { text: qsTrc("appshell", "Performance"), value: DawProjectModel.Performance,
                          hint: qsTrc("appshell", "Change how notes are played; the score stays as written") }
                    ]
                    delegate: FlatRadioButton {
                        required property var modelData
                        required property int index
                        ButtonGroup.group: modeGroup.radioButtonGroup
                        text: modelData.text
                        toolTipTitle: modelData.text
                        toolTipDescription: modelData.hint
                        checked: root.projectModel.editMode === modelData.value
                        navigation.panel: navPanel
                        navigation.column: index
                        onToggled: root.projectModel.editMode = modelData.value
                    }
                }

                RadioButtonGroup {
                    id: toolGroup
                    visible: !root.performanceMode
                    Layout.preferredWidth: 150
                    Layout.preferredHeight: 28
                    model: [
                        { text: qsTrc("appshell", "Select"), draw: false,
                          hint: qsTrc("appshell", "Click notes to select them (Esc)") },
                        { text: qsTrc("appshell", "Draw"), draw: true,
                          hint: qsTrc("appshell", "Click the grid to add a note to the score") }
                    ]
                    delegate: FlatRadioButton {
                        required property var modelData
                        required property int index
                        ButtonGroup.group: toolGroup.radioButtonGroup
                        text: modelData.text
                        toolTipTitle: modelData.text
                        toolTipDescription: modelData.hint
                        checked: root.drawTool === modelData.draw
                        enabled: !modelData.draw || root.drawBlockedReason.length === 0
                        navigation.panel: navPanel
                        navigation.column: 2 + index
                        onToggled: root.drawTool = modelData.draw
                    }
                }

                StyledDropdown {
                    visible: root.drawActive
                    Layout.preferredWidth: 110
                    model: [
                        { text: qsTrc("appshell", "Whole"), value: 1920 },
                        { text: qsTrc("appshell", "Half"), value: 960 },
                        { text: qsTrc("appshell", "Quarter"), value: 480 },
                        { text: qsTrc("appshell", "Eighth"), value: 240 },
                        { text: qsTrc("appshell", "16th"), value: 120 }
                    ]
                    currentIndex: indexOfValue(root.newNoteTicks)
                    navigation.panel: navPanel
                    navigation.column: 4
                    navigation.accessible.name: qsTrc("appshell", "New note length")
                    onActivated: function(index, value) { root.newNoteTicks = value }
                }

                Item { Layout.fillWidth: true }

                FlatButton {
                    visible: root.selectedNotes.length > 0
                    enabled: root.selectedHasTimingOverride()
                    text: qsTrc("appshell", "Reset position")
                    toolTipTitle: qsTrc("appshell", "Reset position")
                    toolTipDescription: qsTrc("appshell", "Move the selected notes back to their written position and length")
                    navigation.panel: navPanel
                    navigation.column: 5
                    navigation.accessible.name: qsTrc("appshell", "Reset the position of the selected notes")
                    onClicked: root.resetSelectedPosition()
                }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: dawTheme.line }

                StyledTextLabel {
                    text: "↔"
                    color: ui.theme.fontSecondaryColor
                }
                FlatButton {
                    icon: IconCode.ZOOM_OUT
                    enabled: root.canZoomOut
                    toolTipTitle: qsTrc("appshell", "Zoom out horizontally")
                    toolTipDescription: qsTrc("appshell", "Ctrl + mouse wheel")
                    navigation.panel: navPanel
                    navigation.column: 7
                    navigation.accessible.name: toolTipTitle
                    onClicked: root.zoomRequested(0.8, editor.width / 2)
                }
                FlatButton {
                    icon: IconCode.ZOOM_IN
                    enabled: root.canZoomIn
                    toolTipTitle: qsTrc("appshell", "Zoom in horizontally")
                    toolTipDescription: qsTrc("appshell", "Ctrl + mouse wheel")
                    navigation.panel: navPanel
                    navigation.column: 8
                    navigation.accessible.name: toolTipTitle
                    onClicked: root.zoomRequested(1.25, editor.width / 2)
                }

                StyledTextLabel {
                    text: "↕"
                    color: ui.theme.fontSecondaryColor
                }
                FlatButton {
                    icon: IconCode.ZOOM_OUT
                    enabled: root.noteHeight > root.minimumNoteHeight
                    toolTipTitle: qsTrc("appshell", "Zoom out vertically")
                    toolTipDescription: qsTrc("appshell", "Ctrl + Shift + mouse wheel, or Ctrl + wheel over the keyboard")
                    navigation.panel: navPanel
                    navigation.column: 9
                    navigation.accessible.name: toolTipTitle
                    onClicked: root.zoomVertical(0.8)
                }
                FlatButton {
                    icon: IconCode.ZOOM_IN
                    enabled: root.noteHeight < root.maximumNoteHeight
                    toolTipTitle: qsTrc("appshell", "Zoom in vertically")
                    toolTipDescription: qsTrc("appshell", "Ctrl + Shift + mouse wheel, or Ctrl + wheel over the keyboard")
                    navigation.panel: navPanel
                    navigation.column: 10
                    navigation.accessible.name: toolTipTitle
                    onClicked: root.zoomVertical(1.25)
                }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: dawTheme.line }

                FlatButton {
                    icon: IconCode.SPLIT_OUT_ARROWS
                    text: root.width >= 900 ? qsTrc("appshell", "Fit") : ""
                    orientation: Qt.Horizontal
                    accentButton: root.autoFitPitch
                    toolTipTitle: qsTrc("appshell", "Fit notes")
                    toolTipDescription: qsTrc("appshell", "Show the pitch range used by this part")
                    navigation.panel: navPanel
                    navigation.column: 6
                    navigation.accessible.name: qsTrc("appshell", "Fit notes to the available height")
                    onClicked: root.fitNotes()
                }

                FlatButton {
                    icon: root.maximized ? IconCode.APP_UNMAXIMIZE : IconCode.APP_MAXIMIZE
                    toolTipTitle: root.maximized ? qsTrc("appshell", "Restore the panels") : qsTrc("appshell", "Maximize the piano roll")
                    navigation.panel: navPanel
                    navigation.column: 11
                    navigation.accessible.name: toolTipTitle
                    onClicked: root.maximizeRequested()
                }
            }
        }

        // Body: keyboard, ruler, grid and notes share one tick and pitch transform.
        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            NavigationFocusBorder {
                anchors.fill: editor
                navigationCtrl: notesNavCtrl
                drawOutsideParent: false
                z: 50
            }

            // Middle button: drag the view (anywhere in the piano roll) in both directions.
            DragHandler {
                target: null
                acceptedButtons: Qt.MiddleButton
                cursorShape: Qt.ClosedHandCursor
                property real startX: 0
                property real startY: 0
                onActiveChanged: {
                    if (active) {
                        startX = editor.contentX
                        startY = editor.contentY
                        root.autoFitPitch = false
                    }
                }
                onTranslationChanged: {
                    if (!active)
                        return
                    editor.contentX = Math.max(0, Math.min(Math.max(0, editor.contentWidth - editor.width), startX - translation.x))
                    editor.contentY = Math.max(0, Math.min(Math.max(0, editor.contentHeight - editor.height), startY - translation.y))
                }
            }

            Rectangle {
                width: root.keyWidth
                height: dawTheme.rulerHeight
                color: dawTheme.panel
            }

            Item {
                id: rulerArea
                x: root.keyWidth
                width: parent.width - x
                height: dawTheme.rulerHeight

                Rectangle { anchors.fill: parent; color: dawTheme.panel }

                DawTimeGrid {
                    anchors.fill: parent
                    ruler: true
                    measures: root.transport.measures
                    pixelsPerTick: root.pixelsPerTick
                    viewX: editor.contentX
                }

                Rectangle {
                    x: root.transport.tick * root.pixelsPerTick - editor.contentX - 5
                    y: parent.height - 10
                    width: 10
                    height: 10
                    rotation: 45
                    color: dawTheme.playhead
                    visible: x > -10 && x < parent.width
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    function seek(mouseX) {
                        root.transport.seekToTick(root.transport.snapTick((mouseX + editor.contentX) / root.pixelsPerTick, root.snapTicks, true))
                    }
                    onPressed: mouse => seek(mouse.x)
                    onPositionChanged: mouse => { if (pressed) seek(mouse.x) }
                }

                SeparatorLine { anchors.bottom: parent.bottom; orientation: Qt.Horizontal }
            }

            Flickable {
                id: keyboard
                y: dawTheme.rulerHeight
                width: root.keyWidth
                height: parent.height - y - root.controlLaneExtent
                clip: true
                interactive: false
                contentHeight: editor.contentHeight
                contentY: editor.contentY

                // Ctrl + wheel over the keyboard zooms vertically.
                WheelHandler {
                    acceptedModifiers: Qt.ControlModifier
                    onWheel: event => {
                        const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                        if (delta !== 0)
                            root.zoomVertical(delta > 0 ? 1.25 : 0.8, point.position.y - keyboard.contentY)
                        event.accepted = true
                    }
                }

                //! A piano on its side: the backs of the keys on the left, the fronts against the grid.
                //! One row per pitch keeps the keys aligned with the notes; the white keys are one
                //! continuous surface split where two white keys meet (E|F, B|C) and, beside a black
                //! key, halfway along it, so the black keys read as short keys lying on the white ones.
                Column {
                    width: keyboard.width
                    Repeater {
                        model: root.highestPitch - root.lowestPitch + 1
                        delegate: Item {
                            id: keyRow
                            required property int index
                            readonly property int pitch: root.highestPitch - index
                            readonly property int pitchClass: pitch % 12
                            readonly property bool blackKey: [1, 3, 6, 8, 10].indexOf(pitchClass) >= 0
                            readonly property bool highlighted: root.selectedPitches[pitch] === true
                            readonly property real keysX: width - root.keyLength
                            readonly property real blackLength: Math.round(root.keyLength * 0.6)
                            width: keyboard.width
                            height: root.noteHeight

                            Rectangle {
                                width: keyRow.keysX
                                height: parent.height
                                color: dawTheme.panel
                            }

                            StyledTextLabel {
                                anchors.right: parent.right
                                anchors.rightMargin: root.keyLength + 6
                                anchors.verticalCenter: parent.verticalCenter
                                visible: keyRow.pitchClass === 0 && root.noteHeight >= 9
                                text: root.noteName(keyRow.pitch)
                                color: ui.theme.fontSecondaryColor
                                font.pixelSize: 10
                            }

                            // White key surface (also under the black keys)
                            Rectangle {
                                x: keyRow.keysX
                                width: root.keyLength
                                height: parent.height
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: Qt.darker(dawTheme.pianoWhiteKey, 1.12) }
                                    GradientStop { position: 0.12; color: dawTheme.pianoWhiteKey }
                                    GradientStop { position: 0.94; color: keyRow.highlighted && !keyRow.blackKey
                                                                          ? Qt.tint(dawTheme.pianoWhiteKey, Qt.rgba(dawTheme.brandAccent.r, dawTheme.brandAccent.g, dawTheme.brandAccent.b, 0.55))
                                                                          : dawTheme.pianoWhiteKey }
                                    GradientStop { position: 1.0; color: Qt.darker(dawTheme.pianoWhiteKey, 1.08) }
                                }
                            }

                            // Where two white keys meet at a row edge: F above E, C above B
                            Rectangle {
                                visible: keyRow.pitchClass === 5 || keyRow.pitchClass === 0
                                x: keyRow.keysX
                                y: parent.height - 1
                                width: root.keyLength
                                height: 1
                                color: Qt.darker(dawTheme.pianoWhiteKey, 1.45)
                            }

                            // In front of a black key, the edge between its two white neighbours
                            Rectangle {
                                visible: keyRow.blackKey
                                x: keyRow.keysX + keyRow.blackLength
                                y: Math.floor(parent.height / 2)
                                width: root.keyLength - keyRow.blackLength
                                height: 1
                                color: Qt.darker(dawTheme.pianoWhiteKey, 1.45)
                            }

                            // The black key: shorter, raised, with a lighter front edge
                            Rectangle {
                                visible: keyRow.blackKey
                                x: keyRow.keysX
                                y: 1
                                width: keyRow.blackLength
                                height: Math.max(2, parent.height - 2)
                                radius: 2
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0.0; color: Qt.darker(dawTheme.pianoBlackKey, 1.3) }
                                    GradientStop { position: 0.8; color: keyRow.highlighted ? dawTheme.brandAccent : dawTheme.pianoBlackKey }
                                    GradientStop { position: 0.92; color: Qt.lighter(keyRow.highlighted ? dawTheme.brandAccent : dawTheme.pianoBlackKey, 1.9) }
                                    GradientStop { position: 1.0; color: Qt.darker(dawTheme.pianoBlackKey, 1.1) }
                                }
                            }

                            // C labels on the keys themselves, when there is room
                            StyledTextLabel {
                                anchors.right: parent.right
                                anchors.rightMargin: 3
                                anchors.bottom: parent.bottom
                                visible: keyRow.pitchClass === 0 && root.noteHeight >= 12
                                text: root.noteName(keyRow.pitch)
                                color: dawTheme.pianoWhiteKeyText
                                font.pixelSize: 8
                            }
                        }
                    }
                }

                Rectangle {
                    x: keyboard.width - 1
                    width: 1
                    height: keyboard.contentHeight
                    color: dawTheme.line
                }
            }

            DawTimeGrid {
                x: root.keyWidth
                y: dawTheme.rulerHeight
                width: parent.width - x
                height: parent.height - y - root.controlLaneExtent
                measures: root.transport.measures
                pixelsPerTick: root.pixelsPerTick
                viewX: editor.contentX
                drawPitchRows: true
                viewY: editor.contentY
                rowHeight: root.noteHeight
                highestPitch: root.highestPitch
                lowestPitch: root.lowestPitch
            }

            Flickable {
                id: editor
                // Mouse buttons edit and select; the wheel and scroll bars scroll.
                acceptedButtons: Qt.NoButton
                x: root.keyWidth
                y: dawTheme.rulerHeight
                width: parent.width - x
                height: parent.height - y - root.controlLaneExtent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: Math.max(width, (root.transport.endTick + 1920) * root.pixelsPerTick)
                contentHeight: (root.highestPitch - root.lowestPitch + 1) * root.noteHeight
                ScrollBar.horizontal: StyledScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.vertical: StyledScrollBar { policy: ScrollBar.AsNeeded }

                onWidthChanged: root.updateWindow(true)
                onContentXChanged: {
                    root.updateWindow(false)
                    if (Math.abs(contentX - root.timelineContentX) > 0.5)
                        root.timelineScrollRequested(contentX)
                }
                onMovementStarted: if (movingVertically) root.autoFitPitch = false

                WheelHandler {
                    acceptedModifiers: Qt.ControlModifier
                    onWheel: event => {
                        const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                        if (delta !== 0)
                            root.zoomRequested(delta > 0 ? 1.25 : 0.8, point.position.x - editor.contentX)
                        event.accepted = true
                    }
                }
                WheelHandler {
                    acceptedModifiers: Qt.ControlModifier | Qt.ShiftModifier
                    onWheel: event => {
                        const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                        if (delta !== 0)
                            root.zoomVertical(delta > 0 ? 1.25 : 0.8, point.position.y - editor.contentY)
                        event.accepted = true
                    }
                }

                Item {
                    width: editor.contentWidth
                    height: editor.contentHeight

                    MouseArea {
                        id: gridArea
                        anchors.fill: parent
                        preventStealing: true
                        hoverEnabled: root.drawActive
                        cursorShape: root.drawActive ? Qt.CrossCursor : Qt.ArrowCursor
                        property int previewTick: 0
                        property int previewPitch: 60
                        //! Area selection: dragging on the empty grid (not while drawing)
                        property real pressX: 0
                        property real pressY: 0
                        property int pressModifiers: 0
                        property bool banding: false
                        property bool suppressClick: false

                        function updatePreview(mouse) {
                            previewTick = root.transport.snapTick(mouse.x / root.pixelsPerTick, root.snapTicks, false)
                            previewPitch = root.pitchAtY(mouse.y)
                        }

                        onPressed: mouse => {
                            root.forceActiveFocus()
                            pressX = mouse.x
                            pressY = mouse.y
                            pressModifiers = mouse.modifiers
                            banding = false
                            suppressClick = false
                        }
                        onPositionChanged: mouse => {
                            updatePreview(mouse)
                            if (pressed && !root.drawActive && !banding
                                    && (Math.abs(mouse.x - pressX) > 3 || Math.abs(mouse.y - pressY) > 3))
                                banding = true
                        }
                        onReleased: mouse => {
                            if (!banding)
                                return
                            root.selectInRect(pressX, pressY, mouse.x, mouse.y,
                                              (pressModifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0)
                            banding = false
                            suppressClick = true
                        }
                        onCanceled: banding = false
                        onClicked: mouse => {
                            if (suppressClick) {
                                suppressClick = false
                                return
                            }
                            if (!root.drawActive) {
                                if (!(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
                                    root.selectNotes([], "")
                                return
                            }
                            updatePreview(mouse)
                            // Keep the view still while drawing; Fit is available on demand.
                            root.autoFitPitch = false
                            const error = root.projectModel.addScoreNote(previewTick, root.newNoteTicks, previewPitch,
                                                                         root.targetTrackForPitch(previewPitch))
                            if (error.length > 0) {
                                root.showFeedback(error, true)
                            } else {
                                root.showFeedback(qsTrc("appshell", "Added %1 at %2")
                                                  .arg(root.noteName(previewPitch))
                                                  .arg(root.transport.positionTextForTick(previewTick)), false)
                            }
                        }
                    }

                    Rectangle {
                        visible: gridArea.banding
                        x: Math.min(gridArea.pressX, gridArea.mouseX)
                        y: Math.min(gridArea.pressY, gridArea.mouseY)
                        width: Math.abs(gridArea.mouseX - gridArea.pressX)
                        height: Math.abs(gridArea.mouseY - gridArea.pressY)
                        color: Qt.rgba(dawTheme.brandAccent.r, dawTheme.brandAccent.g, dawTheme.brandAccent.b, 0.15)
                        border.width: 1
                        border.color: dawTheme.brandAccent
                        z: 30
                    }

                    Rectangle {
                        visible: root.drawActive && gridArea.containsMouse
                        x: gridArea.previewTick * root.pixelsPerTick
                        y: (root.highestPitch - gridArea.previewPitch) * root.noteHeight + 1
                        width: Math.max(6, root.newNoteTicks * root.pixelsPerTick - 1)
                        height: root.noteHeight - 2
                        radius: 2
                        color: root.trackColor
                        opacity: 0.45
                        border.color: dawTheme.noteSelectedOutline
                        z: 2

                        StyledTextLabel {
                            anchors.left: parent.right
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.noteName(gridArea.previewPitch) + "  " + root.transport.positionTextForTick(gridArea.previewTick)
                            font.pixelSize: 10
                        }
                    }

                    Repeater {
                        model: root.visibleNotes
                        delegate: Item {
                            id: noteItem
                            required property var modelData
                            readonly property bool isSelected: root.isNoteSelected(modelData.noteId)
                            readonly property color noteColor: dawTheme.trackColor(modelData.colorIndex)
                            readonly property bool hasOffset: modelData.startOffsetTicks !== 0
                            //! A drag moves every selected note by the same amount
                            readonly property bool dragging: root.noteDragActive && isSelected
                            readonly property real dragTicks: dragging ? root.noteDragTicks : 0
                            readonly property int dragPitch: dragging ? root.noteDragPitch : 0
                            property real pressY: 0
                            property real pressX: 0
                            property bool dragOrigin: false
                            //! Pressing a note of a larger selection keeps the selection (to drag it all);
                            //! a click without a drag then selects just this note
                            property bool collapseOnRelease: false

                            x: modelData.startTick * root.pixelsPerTick + dragTicks * root.pixelsPerTick
                            y: (root.highestPitch - modelData.pitch - dragPitch) * root.noteHeight + 1
                            width: Math.max(4, modelData.durationTicks * root.pixelsPerTick - 1)
                            height: root.noteHeight - 2
                            z: isSelected ? 4 : 3

                            // Written (score) position shown as an anchor in Performance mode.
                            Rectangle {
                                visible: root.performanceMode && (noteItem.hasOffset || noteItem.dragging)
                                x: (noteItem.modelData.scoreTick - noteItem.modelData.startTick - noteItem.dragTicks) * root.pixelsPerTick
                                width: Math.max(4, noteItem.modelData.notatedDurationTicks * root.pixelsPerTick - 1)
                                height: parent.height
                                radius: 2
                                color: "transparent"
                                border.width: 1
                                border.color: noteItem.noteColor
                                opacity: 0.7
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: 2
                                color: noteItem.isSelected ? Qt.lighter(noteItem.noteColor, 1.3) : noteItem.noteColor
                                opacity: noteItem.dragging ? 0.8 : 1
                                border.width: noteItem.isSelected ? 2 : 1
                                border.color: noteItem.isSelected ? dawTheme.noteSelectedOutline : dawTheme.noteBorder
                            }

                            MouseArea {
                                anchors.fill: parent
                                // Keep the gesture even if the pointer leaves the note.
                                preventStealing: true
                                cursorShape: root.performanceMode ? Qt.SizeHorCursor : Qt.SizeAllCursor
                                onCanceled: {
                                    if (noteItem.dragOrigin)
                                        root.cancelDrags()
                                    noteItem.dragOrigin = false
                                    noteItem.collapseOnRelease = false
                                }
                                onPressed: mouse => {
                                    root.forceActiveFocus()
                                    const modifiers = mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)
                                    noteItem.collapseOnRelease = noteItem.isSelected && root.selectedNotes.length > 1 && !modifiers
                                    if (!noteItem.collapseOnRelease)
                                        root.clickSelect(noteItem.modelData.noteId, mouse.modifiers)
                                    // Scene coordinates: the note itself moves while it is dragged.
                                    noteItem.pressX = mapToItem(null, mouse.x, mouse.y).x
                                    noteItem.pressY = mapToItem(null, mouse.x, mouse.y).y
                                    noteItem.dragOrigin = true
                                    root.cancelDrags()
                                }
                                // Performance mode: along time only (the written note stays). Notes mode: time and
                                // pitch, and the score changes on release.
                                onPositionChanged: mouse => {
                                    if (!pressed || !noteItem.dragOrigin || !noteItem.isSelected)
                                        return
                                    const scenePoint = mapToItem(null, mouse.x, mouse.y)
                                    const sceneX = scenePoint.x
                                    const rawTicks = (sceneX - noteItem.pressX) / root.pixelsPerTick
                                    if (!root.noteDragActive && Math.abs(sceneX - noteItem.pressX) < 3
                                            && (root.performanceMode || Math.abs(scenePoint.y - noteItem.pressY) < 3))
                                        return
                                    root.noteDragActive = true
                                    if (!root.performanceMode)
                                        root.noteDragPitch = -Math.round((scenePoint.y - noteItem.pressY) / root.noteHeight)
                                    // Shift moves freely for fine micro-timing; the others follow this note.
                                    const target = (mouse.modifiers & Qt.ShiftModifier)
                                                   ? Math.round(noteItem.modelData.startTick + rawTicks)
                                                   : root.transport.snapTick(noteItem.modelData.startTick + rawTicks, root.snapTicks, true)
                                    root.noteDragTicks = target - noteItem.modelData.startTick
                                }
                                onReleased: {
                                    if (root.noteDragActive && root.performanceMode && root.noteDragTicks !== 0)
                                        root.commitNoteMove(root.noteDragTicks)
                                    else if (root.noteDragActive && !root.performanceMode && (root.noteDragTicks !== 0 || root.noteDragPitch !== 0))
                                        root.commitScoreMove(Math.round(root.noteDragTicks), root.noteDragPitch)
                                    else if (noteItem.collapseOnRelease)
                                        root.clickSelect(noteItem.modelData.noteId, 0)
                                    root.cancelDrags()
                                    noteItem.dragOrigin = false
                                    noteItem.collapseOnRelease = false
                                }
                                // With several tracks shown, a double-click keeps only this note's track.
                                onDoubleClicked: {
                                    const track = root.selectedTracks[noteItem.modelData.rangeIndex]
                                    if (root.multiTrack && track)
                                        root.tracksModel.selectTrack(track.row)
                                }
                            }

                            Rectangle {
                                visible: noteItem.dragOrigin && root.noteDragActive
                                y: -height - 4
                                width: dragLabel.implicitWidth + 12
                                height: 20
                                radius: 3
                                color: ui.theme.popupBackgroundColor
                                border.color: dawTheme.line
                                StyledTextLabel {
                                    id: dragLabel
                                    anchors.centerIn: parent
                                    font.pixelSize: 10
                                    text: root.performanceMode
                                          ? root.transport.positionTextForTick(noteItem.modelData.startTick + noteItem.dragTicks)
                                            + "   " + qsTrc("appshell", "offset %1").arg(noteItem.modelData.startTick + noteItem.dragTicks - noteItem.modelData.scoreTick)
                                          : root.noteName(noteItem.modelData.pitch + noteItem.dragPitch) + "   "
                                            + root.transport.positionTextForTick(noteItem.modelData.scoreTick + noteItem.dragTicks)
                                }
                            }
                        }
                    }

                    Rectangle {
                        x: root.transport.tick * root.pixelsPerTick
                        width: 2
                        height: parent.height
                        color: dawTheme.playhead
                        z: 20
                    }
                }
            }


            Item {
                id: controlLane
                x: 0
                y: parent.height - height
                width: parent.width
                height: root.controlLaneExtent
                clip: true

                Rectangle {
                    anchors.fill: parent
                    color: dawTheme.workspace
                }

                Rectangle {
                    width: root.keyWidth
                    height: parent.height
                    color: dawTheme.panel

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 10
                        anchors.bottomMargin: 10
                        spacing: 6

                        StyledTextLabel {
                            Layout.fillWidth: true
                            text: qsTrc("appshell", "Control")
                            font: ui.theme.bodyBoldFont
                            horizontalAlignment: Text.AlignLeft
                        }

                        FlatButton {
                            id: controlSelector
                            Layout.fillWidth: true
                            text: root.currentControlSpec().text
                            orientation: Qt.Horizontal
                            toolTipTitle: root.currentControlSpec().text
                            toolTipDescription: root.currentControlSpec().reason || ""
                            navigation.panel: controlLaneNavPanel
                            navigation.row: 0
                            navigation.column: 1
                            navigation.accessible.name: qsTrc("appshell", "Control lane parameter: %1")
                                                        .arg(root.currentControlSpec().text)
                            onClicked: controlLaneMenu.toggleOpened(root.buildControlLaneMenu())

                            FlatButtonMenuIndicatorTriangle {}

                            StyledMenuLoader {
                                id: controlLaneMenu
                                menuAnchorItem: root.Window.window ? root.Window.window.contentItem : null
                                isSearchable: true
                                accessibleName: qsTrc("appshell", "MIDI controller lanes")
                                onHandleMenuItem: function(itemId) {
                                    if (itemId.indexOf("lane:") !== 0)
                                        return
                                    const value = itemId.substring(5)
                                    const spec = root.controlSpec(value)
                                    if (!spec.enabled) {
                                        root.showFeedback(spec.reason, true)
                                        return
                                    }
                                    root.controlLaneParameter = value
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            StyledTextLabel {
                                Layout.fillWidth: true
                                text: root.currentControlSpec().channel
                                      ? (automationLane.selectedCount > 0
                                         ? qsTrc("appshell", "%1 of %2 points selected").arg(automationLane.selectedCount).arg(root.channelAutomationPoints.length)
                                         : qsTrc("appshell", "%1 automation points").arg(root.channelAutomationPoints.length))
                                      : root.selectedControlText()
                                color: ui.theme.fontSecondaryColor
                                horizontalAlignment: Text.AlignLeft
                                elide: Text.ElideRight
                            }

                            FlatButton {
                                visible: root.currentControlSpec().channel
                                         ? root.channelAutomationPoints.length > 0
                                         : root.selectedControlHasOverride()
                                text: qsTrc("appshell", "Reset")
                                toolTipTitle: root.currentControlSpec().channel
                                              ? qsTrc("appshell", "Remove all automation points for this channel control")
                                              : qsTrc("appshell", "Reset the selected values to the score")
                                navigation.panel: controlLaneNavPanel
                                navigation.row: 0
                                navigation.column: 2
                                onClicked: {
                                    if (root.currentControlSpec().channel) {
                                        root.projectModel.resetChannelAutomation(root.tracksModel.selectedPartId,
                                                                                 root.tracksModel.selectedInstrumentId,
                                                                                 root.controlLaneParameter)
                                    } else {
                                        root.resetSelectedControl()
                                    }
                                }
                            }
                        }

                        StyledDropdown {
                            Layout.fillWidth: true
                            visible: root.currentControlSpec().channel && automationLane.selectedCount > 0
                            model: [
                                { text: qsTrc("appshell", "Linear segment"), value: "linear" },
                                { text: qsTrc("appshell", "Curved segment"), value: "curve" },
                                { text: qsTrc("appshell", "Stepped segment"), value: "step" }
                            ]
                            currentIndex: -1
                            displayText: qsTrc("appshell", "Segment shape…")
                            navigation.panel: controlLaneNavPanel
                            navigation.row: 0
                            navigation.column: 3
                            navigation.accessible.name: qsTrc("appshell", "Shape of the segments arriving at the selected points")
                            onActivated: function(index, value) {
                                automationLane.applyShape(value)
                                currentIndex = -1
                            }
                        }

                        StyledTextLabel {
                            Layout.fillWidth: true
                            visible: !root.currentControlSpec().enabled
                                     || root.currentControlSpec().playable === false
                            text: root.currentControlSpec().reason || ""
                            color: dawTheme.warning
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }
                    }
                }

                Item {
                    id: controlGraph
                    x: root.keyWidth
                    width: parent.width - x
                    height: parent.height
                    clip: true

                    DawTimeGrid {
                        anchors.fill: parent
                        measures: root.transport.measures
                        pixelsPerTick: root.pixelsPerTick
                        viewX: editor.contentX
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        y: root.currentControlSpec().bipolar ? parent.height / 2 : parent.height - 14
                        height: 1
                        color: dawTheme.line
                    }

                    //! Note lanes: dragging across the empty lane selects the notes starting in that time range
                    MouseArea {
                        id: laneBandArea
                        anchors.fill: parent
                        enabled: root.currentControlSpec().enabled && !root.currentControlSpec().channel
                        preventStealing: true
                        property real pressX: 0
                        property int pressModifiers: 0
                        property bool banding: false
                        onPressed: mouse => {
                            root.forceActiveFocus()
                            pressX = mouse.x
                            pressModifiers = mouse.modifiers
                            banding = false
                        }
                        onPositionChanged: mouse => {
                            if (pressed && !banding && Math.abs(mouse.x - pressX) > 3)
                                banding = true
                        }
                        onReleased: mouse => {
                            const additive = (pressModifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0
                            if (banding) {
                                root.selectInTickRange((Math.min(pressX, mouse.x) + editor.contentX) / root.pixelsPerTick,
                                                       (Math.max(pressX, mouse.x) + editor.contentX) / root.pixelsPerTick, additive)
                            } else if (!additive) {
                                root.selectNotes([], "")
                            }
                            banding = false
                        }
                        onCanceled: banding = false
                    }

                    Rectangle {
                        visible: laneBandArea.banding
                        x: Math.min(laneBandArea.pressX, laneBandArea.mouseX)
                        width: Math.abs(laneBandArea.mouseX - laneBandArea.pressX)
                        height: parent.height
                        color: Qt.rgba(dawTheme.brandAccent.r, dawTheme.brandAccent.g, dawTheme.brandAccent.b, 0.15)
                        border.width: 1
                        border.color: dawTheme.brandAccent
                        z: 1
                    }

                    Repeater {
                        model: !root.currentControlSpec().channel && root.currentControlSpec().enabled ? root.visibleNotes : []
                        delegate: Item {
                            id: noteStem
                            required property var modelData
                            readonly property bool isSelected: root.isNoteSelected(modelData.noteId)
                            //! A drag changes every selected stem by the same amount
                            readonly property bool dragging: root.laneDragActive && isSelected
                            property bool hovered: false
                            property bool dragOrigin: false
                            property bool collapseOnRelease: false
                            property real pressSceneY: 0
                            readonly property real availableHeight: Math.max(24, controlGraph.height - 34)
                            readonly property real baseValue: root.normalizedNoteControlValue(modelData)
                            readonly property real shownValue: dragging ? Math.max(0, Math.min(1, baseValue + root.laneDragDelta))
                                                                        : baseValue
                            readonly property real valueY: 8 + (1 - shownValue) * availableHeight
                            readonly property real baselineY: root.currentControlSpec().bipolar ? 8 + availableHeight / 2
                                                                                               : 8 + availableHeight

                            x: modelData.startTick * root.pixelsPerTick - editor.contentX - width / 2
                            y: 0
                            width: 18
                            height: controlGraph.height
                            z: isSelected || dragging ? 4 : 2


                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: Math.min(noteStem.valueY, noteStem.baselineY)
                                width: noteStem.isSelected ? 4 : 3
                                height: Math.max(2, Math.abs(noteStem.baselineY - noteStem.valueY))
                                color: noteStem.isSelected ? dawTheme.brandAccent : dawTheme.trackColor(noteStem.modelData.colorIndex)
                            }

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: noteStem.valueY - height / 2
                                width: noteStem.isSelected || noteStem.hovered || noteStem.dragging ? 10 : 7
                                height: width
                                radius: width / 2
                                color: noteStem.isSelected ? dawTheme.brandAccent : dawTheme.trackColor(noteStem.modelData.colorIndex)
                                border.width: noteStem.isSelected ? 2 : 1
                                border.color: noteStem.isSelected ? dawTheme.noteSelectedOutline : dawTheme.noteBorder
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                preventStealing: true
                                cursorShape: Qt.SizeVerCursor
                                onEntered: noteStem.hovered = true
                                onExited: noteStem.hovered = false
                                onPressed: mouse => {
                                    root.forceActiveFocus()
                                    const modifiers = mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)
                                    noteStem.collapseOnRelease = noteStem.isSelected && root.selectedNotes.length > 1 && !modifiers
                                    if (!noteStem.collapseOnRelease)
                                        root.clickSelect(noteStem.modelData.noteId, mouse.modifiers)
                                    noteStem.pressSceneY = mapToItem(null, mouse.x, mouse.y).y
                                    noteStem.dragOrigin = true
                                    root.cancelDrags()
                                }
                                // Relative: a click only selects, and a drag moves the values from where they are.
                                onPositionChanged: mouse => {
                                    if (!pressed || !noteStem.dragOrigin || !noteStem.isSelected)
                                        return
                                    const rise = noteStem.pressSceneY - mapToItem(null, mouse.x, mouse.y).y
                                    if (!root.laneDragActive && Math.abs(rise) < 3)
                                        return
                                    root.laneDragActive = true
                                    root.laneDragDelta = rise / noteStem.availableHeight
                                }
                                onCanceled: {
                                    if (noteStem.dragOrigin)
                                        root.cancelDrags()
                                    noteStem.dragOrigin = false
                                    noteStem.collapseOnRelease = false
                                }
                                onReleased: {
                                    if (root.laneDragActive && Math.abs(root.laneDragDelta) > 0.0001)
                                        root.commitSelectedControlDelta(root.laneDragDelta)
                                    else if (noteStem.collapseOnRelease)
                                        root.clickSelect(noteStem.modelData.noteId, 0)
                                    root.cancelDrags()
                                    noteStem.dragOrigin = false
                                    noteStem.collapseOnRelease = false
                                }
                                onDoubleClicked: root.resetNoteControl(noteStem.modelData)
                            }

                            Rectangle {
                                visible: noteStem.hovered || (noteStem.dragOrigin && root.laneDragActive)
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 0
                                width: noteReadout.implicitWidth + 12
                                height: 20
                                radius: 3
                                color: ui.theme.popupBackgroundColor
                                border.color: dawTheme.line

                                StyledTextLabel {
                                    id: noteReadout
                                    anchors.centerIn: parent
                                    text: root.noteName(noteStem.modelData.pitch) + "  "
                                          + root.displayControlValue(noteStem.shownValue)
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }

                    DawAutomationLane {
                        id: automationLane
                        anchors.fill: parent
                        visible: root.currentControlSpec().enabled && root.currentControlSpec().channel
                        enabled: visible
                        points: visible ? root.channelAutomationPoints : []
                        pixelsPerTick: root.pixelsPerTick
                        viewX: editor.contentX
                        snapTicks: root.snapTicks
                        transport: root.transport
                        bipolar: root.currentControlSpec().bipolar
                        lineColor: root.trackColor
                        valueText: value => root.displayControlValue(value)
                        onEditRequested: edits => {
                            if (!root.projectModel.editChannelAutomation(root.tracksModel.selectedPartId,
                                                                         root.tracksModel.selectedInstrumentId,
                                                                         root.controlLaneParameter, edits))
                                root.showFeedback(qsTrc("appshell", "The automation could not be changed."), true)
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        visible: !root.currentControlSpec().enabled
                        spacing: 4

                        StyledIconLabel {
                            anchors.horizontalCenter: parent.horizontalCenter
                            iconCode: IconCode.WARNING
                            color: dawTheme.warning
                        }
                        StyledTextLabel {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.min(implicitWidth, controlGraph.width - 32)
                            text: root.currentControlSpec().reason || ""
                            color: ui.theme.fontSecondaryColor
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    Rectangle {
                        x: root.transport.tick * root.pixelsPerTick - editor.contentX
                        width: 2
                        height: parent.height
                        color: dawTheme.playhead
                        visible: x >= 0 && x <= parent.width
                        z: 10
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 5
                    color: resizeLaneArea.containsMouse || resizeLaneArea.pressed ? dawTheme.brandAccent : dawTheme.line

                    MouseArea {
                        id: resizeLaneArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SizeVerCursor
                        property real pressSceneY: 0
                        property real initialHeight: 0
                        onPressed: mouse => {
                            pressSceneY = mapToItem(null, mouse.x, mouse.y).y
                            initialHeight = root.controlLaneHeight
                        }
                        onPositionChanged: mouse => {
                            if (!pressed)
                                return
                            const sceneY = mapToItem(null, mouse.x, mouse.y).y
                            root.controlLaneHeight = Math.max(92, Math.min(body.height - 120,
                                                                          initialHeight + pressSceneY - sceneY))
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: editor
                spacing: 4
                visible: root.notes.length === 0
                z: 40

                StyledTextLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.startTrack < 0 ? qsTrc("appshell", "Select a track to see its notes")
                                              : qsTrc("appshell", "This part has no notes yet")
                    font: ui.theme.bodyBoldFont
                }
                StyledTextLabel {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: root.startTrack >= 0
                    text: root.drawBlockedReason.length > 0 ? root.drawBlockedReason
                                                           : qsTrc("appshell", "Choose Draw and click the grid, or write notes in Score mode")
                    color: ui.theme.fontSecondaryColor
                }
            }
        }

        // Footer: result of the last action, or details of the selected note.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: dawTheme.panel

            SeparatorLine { anchors.top: parent.top; orientation: Qt.Horizontal }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                StyledIconLabel {
                    visible: root.feedbackText.length > 0 && root.feedbackIsError
                    iconCode: IconCode.WARNING
                    color: dawTheme.warning
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    color: root.feedbackText.length > 0 && root.feedbackIsError ? dawTheme.warning : ui.theme.fontSecondaryColor
                    text: {
                        if (root.feedbackText.length > 0)
                            return root.feedbackText
                        const note = root.selectedNote
                        if (note) {
                            let details = root.noteName(note.pitch) + "  ·  " + root.transport.positionTextForTick(note.scoreTick)
                            if (root.performanceMode)
                                details += "  ·  " + qsTrc("appshell", "velocity %1").arg(note.velocity)
                                        + "  ·  " + qsTrc("appshell", "offset %1").arg(note.startOffsetTicks)
                                        + "      " + qsTrc("appshell", "Delete: back to score")
                            else
                                details += "      " + qsTrc("appshell", "↑↓ pitch  ·  Ctrl+↑↓ octave  ·  Delete removes the note")
                            return details
                        }
                        if (root.performanceMode)
                            return qsTrc("appshell", "Drag a note to change when it plays (Shift: fine). The score stays as written.")
                        if (root.drawActive)
                            return root.staffCount > 1
                                   ? qsTrc("appshell", "Click to add a note. Notes below C4 go to the lower staff.")
                                   : qsTrc("appshell", "Click to add a note to the score.")
                        return qsTrc("appshell", "Click a note to select it in the score too; drag it to move it in the score (time and pitch).")
                    }
                }

                StyledTextLabel {
                    visible: root.performanceMode && root.selectedNote !== null
                    text: root.selectedNote && root.selectedNote.hasVelocityOverride
                          ? qsTrc("appshell", "Velocity (modified)") : qsTrc("appshell", "Velocity")
                    color: root.selectedNote && root.selectedNote.hasVelocityOverride
                           ? dawTheme.brandAccent : ui.theme.fontSecondaryColor
                }

                IncrementalPropertyControl {
                    Layout.preferredWidth: 72
                    visible: root.performanceMode && root.selectedNote !== null
                    enabled: visible
                    currentValue: root.selectedNote ? root.selectedNote.velocity : 64
                    minValue: 1
                    maxValue: 127
                    step: 1
                    decimals: 0
                    navigation.panel: navPanel
                    navigation.column: 12
                    navigation.accessible.name: root.selectedNote && root.selectedNote.hasVelocityOverride
                                                ? qsTrc("appshell", "Modified velocity for selected note")
                                                : qsTrc("appshell", "Velocity for selected note")
                    // Typed once, it applies to every selected note.
                    onValueEditingFinished: function(newValue) {
                        if (root.selectedNotes.length === 0)
                            return
                        const velocity = Math.max(1, Math.min(127, Math.round(Number(newValue))))
                        const velocities = {}
                        for (let index = 0; index < root.selectedNotes.length; ++index)
                            velocities[root.selectedNotes[index].noteId] = velocity
                        if (!root.projectModel.setPerformanceVelocities(velocities))
                            root.showFeedback(qsTrc("appshell", "Velocity could not be changed."), true)
                    }
                }

                FlatButton {
                    visible: root.performanceMode
                    enabled: root.selectedNotes.some(note => note.hasPerformanceOverride)
                    text: qsTrc("appshell", "Reset to score")
                    toolTipTitle: qsTrc("appshell", "Remove the timing and velocity changes of the selected notes")
                    navigation.panel: navPanel
                    navigation.column: 13
                    onClicked: root.projectModel.resetPerformances(root.selectedNoteIds())
                }
            }
        }
    }
}
