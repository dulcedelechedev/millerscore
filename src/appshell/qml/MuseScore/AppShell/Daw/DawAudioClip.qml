/* SPDX-License-Identifier: GPL-3.0-only */
pragma ComponentBehavior: Bound

import QtQuick
import Muse.Ui
import Muse.UiComponents

//! An audio clip on an audio track: its name and waveform. Drag it to move it along the timeline
//! (snapped to the grid, Shift moves freely); right-click removes it.
Item {
    id: clip

    DawTheme { id: dawTheme }

    required property var clipData // { id, name, startTick, endTick, seconds, peaks }
    property real pixelsPerTick: 0.1
    property real viewX: 0
    property real viewWidth: 0
    property color trackColor: dawTheme.brandAccent
    property bool muted: false
    property var snapTick: tick => tick

    signal moveRequested(int tick)
    signal removeRequested()

    property real dragTicks: 0
    readonly property real clipX: clipData.startTick * pixelsPerTick

    x: clipX + dragTicks * pixelsPerTick
    width: Math.max(6, (clipData.endTick - clipData.startTick) * pixelsPerTick)

    Rectangle {
        anchors.fill: parent
        radius: 3
        color: Qt.rgba(clip.trackColor.r, clip.trackColor.g, clip.trackColor.b, clip.muted ? 0.15 : 0.3)
        border.width: 1
        border.color: clip.trackColor
    }

    Rectangle {
        width: parent.width
        height: 16
        radius: 3
        color: clip.trackColor
        opacity: clip.muted ? 0.4 : 0.9
        StyledTextLabel {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            horizontalAlignment: Text.AlignLeft
            text: clip.clipData.name
            font.pixelSize: 10
            color: dawTheme.contrastingText(clip.trackColor)
            elide: Text.ElideRight
        }
    }

    // Waveform, drawn only for the visible part of the clip
    Canvas {
        id: waveform
        readonly property real visibleFrom: Math.max(0, clip.viewX - clip.x)
        readonly property real visibleTo: Math.min(clip.width, clip.viewX + clip.viewWidth - clip.x)
        x: visibleFrom
        y: 16
        width: Math.max(1, visibleTo - visibleFrom)
        height: parent.height - 16
        visible: visibleTo > visibleFrom
        onVisibleFromChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const peaks = clip.clipData.peaks
            if (!peaks || peaks.length === 0)
                return
            const middle = height / 2
            ctx.fillStyle = Qt.rgba(clip.trackColor.r, clip.trackColor.g, clip.trackColor.b, clip.muted ? 0.35 : 0.95)
            const perPixel = peaks.length / clip.width
            for (let px = 0; px < width; ++px) {
                const from = Math.floor((visibleFrom + px) * perPixel)
                const to = Math.max(from + 1, Math.floor((visibleFrom + px + 1) * perPixel))
                let peak = 0
                for (let index = from; index < to && index < peaks.length; ++index)
                    peak = Math.max(peak, peaks[index])
                const half = Math.max(0.5, peak * (middle - 1))
                ctx.fillRect(px, middle - half, 1, half * 2)
            }
        }
    }

    Connections {
        target: clip
        function onClipDataChanged() { waveform.requestPaint() }
        function onPixelsPerTickChanged() { waveform.requestPaint() }
        function onMutedChanged() { waveform.requestPaint() }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real pressSceneX: 0
        property bool moved: false

        onPressed: mouse => {
            pressSceneX = mapToItem(null, mouse.x, mouse.y).x
            moved = false
            clip.dragTicks = 0
        }
        onPositionChanged: mouse => {
            if (!pressed || !(pressedButtons & Qt.LeftButton))
                return
            const delta = (mapToItem(null, mouse.x, mouse.y).x - pressSceneX) / clip.pixelsPerTick
            if (!moved && Math.abs(delta * clip.pixelsPerTick) < 3)
                return
            moved = true
            const target = (mouse.modifiers & Qt.ShiftModifier) ? Math.round(clip.clipData.startTick + delta)
                                                                 : clip.snapTick(clip.clipData.startTick + delta)
            clip.dragTicks = Math.max(0, target) - clip.clipData.startTick
        }
        onReleased: mouse => {
            if (mouse.button === Qt.RightButton) {
                clip.removeRequested()
            } else if (moved && clip.dragTicks !== 0) {
                clip.moveRequested(clip.clipData.startTick + clip.dragTicks)
            }
            clip.dragTicks = 0
        }
        onCanceled: clip.dragTicks = 0
    }
}
