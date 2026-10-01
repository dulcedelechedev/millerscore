/* SPDX-License-Identifier: GPL-3.0-only */
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Muse.Ui
import Muse.UiComponents
import MuseScore.AppShell

//! DAW view of the open score. Tracks are the score's parts, the transport is
//! the application's transport, and every panel follows one track selection.
Rectangle {
    id: root

    DawTheme { id: dawTheme }

    readonly property bool compact: width < 1200
    property bool inspectorOpen: width >= 1100
    property bool pianoRollOpen: true
    property bool followPlayhead: true
    readonly property int headerWidth: compact ? 184 : 224
    readonly property int inspectorWidth: compact ? 248 : 280
    readonly property real defaultPixelsPerTick: 0.1
    property real pixelsPerTick: defaultPixelsPerTick
    property int snapTicks: 120
    //! Off: notes and the playhead go exactly where they are dropped
    property bool snapEnabled: true
    readonly property int effectiveSnapTicks: snapEnabled ? snapTicks : 1
    //! "" (all panels), "arranger" or "pianoRoll": one panel takes the whole workspace
    property string maximizedPanel: ""
    readonly property real minimumPixelsPerTick: 0.02
    readonly property real maximumPixelsPerTick: 0.5

    color: dawTheme.workspace

    DawProjectModel { id: projectModel }
    DawInstrumentModel { id: instrumentModel }
    DawTransportModel {
        id: transport
        onTickChanged: root.followIfNeeded()
    }
    DawTracksModel {
        id: tracksModel
        onSelectionChanged: instrumentModel.setTrack(selectedPartId, selectedInstrumentId)
    }
    //! Audio tracks (WAV/MP3/FLAC clips), listed below the score's tracks
    DawAudioTracksModel { id: audioTracksModel }

    readonly property int scoreLanesHeight: tracksModel.count * dawTheme.trackRowHeight
    readonly property int audioLanesHeight: audioTracksModel.count * dawTheme.trackRowHeight

    //! Audio tracks take colours after the score's tracks
    function audioTrackColor(colorIndex) {
        return dawTheme.trackColor(tracksModel.count + colorIndex)
    }

    Component.onCompleted: {
        projectModel.load()
        instrumentModel.load()
        transport.load()
        tracksModel.load()
        audioTracksModel.load()
    }

    NavigationSection {
        id: navSection
        name: "DawWorkspace"
        enabled: root.visible
        order: 5
    }

    NavigationPanel {
        id: toolbarNavPanel
        name: "DawToolbar"
        section: navSection
        order: 1
        direction: NavigationPanel.Horizontal
        accessible.name: qsTrc("appshell", "DAW toolbar")
    }

    NavigationPanel {
        id: tracksNavPanel
        name: "DawTracks"
        section: navSection
        order: 2
        direction: NavigationPanel.Both
        accessible.name: qsTrc("appshell", "Tracks")
    }

    function setTimelineX(value) {
        timelineViewport.contentX = Math.max(0, Math.min(Math.max(0, timelineViewport.contentWidth - timelineViewport.width), value))
    }

    function followIfNeeded() {
        if (!followPlayhead || !transport.playing || timelineViewport.width <= 0)
            return
        const x = transport.tick * pixelsPerTick
        const left = timelineViewport.contentX + timelineViewport.width * 0.08
        const right = timelineViewport.contentX + timelineViewport.width * 0.85
        if (x < left || x > right)
            setTimelineX(x - timelineViewport.width * 0.2)
    }

    //! `viewX`: the point of the viewport that stays put (its centre when not given)
    function zoom(factor, viewX) {
        const anchorX = viewX === undefined ? timelineViewport.width / 2 : viewX
        const oldScale = pixelsPerTick
        const anchorTick = (timelineViewport.contentX + anchorX) / oldScale
        pixelsPerTick = Math.max(minimumPixelsPerTick, Math.min(maximumPixelsPerTick, oldScale * factor))
        setTimelineX(anchorTick * pixelsPerTick - anchorX)
    }

    function toggleMaximized(panel) {
        maximizedPanel = maximizedPanel === panel ? "" : panel
        if (maximizedPanel === "pianoRoll")
            pianoRollOpen = true
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Contextual DAW toolbar. Play/stop stay in the application's single
        // playback toolbar; this bar shows position and view tools only.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: dawTheme.toolbarHeight
            color: dawTheme.panel

            SeparatorLine { anchors.bottom: parent.bottom; orientation: Qt.Horizontal }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                FlatButton {
                    icon: IconCode.PLUS
                    text: root.compact ? "" : qsTrc("appshell", "Instruments")
                    orientation: Qt.Horizontal
                    enabled: tracksModel.hasScore
                    toolTipTitle: qsTrc("appshell", "Add or remove instruments")
                    toolTipDescription: qsTrc("appshell", "Each instrument in the score is a track")
                    navigation.panel: toolbarNavPanel
                    navigation.column: 0
                    navigation.accessible.name: qsTrc("appshell", "Add or remove instruments")
                    onClicked: tracksModel.openInstrumentsDialog()
                }

                FlatButton {
                    icon: IconCode.WAVEFORM
                    text: root.compact ? "" : qsTrc("appshell", "Audio track")
                    orientation: Qt.Horizontal
                    enabled: tracksModel.hasScore
                    toolTipTitle: qsTrc("appshell", "Add an audio track")
                    toolTipDescription: qsTrc("appshell", "For WAV, MP3 or FLAC recordings and samples, played with the score")
                    navigation.panel: toolbarNavPanel
                    navigation.column: 10
                    navigation.accessible.name: qsTrc("appshell", "Add an audio track")
                    onClicked: audioTracksModel.addTrack()
                }

                FlatButton {
                    icon: IconCode.MICROPHONE
                    text: audioTracksModel.recording
                          ? qsTrc("appshell", "Stop %1").arg(new Date(audioTracksModel.recordedSeconds * 1000).toISOString().substr(14, 5))
                          : (root.compact ? "" : qsTrc("appshell", "Record"))
                    orientation: Qt.Horizontal
                    accentButton: audioTracksModel.recording
                    enabled: audioTracksModel.recording || audioTracksModel.hasArmedTrack
                    toolTipTitle: audioTracksModel.recording ? qsTrc("appshell", "Stop recording") : qsTrc("appshell", "Record")
                    toolTipDescription: qsTrc("appshell", "Plays the score and records the armed audio track (R) from the playhead, "
                                                          + "through the ASIO driver (Preferences > Audio & MIDI)")
                    navigation.panel: toolbarNavPanel
                    navigation.column: 11
                    navigation.accessible.name: toolTipTitle
                    onClicked: audioTracksModel.toggleRecording(transport.tick)
                }

                StyledTextLabel {
                    Layout.maximumWidth: 360
                    visible: audioTracksModel.lastError.length > 0
                    text: audioTracksModel.lastError
                    color: dawTheme.warning
                    elide: Text.ElideRight
                }

                Item { Layout.fillWidth: true }

                FlatButton {
                    icon: IconCode.MAGNET
                    text: root.compact ? "" : qsTrc("appshell", "Snap")
                    orientation: Qt.Horizontal
                    accentButton: root.snapEnabled
                    toolTipTitle: root.snapEnabled ? qsTrc("appshell", "Turn the grid off") : qsTrc("appshell", "Turn the grid on")
                    toolTipDescription: root.snapEnabled
                                        ? qsTrc("appshell", "Notes and the playhead snap to the grid; turn it off to place them freely")
                                        : qsTrc("appshell", "Notes and the playhead go exactly where you drop them")
                    navigation.panel: toolbarNavPanel
                    navigation.column: 1
                    navigation.accessible.name: root.snapEnabled ? qsTrc("appshell", "Turn snapping to the grid off")
                                                                 : qsTrc("appshell", "Turn snapping to the grid on")
                    onClicked: root.snapEnabled = !root.snapEnabled
                }

                StyledDropdown {
                    id: snapDropdown
                    enabled: root.snapEnabled
                    Layout.preferredWidth: 92
                    model: [
                        { text: qsTrc("appshell", "Beat"), value: 480 },
                        { text: qsTrc("appshell", "1/8"), value: 240 },
                        { text: qsTrc("appshell", "1/16"), value: 120 },
                        { text: qsTrc("appshell", "1/32"), value: 60 }
                    ]
                    currentIndex: indexOfValue(root.snapTicks)
                    navigation.panel: toolbarNavPanel
                    navigation.column: 2
                    navigation.accessible.name: qsTrc("appshell", "Snap to grid")
                    onActivated: function(index, value) { root.snapTicks = value }
                }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: dawTheme.line }

                FlatButton {
                    icon: IconCode.ZOOM_OUT
                    toolTipTitle: qsTrc("appshell", "Zoom out")
                    enabled: root.pixelsPerTick > root.minimumPixelsPerTick
                    navigation.panel: toolbarNavPanel
                    navigation.column: 3
                    navigation.accessible.name: qsTrc("appshell", "Zoom out")
                    onClicked: root.zoom(0.8)
                }

                StyledTextLabel {
                    Layout.preferredWidth: 40
                    text: Math.round(root.pixelsPerTick / root.defaultPixelsPerTick * 100) + "%"
                    color: ui.theme.fontSecondaryColor
                }

                FlatButton {
                    icon: IconCode.ZOOM_IN
                    toolTipTitle: qsTrc("appshell", "Zoom in")
                    enabled: root.pixelsPerTick < root.maximumPixelsPerTick
                    navigation.panel: toolbarNavPanel
                    navigation.column: 4
                    navigation.accessible.name: qsTrc("appshell", "Zoom in")
                    onClicked: root.zoom(1.25)
                }

                FlatButton {
                    icon: IconCode.PLAYHEAD
                    text: root.compact ? "" : qsTrc("appshell", "Follow")
                    orientation: Qt.Horizontal
                    accentButton: root.followPlayhead
                    toolTipTitle: qsTrc("appshell", "Follow playback")
                    toolTipDescription: qsTrc("appshell", "Scroll automatically to keep the playhead in view")
                    navigation.panel: toolbarNavPanel
                    navigation.column: 5
                    navigation.accessible.name: root.followPlayhead
                                                ? qsTrc("appshell", "Stop following playback")
                                                : qsTrc("appshell", "Follow playback")
                    onClicked: root.followPlayhead = !root.followPlayhead
                }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 20; color: dawTheme.line }

                FlatButton {
                    icon: IconCode.MUSIC_NOTES
                    text: root.compact ? "" : qsTrc("appshell", "Piano Roll")
                    orientation: Qt.Horizontal
                    accentButton: root.pianoRollOpen
                    toolTipTitle: root.pianoRollOpen ? qsTrc("appshell", "Hide Piano Roll") : qsTrc("appshell", "Show Piano Roll")
                    navigation.panel: toolbarNavPanel
                    navigation.column: 6
                    navigation.accessible.name: root.pianoRollOpen
                                                ? qsTrc("appshell", "Hide Piano Roll")
                                                : qsTrc("appshell", "Show Piano Roll")
                    onClicked: {
                        root.pianoRollOpen = !root.pianoRollOpen
                        if (!root.pianoRollOpen && root.maximizedPanel === "pianoRoll")
                            root.maximizedPanel = ""
                    }
                }

                FlatButton {
                    icon: IconCode.SETTINGS_COG
                    text: root.compact ? "" : qsTrc("appshell", "Inspector")
                    orientation: Qt.Horizontal
                    accentButton: root.inspectorOpen
                    toolTipTitle: root.inspectorOpen ? qsTrc("appshell", "Hide Inspector") : qsTrc("appshell", "Show Inspector")
                    navigation.panel: toolbarNavPanel
                    navigation.column: 7
                    navigation.accessible.name: root.inspectorOpen
                                                ? qsTrc("appshell", "Hide Inspector")
                                                : qsTrc("appshell", "Show Inspector")
                    onClicked: {
                        root.inspectorOpen = !root.inspectorOpen
                        if (root.inspectorOpen)
                            root.maximizedPanel = ""
                    }
                }

                FlatButton {
                    icon: IconCode.APP_MAXIMIZE
                    text: root.compact ? "" : qsTrc("appshell", "Full screen")
                    orientation: Qt.Horizontal
                    toolTipTitle: qsTrc("appshell", "Full screen")
                    toolTipDescription: qsTrc("appshell", "Show the whole window on the screen; use the maximize button of a panel to give it the whole workspace")
                    navigation.panel: toolbarNavPanel
                    navigation.column: 8
                    navigation.accessible.name: qsTrc("appshell", "Toggle full screen")
                    onClicked: tracksModel.toggleFullScreen()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: Qt.Vertical

                handle: Rectangle {
                    implicitHeight: 5
                    color: SplitHandle.pressed ? dawTheme.focus
                                               : (SplitHandle.hovered ? dawTheme.line : dawTheme.panel)
                }

                // Arranger
                Item {
                    id: arranger
                    // Tall enough for the tracks, leaving the rest to the piano roll.
                    SplitView.preferredHeight: Math.max(150, Math.min(root.height * (root.height < 800 ? 0.4 : 0.55),
                                                                     dawTheme.rulerHeight + Math.max(1, tracksModel.count + audioTracksModel.count) * dawTheme.trackRowHeight + 48))
                    SplitView.fillHeight: !root.pianoRollOpen || root.maximizedPanel === "arranger"
                    SplitView.minimumHeight: 120
                    visible: root.maximizedPanel !== "pianoRoll"
                    clip: true

                    // Middle button: drag the tracks view in both directions.
                    DragHandler {
                        target: null
                        acceptedButtons: Qt.MiddleButton
                        cursorShape: Qt.ClosedHandCursor
                        property real startX: 0
                        property real startY: 0
                        onActiveChanged: {
                            if (active) {
                                startX = timelineViewport.contentX
                                startY = timelineViewport.contentY
                            }
                        }
                        onTranslationChanged: {
                            if (!active)
                                return
                            root.setTimelineX(startX - translation.x)
                            timelineViewport.contentY = Math.max(0, Math.min(Math.max(0, timelineViewport.contentHeight - timelineViewport.height),
                                                                             startY - translation.y))
                        }
                    }

                    Rectangle {
                        width: root.headerWidth
                        height: dawTheme.rulerHeight
                        color: dawTheme.panel

                        StyledTextLabel {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("appshell", "Tracks (%1)").arg(tracksModel.count)
                            color: ui.theme.fontSecondaryColor
                        }
                        FlatButton {
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24
                            height: 24
                            transparent: true
                            icon: root.maximizedPanel === "arranger" ? IconCode.APP_UNMAXIMIZE : IconCode.APP_MAXIMIZE
                            toolTipTitle: root.maximizedPanel === "arranger" ? qsTrc("appshell", "Restore the panels")
                                                                             : qsTrc("appshell", "Maximize the tracks")
                            navigation.panel: toolbarNavPanel
                            navigation.column: 9
                            navigation.accessible.name: toolTipTitle
                            onClicked: root.toggleMaximized("arranger")
                        }
                        SeparatorLine { anchors.bottom: parent.bottom; orientation: Qt.Horizontal }
                        SeparatorLine { anchors.right: parent.right; orientation: Qt.Vertical }
                    }

                    Item {
                        x: root.headerWidth
                        width: parent.width - x
                        height: dawTheme.rulerHeight

                        Rectangle { anchors.fill: parent; color: dawTheme.panel }

                        DawTimeGrid {
                            anchors.fill: parent
                            ruler: true
                            measures: transport.measures
                            pixelsPerTick: root.pixelsPerTick
                            viewX: timelineViewport.contentX
                        }

                        Rectangle {
                            x: transport.tick * root.pixelsPerTick - timelineViewport.contentX - 5
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
                                transport.seekToTick(transport.snapTick((mouseX + timelineViewport.contentX) / root.pixelsPerTick, root.effectiveSnapTicks, true))
                            }
                            onPressed: mouse => seek(mouse.x)
                            onPositionChanged: mouse => { if (pressed) seek(mouse.x) }
                        }

                        SeparatorLine { anchors.bottom: parent.bottom; orientation: Qt.Horizontal }
                    }

                    ListView {
                        id: trackHeaders
                        y: dawTheme.rulerHeight
                        width: root.headerWidth
                        height: parent.height - y
                        model: tracksModel
                        interactive: false
                        clip: true
                        contentY: timelineViewport.contentY

                        delegate: DawTrackHeader {
                            required property int index
                            required property string name
                            required property string sound
                            required property int colorIndex
                            // Existing header properties filled from the model roles.
                            required muted
                            required solo
                            required selected

                            width: trackHeaders.width
                            height: dawTheme.trackRowHeight
                            trackName: name
                            soundName: sound
                            trackColor: dawTheme.trackColor(colorIndex)
                            navigationPanel: tracksNavPanel
                            navigationRow: index
                            onSelectRequested: modifiers => tracksModel.clickTrack(index, modifiers)
                            onMuteRequested: tracksModel.toggleMute(index)
                            onSoloRequested: tracksModel.toggleSolo(index)
                        }
                    }

                    Item {
                        y: dawTheme.rulerHeight
                        width: root.headerWidth
                        height: parent.height - y
                        clip: true

                        Column {
                            y: root.scoreLanesHeight - timelineViewport.contentY
                            width: parent.width

                            Repeater {
                                model: audioTracksModel
                                delegate: Rectangle {
                                    id: audioHeader
                                    required property int index
                                    required property string name
                                    required property int colorIndex
                                    required property bool muted
                                    required property bool armed

                                    width: root.headerWidth
                                    height: dawTheme.trackRowHeight
                                    color: dawTheme.panel

                                    Rectangle {
                                        width: 4
                                        height: parent.height
                                        color: root.audioTrackColor(audioHeader.colorIndex)
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 6
                                        spacing: 4

                                        StyledIconLabel {
                                            iconCode: IconCode.WAVEFORM
                                            color: ui.theme.fontSecondaryColor
                                        }
                                        StyledTextLabel {
                                            Layout.fillWidth: true
                                            horizontalAlignment: Text.AlignLeft
                                            text: audioHeader.name
                                            font: ui.theme.bodyBoldFont
                                            elide: Text.ElideRight
                                        }
                                        FlatButton {
                                            Layout.preferredWidth: 26
                                            Layout.preferredHeight: 26
                                            text: "M"
                                            accentButton: audioHeader.muted
                                            toolTipTitle: audioHeader.muted ? qsTrc("appshell", "Unmute") : qsTrc("appshell", "Mute")
                                            onClicked: audioTracksModel.toggleMute(audioHeader.index)
                                        }
                                        FlatButton {
                                            Layout.preferredWidth: 26
                                            Layout.preferredHeight: 26
                                            text: "R"
                                            accentButton: audioHeader.armed
                                            enabled: !audioTracksModel.recording
                                            toolTipTitle: audioHeader.armed ? qsTrc("appshell", "Disarm recording")
                                                                            : qsTrc("appshell", "Arm for recording")
                                            toolTipDescription: qsTrc("appshell", "Record the audio interface's input into this track")
                                            onClicked: audioTracksModel.toggleArm(audioHeader.index)
                                        }
                                        FlatButton {
                                            Layout.preferredWidth: 26
                                            Layout.preferredHeight: 26
                                            icon: IconCode.IMPORT
                                            toolTipTitle: qsTrc("appshell", "Import audio")
                                            toolTipDescription: qsTrc("appshell", "Place a WAV, MP3 or FLAC file at the playhead (or double-click the lane where it should go)")
                                            onClicked: audioTracksModel.importAudio(audioHeader.index, transport.tick)
                                        }
                                        FlatButton {
                                            Layout.preferredWidth: 26
                                            Layout.preferredHeight: 26
                                            icon: IconCode.DELETE_TANK
                                            toolTipTitle: qsTrc("appshell", "Remove this audio track")
                                            onClicked: audioTracksModel.removeTrack(audioHeader.index)
                                        }
                                    }

                                    SeparatorLine { anchors.bottom: parent.bottom; orientation: Qt.Horizontal }
                                }
                            }
                        }
                    }

                    SeparatorLine {
                        x: root.headerWidth - 1
                        y: dawTheme.rulerHeight
                        height: parent.height - y
                        orientation: Qt.Vertical
                    }

                    DawTimeGrid {
                        x: root.headerWidth
                        y: dawTheme.rulerHeight
                        width: parent.width - x
                        height: parent.height - y
                        measures: transport.measures
                        pixelsPerTick: root.pixelsPerTick
                        viewX: timelineViewport.contentX
                    }

                    Flickable {
                        id: timelineViewport
                        // Mouse buttons edit and select; the wheel and scroll bars scroll.
                        acceptedButtons: Qt.NoButton
                        x: root.headerWidth
                        y: dawTheme.rulerHeight
                        width: parent.width - x
                        height: parent.height - y
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: Math.max(width, (transport.endTick + 1920) * root.pixelsPerTick)
                        contentHeight: Math.max(height, root.scoreLanesHeight + root.audioLanesHeight)
                        ScrollBar.horizontal: StyledScrollBar { policy: ScrollBar.AsNeeded }
                        ScrollBar.vertical: StyledScrollBar { policy: ScrollBar.AsNeeded }

                        // Ctrl + wheel zooms around the pointer; the plain wheel keeps scrolling.
                        WheelHandler {
                            acceptedModifiers: Qt.ControlModifier
                            onWheel: event => {
                                const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                                if (delta !== 0)
                                    root.zoom(delta > 0 ? 1.25 : 0.8, point.position.x - timelineViewport.contentX)
                                event.accepted = true
                            }
                        }

                        ListView {
                            id: lanes
                            width: timelineViewport.contentWidth
                            height: tracksModel.count * dawTheme.trackRowHeight
                            model: tracksModel
                            interactive: false

                            delegate: Item {
                                id: lane
                                required property int index
                                required property string name
                                required property int colorIndex
                                required property bool muted
                                required property bool selected
                                required property var clips
                                readonly property color trackColor: dawTheme.trackColor(colorIndex)

                                width: lanes.width
                                height: dawTheme.trackRowHeight

                                Rectangle {
                                    anchors.fill: parent
                                    color: lane.selected ? dawTheme.laneSelected
                                                         : (lane.index % 2 === 1 ? dawTheme.laneAlternate : "transparent")
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: mouse => tracksModel.clickTrack(lane.index, mouse.modifiers)
                                }

                                Repeater {
                                    model: lane.clips
                                    delegate: DawClip {
                                        required property var modelData
                                        x: modelData.startTick * root.pixelsPerTick
                                        y: 4
                                        width: Math.max(6, (modelData.endTick - modelData.startTick) * root.pixelsPerTick)
                                        height: lane.height - 8
                                        trackColor: lane.trackColor
                                        selected: lane.selected
                                        muted: lane.muted
                                        label: modelData.firstBar === modelData.lastBar
                                               ? qsTrc("appshell", "Bar %1").arg(modelData.firstBar)
                                               : qsTrc("appshell", "Bars %1–%2").arg(modelData.firstBar).arg(modelData.lastBar)

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: mouse => tracksModel.clickTrack(lane.index, mouse.modifiers)
                                            onDoubleClicked: {
                                                tracksModel.selectTrack(lane.index)
                                                root.pianoRollOpen = true
                                            }
                                        }
                                    }
                                }

                                DawLaneNotes {
                                    x: timelineViewport.contentX
                                    y: 4
                                    width: timelineViewport.width
                                    height: lane.height - 8
                                    clips: lane.clips
                                    pixelsPerTick: root.pixelsPerTick
                                    viewX: timelineViewport.contentX
                                    noteColor: dawTheme.contrastingText(dawTheme.workspace)
                                    opacity: lane.muted ? 0.35 : 0.85
                                }

                                SeparatorLine { anchors.bottom: parent.bottom; orientation: Qt.Horizontal }
                            }
                        }

                        Column {
                            y: root.scoreLanesHeight
                            width: timelineViewport.contentWidth

                            Repeater {
                                model: audioTracksModel
                                delegate: Item {
                                    id: audioLane
                                    required property int index
                                    required property int colorIndex
                                    required property bool muted
                                    required property var clips
                                    readonly property color trackColor: root.audioTrackColor(colorIndex)

                                    width: timelineViewport.contentWidth
                                    height: dawTheme.trackRowHeight

                                    Rectangle {
                                        anchors.fill: parent
                                        color: (tracksModel.count + audioLane.index) % 2 === 1 ? dawTheme.laneAlternate : "transparent"
                                    }

                                    // Double-click the lane: import a file there
                                    MouseArea {
                                        anchors.fill: parent
                                        onDoubleClicked: mouse => audioTracksModel.importAudio(
                                                             audioLane.index,
                                                             transport.snapTick(mouse.x / root.pixelsPerTick, root.effectiveSnapTicks, false))
                                    }

                                    Repeater {
                                        model: audioLane.clips
                                        delegate: DawAudioClip {
                                            required property var modelData
                                            clipData: modelData
                                            y: 4
                                            height: audioLane.height - 8
                                            pixelsPerTick: root.pixelsPerTick
                                            viewX: timelineViewport.contentX
                                            viewWidth: timelineViewport.width
                                            trackColor: audioLane.trackColor
                                            muted: audioLane.muted
                                            snapTick: tick => transport.snapTick(Math.round(tick), root.effectiveSnapTicks, true)
                                            onMoveRequested: tick => audioTracksModel.moveClip(audioLane.index, modelData.id, tick)
                                            onRemoveRequested: audioTracksModel.removeClip(audioLane.index, modelData.id)
                                        }
                                    }

                                    SeparatorLine { anchors.bottom: parent.bottom; orientation: Qt.Horizontal }
                                }
                            }
                        }

                        Rectangle {
                            x: transport.tick * root.pixelsPerTick
                            width: 2
                            height: timelineViewport.contentHeight
                            color: dawTheme.playhead
                            z: 20
                        }
                    }

                    Column {
                        anchors.centerIn: timelineViewport
                        spacing: 10
                        visible: tracksModel.count === 0
                        z: 40

                        StyledTextLabel {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: tracksModel.hasScore ? qsTrc("appshell", "This score has no instruments yet")
                                                       : qsTrc("appshell", "Open or create a score to arrange it here")
                            font: ui.theme.largeBodyBoldFont
                        }
                        StyledTextLabel {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: tracksModel.hasScore
                            text: qsTrc("appshell", "Every instrument you add becomes a track")
                            color: ui.theme.fontSecondaryColor
                        }
                        FlatButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: tracksModel.hasScore
                            icon: IconCode.PLUS
                            text: qsTrc("appshell", "Add instruments")
                            orientation: Qt.Horizontal
                            accentButton: true
                            onClicked: tracksModel.openInstrumentsDialog()
                        }
                    }
                }

                PianoRoll {
                    instrumentModel: instrumentModel
                    visible: root.pianoRollOpen && root.maximizedPanel !== "arranger"
                    SplitView.fillHeight: root.pianoRollOpen
                    SplitView.minimumHeight: 180
                    projectModel: projectModel
                    keyWidth: root.headerWidth
                    transport: transport
                    tracksModel: tracksModel
                    timelineContentX: timelineViewport.contentX
                    pixelsPerTick: root.pixelsPerTick
                    canZoomIn: root.pixelsPerTick < root.maximumPixelsPerTick
                    canZoomOut: root.pixelsPerTick > root.minimumPixelsPerTick
                    snapTicks: root.effectiveSnapTicks
                    maximized: root.maximizedPanel === "pianoRoll"
                    navigationSection: navSection
                    navigationOrderStart: 3
                    onTimelineScrollRequested: value => root.setTimelineX(value)
                    onZoomRequested: (factor, viewX) => root.zoom(factor, viewX)
                    onMaximizeRequested: root.toggleMaximized("pianoRoll")
                }
            }

            DawTrackInspector {
                visible: root.inspectorOpen && root.maximizedPanel === ""
                Layout.preferredWidth: root.inspectorWidth
                Layout.fillHeight: true
                tracksModel: tracksModel
                instrumentModel: instrumentModel
                navigationSection: navSection
                navigationOrderStart: 5
            }
        }
    }
}
