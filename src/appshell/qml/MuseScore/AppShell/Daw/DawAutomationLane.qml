/* SPDX-License-Identifier: GPL-3.0-only */
pragma ComponentBehavior: Bound

import QtQuick
import Muse.Ui
import Muse.UiComponents

//! One automation curve (channel volume, pan, pitch or a MIDI CC), edited the way Logic Pro edits
//! automation: a line through the points, holding the first value before it and the last one after.
//! - Click the line to add a point there; double-click empty space to add one at the pointer.
//! - Click a point to select it (Shift adds or removes); drag across the lane to select the points in
//!   that time range (Shift adds). Selected points and the line between them turn white.
//! - Drag a selected point to move the whole selection: the first movement locks the axis (time or
//!   value); Ctrl moves the value finely; time follows the grid.
//! - Ctrl+Shift+drag a segment bends it into a curve; Ctrl+Alt+Shift+click makes it a step (or a line
//!   again).
//! - Double-click a point to delete it; Delete removes the selection.
//! Every gesture is one undo step (editRequested carries all of its point edits).
Item {
    id: lane

    DawTheme { id: dawTheme }

    //! { tick, value, arrival, shape: "linear" | "curve" | "step", bend, bendTime } sorted by tick
    property var points: []
    property real pixelsPerTick: 0.1
    property real viewX: 0
    property int snapTicks: 120
    property var transport: null
    property bool bipolar: false
    property real defaultValue: 0.5
    property color lineColor: dawTheme.brandAccent
    //! function(normalizedValue) -> display text
    property var valueText: function(value) { return String(Math.round(value * 127)) }

    signal editRequested(var edits)

    readonly property real topPad: 8
    readonly property real availableHeight: Math.max(24, height - 34)
    readonly property int selectedCount: selectedTicks.length

    property var selectedTicks: []
    readonly property var selectedTickLookup: {
        const lookup = Object.create(null)
        for (let index = 0; index < selectedTicks.length; ++index)
            lookup[selectedTicks[index]] = true
        return lookup
    }
    //! Drag preview, applied to the selected points until release
    property int dragMode: 0 // 0 none, 1 undecided, 2 value, 3 time, 4 bend, 5 range
    property real dragValueDelta: 0
    property int dragTickDelta: 0
    property real bendPreview: -1
    property int bendTick: -1
    property real rangeFromX: 0
    property real rangeToX: 0
    property int hoveredTick: -1

    function xForTick(tick) { return tick * pixelsPerTick - viewX }
    function tickForX(x) { return (x + viewX) / pixelsPerTick }
    function yForValue(value) { return topPad + (1 - value) * availableHeight }
    function valueForY(y) { return Math.max(0, Math.min(1, 1 - (y - topPad) / availableHeight)) }
    function snapped(tick) {
        return Math.max(0, transport ? transport.snapTick(Math.round(tick), snapTicks, true) : Math.round(tick))
    }
    function isSelected(tick) { return selectedTickLookup[tick] === true }

    function clearSelection() {
        selectedTicks = []
    }

    function cancelDrag() {
        dragMode = 0
        dragValueDelta = 0
        dragTickDelta = 0
        bendPreview = -1
        bendTick = -1
        canvas.requestPaint()
    }

    //! Same incoming segment as muse::mpe::evaluateAt, including saved bend time.
    function evaluateSegment(prevOut, arrival, shape, bend, t, bendTime) {
        const range = arrival - prevOut
        const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v))
        const et = bendTime === undefined ? 0.5 : bendTime
        if (shape !== "curve" || et <= 0 || et >= 1)
            return clamp(prevOut + range * t, 0, 1)
        const lo = Math.min(prevOut, arrival)
        const hi = Math.max(prevOut, arrival)
        const bendValue = prevOut + clamp(bend, 0, 1) * range
        const halfSlope = 0.5 * range
        const bezier = (s, p0, p1, p2) => (1 - s) * (1 - s) * p0 + 2 * (1 - s) * s * p1 + s * s * p2
        if (t <= et)
            return clamp(bezier(t / et, prevOut, clamp(bendValue - et * halfSlope, lo, hi), bendValue), 0, 1)
        return clamp(bezier((t - et) / (1 - et), bendValue, clamp(bendValue + (1 - et) * halfSlope, lo, hi), arrival), 0, 1)
    }

    //! The points as they look right now, with the drag preview applied
    function shownPoints() {
        const result = []
        for (let index = 0; index < points.length; ++index) {
            const point = points[index]
            const selected = isSelected(point.tick)
            let tick = point.tick
            let value = Number(point.value)
            let arrival = point.arrival === undefined ? value : Number(point.arrival)
            let bend = Number(point.bend)
            if (selected && dragMode === 2) {
                value = Math.max(0, Math.min(1, value + dragValueDelta))
                arrival = Math.max(0, Math.min(1, arrival + dragValueDelta))
            }
            if (selected && dragMode === 3)
                tick = Math.max(0, tick + dragTickDelta)
            if (dragMode === 4 && point.tick === bendTick && bendPreview >= 0)
                bend = bendPreview
            result.push({ tick: tick, value: value, arrival: arrival, shape: point.shape,
                          shapeShown: (dragMode === 4 && point.tick === bendTick) ? "curve" : point.shape,
                          bend: bend, bendTime: point.bendTime === undefined ? 0.5 : Number(point.bendTime),
                          selected: selected, sourceTick: point.tick })
        }
        result.sort((a, b) => a.tick - b.tick)
        return result
    }

    //! The value the curve has at `tick` (for adding a point on the line)
    function valueAtTick(tick) {
        const shown = shownPoints()
        if (shown.length === 0)
            return Math.max(0, Math.min(1, defaultValue))
        if (tick <= shown[0].tick)
            return shown[0].value
        for (let index = 1; index < shown.length; ++index) {
            const prev = shown[index - 1]
            const next = shown[index]
            if (tick === next.tick)
                return next.value
            if (tick <= next.tick) {
                if (next.shape === "step")
                    return prev.value
                return evaluateSegment(prev.value, next.arrival, next.shapeShown, next.bend,
                                       (tick - prev.tick) / Math.max(1, next.tick - prev.tick), next.bendTime)
            }
        }
        return shown[shown.length - 1].value
    }

    //! Nearest point within `radius` px of (x, y), or null
    function pointAt(x, y, radius) {
        let best = null
        let bestDistance = radius
        for (let index = 0; index < points.length; ++index) {
            const point = points[index]
            const distance = Math.hypot(xForTick(point.tick) - x, yForValue(point.value) - y)
            if (distance <= bestDistance) {
                best = point
                bestDistance = distance
            }
        }
        return best
    }

    //! The point that ends the segment under x (the one whose shape the segment takes), or null
    function segmentEndAt(x) {
        const tick = tickForX(x)
        for (let index = 1; index < points.length; ++index) {
            if (tick > points[index - 1].tick && tick < points[index].tick)
                return { end: points[index], start: points[index - 1] }
        }
        return null
    }

    function pointEdit(point, overrides) {
        const edit = { op: "set", tick: point.tick, value: Number(point.value),
                       arrival: point.arrival === undefined ? Number(point.value) : Number(point.arrival),
                       shape: point.shape, bend: Number(point.bend),
                       bendTime: point.bendTime === undefined ? 0.5 : Number(point.bendTime) }
        for (const key in overrides)
            edit[key] = overrides[key]
        if (point.shape === "step" && edit.shape !== "step" && overrides.arrival === undefined)
            edit.arrival = edit.value
        return edit
    }

    function commitValueDrag() {
        const edits = []
        for (let index = 0; index < points.length; ++index) {
            const point = points[index]
            if (isSelected(point.tick)) {
                const arrival = point.arrival === undefined ? Number(point.value) : Number(point.arrival)
                edits.push(pointEdit(point, {
                    value: Math.max(0, Math.min(1, Number(point.value) + dragValueDelta)),
                    arrival: Math.max(0, Math.min(1, arrival + dragValueDelta))
                }))
            }
        }
        if (edits.length > 0)
            editRequested(edits)
    }

    function commitTimeDrag() {
        const moving = points.filter(point => isSelected(point.tick))
        // Moving right, the rightmost point moves first (and the other way round), so a moved point never
        // lands on one of the selection that has not moved yet.
        moving.sort((a, b) => dragTickDelta > 0 ? b.tick - a.tick : a.tick - b.tick)
        const edits = []
        const newTicks = []
        for (let index = 0; index < moving.length; ++index) {
            const point = moving[index]
            const target = Math.max(0, point.tick + dragTickDelta)
            newTicks.push(target)
            if (target !== point.tick)
                edits.push(pointEdit(point, { op: "move", tick: target, from: point.tick }))
        }
        if (edits.length > 0)
            editRequested(edits)
        selectedTicks = newTicks
    }

    function deleteSelection() {
        const edits = selectedTicks.map(tick => ({ op: "erase", tick: tick }))
        selectedTicks = []
        if (edits.length > 0)
            editRequested(edits)
        return edits.length > 0
    }

    //! Segment shape for the selected points (the segments arriving at them)
    function applyShape(shape) {
        const edits = []
        for (let index = 0; index < points.length; ++index) {
            const point = points[index]
            if (isSelected(point.tick) && point.shape !== shape)
                edits.push(pointEdit(point, { shape: shape, bend: shape === "curve" ? 0.3 : Number(point.bend), bendTime: 0.5 }))
        }
        if (edits.length > 0)
            editRequested(edits)
    }

    function addPoint(tick, value) {
        const target = snapped(tick)
        editRequested([{ op: "set", tick: target, value: value, shape: "linear", bend: 0.5 }])
        selectedTicks = [target]
    }

    onPointsChanged: {
        // Forget selected points that no longer exist (undo, reset)
        const existing = {}
        for (let index = 0; index < points.length; ++index)
            existing[points[index].tick] = true
        const kept = selectedTicks.filter(tick => existing[tick])
        if (kept.length !== selectedTicks.length)
            selectedTicks = kept
        canvas.requestPaint()
    }
    onSelectedTicksChanged: canvas.requestPaint()
    onViewXChanged: canvas.requestPaint()
    onPixelsPerTickChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onDragValueDeltaChanged: canvas.requestPaint()
    onDragTickDeltaChanged: canvas.requestPaint()
    onBendPreviewChanged: canvas.requestPaint()
    onLineColorChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const shown = lane.shownPoints()
            if (shown.length === 0)
                return

            const x = tick => lane.xForTick(tick)
            const y = value => lane.yForValue(value)

            // Every segment as a list of [x, y], so it can be stroked twice (all, then the selected ones)
            const segments = []
            segments.push({ path: [[0, y(shown[0].value)], [x(shown[0].tick), y(shown[0].value)]], selected: false })
            for (let index = 1; index < shown.length; ++index) {
                const prev = shown[index - 1]
                const next = shown[index]
                const x0 = x(prev.tick)
                const x1 = x(next.tick)
                if (x1 < 0 || x0 > width)
                    continue
                const path = [[x0, y(prev.value)]]
                if (next.shapeShown === "step") {
                    path.push([x1, y(prev.value)])
                } else if (next.shapeShown === "curve") {
                    const steps = Math.max(8, Math.min(48, Math.round((x1 - x0) / 4)))
                    for (let step = 1; step <= steps; ++step)
                        path.push([x0 + (x1 - x0) * step / steps,
                                   y(lane.evaluateSegment(prev.value, next.arrival, "curve", next.bend, step / steps, next.bendTime))])
                } else {
                    path.push([x1, y(next.arrival)])
                }
                path.push([x1, y(next.value)])
                segments.push({ path: path, selected: prev.selected && next.selected })
            }
            const last = shown[shown.length - 1]
            segments.push({ path: [[x(last.tick), y(last.value)], [width, y(last.value)]], selected: false })

            // A light fill under the curve, from the baseline
            const baseline = lane.bipolar ? y(0.5) : y(0)
            ctx.beginPath()
            ctx.moveTo(0, baseline)
            for (const segment of segments)
                for (const point of segment.path)
                    ctx.lineTo(point[0], point[1])
            ctx.lineTo(width, baseline)
            ctx.closePath()
            ctx.fillStyle = Qt.rgba(lane.lineColor.r, lane.lineColor.g, lane.lineColor.b, 0.12)
            ctx.fill()

            ctx.lineJoin = "round"
            for (const pass of [false, true]) {
                ctx.beginPath()
                for (const segment of segments) {
                    if (segment.selected !== pass)
                        continue
                    ctx.moveTo(segment.path[0][0], segment.path[0][1])
                    for (let index = 1; index < segment.path.length; ++index)
                        ctx.lineTo(segment.path[index][0], segment.path[index][1])
                }
                ctx.lineWidth = pass ? 2.5 : 2
                ctx.strokeStyle = pass ? "#FFFFFF" : lane.lineColor
                ctx.stroke()
            }
        }
    }

    // The selected range, as a grey bar along the top (as in Logic)
    Rectangle {
        readonly property var ticks: lane.selectedTicks.map(tick => tick + (lane.dragMode === 3 ? lane.dragTickDelta : 0))
        visible: lane.selectedCount > 1
        x: lane.xForTick(Math.min.apply(null, ticks.length ? ticks : [0]))
        width: Math.max(2, lane.xForTick(Math.max.apply(null, ticks.length ? ticks : [0])) - x)
        y: 1
        height: 5
        radius: 2
        color: ui.theme.fontSecondaryColor
        opacity: 0.6
    }

    // Range being selected
    Rectangle {
        visible: lane.dragMode === 5
        x: Math.min(lane.rangeFromX, lane.rangeToX)
        width: Math.abs(lane.rangeToX - lane.rangeFromX)
        height: parent.height
        color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1
        border.color: ui.theme.fontSecondaryColor
    }

    // Points and their values
    Repeater {
        model: lane.points
        delegate: Item {
            id: pointItem
            required property var modelData
            required property int index
            readonly property bool selected: lane.isSelected(modelData.tick)
            readonly property real shownValue: selected && lane.dragMode === 2
                                                ? Math.max(0, Math.min(1, Number(modelData.value) + lane.dragValueDelta))
                                                : Number(modelData.value)
            readonly property int shownTick: selected && lane.dragMode === 3 ? Math.max(0, modelData.tick + lane.dragTickDelta)
                                                                              : modelData.tick
            readonly property real nextX: index + 1 < lane.points.length ? lane.xForTick(lane.points[index + 1].tick) : 1e9
            readonly property bool roomForLabel: nextX - lane.xForTick(modelData.tick) >= 46 && lane.height >= 70
            readonly property bool emphasized: selected || lane.hoveredTick === modelData.tick

            x: lane.xForTick(shownTick) - 5
            y: lane.yForValue(shownValue) - 5
            width: 10
            height: 10
            visible: x > -60 && x < lane.width + 10
            z: emphasized ? 3 : 2

            Rectangle {
                anchors.centerIn: parent
                width: pointItem.emphasized ? 10 : 8
                height: width
                radius: width / 2
                color: pointItem.selected ? "#FFFFFF" : lane.lineColor
                border.width: 1
                border.color: pointItem.selected ? lane.lineColor : Qt.darker(lane.lineColor, 1.6)
            }

            StyledTextLabel {
                visible: pointItem.roomForLabel || pointItem.emphasized
                x: 12
                y: pointItem.shownValue > 0.8 ? 8 : -14
                text: lane.valueText(pointItem.shownValue)
                color: pointItem.selected ? ui.theme.fontPrimaryColor : ui.theme.fontSecondaryColor
                font.pixelSize: 10
            }
        }
    }

    MouseArea {
        id: input
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton

        property real pressX: 0
        property real pressY: 0
        property int pressModifiers: 0
        property var pressedPoint: null
        property bool clearOnRelease: false

        cursorShape: lane.dragMode === 3 ? Qt.SizeHorCursor
                     : lane.dragMode === 2 || lane.dragMode === 4 ? Qt.SizeVerCursor
                     : (lane.hoveredTick >= 0 ? Qt.PointingHandCursor : Qt.CrossCursor)

        onPositionChanged: mouse => {
            if (!pressed) {
                const hovered = lane.pointAt(mouse.x, mouse.y, 7)
                lane.hoveredTick = hovered ? hovered.tick : -1
                return
            }
            const dx = mouse.x - pressX
            const dy = mouse.y - pressY
            if (lane.dragMode === 1) {
                if (Math.abs(dx) < 3 && Math.abs(dy) < 3)
                    return
                // The first movement decides: along time or along value (as in Logic)
                lane.dragMode = Math.abs(dx) > Math.abs(dy) ? 3 : 2
            }
            if (lane.dragMode === 2) {
                const fine = (mouse.modifiers & Qt.ControlModifier) ? 0.25 : 1
                lane.dragValueDelta = -dy / lane.availableHeight * fine
            } else if (lane.dragMode === 3 && pressedPoint) {
                lane.dragTickDelta = lane.snapped(pressedPoint.tick + dx / lane.pixelsPerTick) - pressedPoint.tick
            } else if (lane.dragMode === 4) {
                const segment = pressedPoint
                const arrival = segment.end.arrival === undefined ? Number(segment.end.value) : Number(segment.end.arrival)
                const rise = arrival - Number(segment.start.value)
                if (Math.abs(rise) > 1e-4)
                    lane.bendPreview = Math.max(0.02, Math.min(0.98, (lane.valueForY(mouse.y) - Number(segment.start.value)) / rise))
            } else if (lane.dragMode === 6 && Math.abs(dx) > 3) {
                lane.dragMode = 5
                lane.rangeFromX = pressX
            }
            if (lane.dragMode === 5)
                lane.rangeToX = mouse.x
        }

        onPressed: mouse => {
            lane.forceActiveFocus()
            pressX = mouse.x
            pressY = mouse.y
            pressModifiers = mouse.modifiers
            clearOnRelease = false
            pressedPoint = null
            const shift = (mouse.modifiers & Qt.ShiftModifier) !== 0
            const ctrl = (mouse.modifiers & Qt.ControlModifier) !== 0
            const alt = (mouse.modifiers & Qt.AltModifier) !== 0

            const point = lane.pointAt(mouse.x, mouse.y, 7)
            if (point) {
                if (shift) {
                    const ticks = lane.selectedTicks.slice()
                    const at = ticks.indexOf(point.tick)
                    if (at >= 0)
                        ticks.splice(at, 1)
                    else
                        ticks.push(point.tick)
                    lane.selectedTicks = ticks
                    lane.dragMode = 0
                    return
                }
                if (!lane.isSelected(point.tick))
                    lane.selectedTicks = [point.tick]
                pressedPoint = point
                lane.dragMode = 1
                return
            }

            const segment = lane.segmentEndAt(mouse.x)
            const onLine = Math.abs(lane.yForValue(lane.valueAtTick(lane.tickForX(mouse.x))) - mouse.y) <= 6
            if (segment && onLine && ctrl && shift && alt) {
                // Toggle the segment between a step and a line
                const end = segment.end
                lane.editRequested([lane.pointEdit(end, { shape: end.shape === "step" ? "linear" : "step" })])
                lane.dragMode = 0
                return
            }
            if (segment && onLine && ctrl && shift) {
                pressedPoint = segment
                lane.bendTick = segment.end.tick
                lane.bendPreview = Number(segment.end.bend)
                lane.dragMode = 4
                return
            }
            if (onLine && lane.points.length > 0 && !ctrl && !shift) {
                // Click on the line: a point there, on the curve, ready to drag
                const tick = lane.snapped(lane.tickForX(mouse.x))
                lane.addPoint(tick, lane.valueAtTick(tick))
                pressedPoint = { tick: tick, value: lane.valueAtTick(tick) }
                lane.dragMode = 1
                return
            }

            // Empty space: a range selection when dragged, a deselect when clicked
            lane.dragMode = 6
            clearOnRelease = !shift
        }

        onReleased: mouse => {
            if (lane.dragMode === 2 && Math.abs(lane.dragValueDelta) > 1e-4) {
                lane.commitValueDrag()
            } else if (lane.dragMode === 3 && lane.dragTickDelta !== 0) {
                lane.commitTimeDrag()
            } else if (lane.dragMode === 4 && pressedPoint && lane.bendPreview >= 0) {
                lane.editRequested([lane.pointEdit(pressedPoint.end, { shape: "curve", bend: lane.bendPreview })])
            } else if (lane.dragMode === 5) {
                const from = lane.tickForX(Math.min(lane.rangeFromX, lane.rangeToX))
                const to = lane.tickForX(Math.max(lane.rangeFromX, lane.rangeToX))
                const ticks = (pressModifiers & Qt.ShiftModifier) ? lane.selectedTicks.slice() : []
                const tickLookup = Object.create(null)
                for (let index = 0; index < ticks.length; ++index)
                    tickLookup[ticks[index]] = true
                for (let index = 0; index < lane.points.length; ++index) {
                    const tick = lane.points[index].tick
                    if (tick >= from && tick <= to && !tickLookup[tick]) {
                        ticks.push(tick)
                        tickLookup[tick] = true
                    }
                }
                lane.selectedTicks = ticks
            } else if (lane.dragMode === 6 && clearOnRelease) {
                lane.clearSelection()
            }
            lane.cancelDrag()
            pressedPoint = null
        }

        onCanceled: {
            lane.cancelDrag()
            pressedPoint = null
        }

        onDoubleClicked: mouse => {
            const point = lane.pointAt(mouse.x, mouse.y, 7)
            if (point) {
                lane.selectedTicks = lane.selectedTicks.filter(tick => tick !== point.tick)
                lane.editRequested([{ op: "erase", tick: point.tick }])
            } else {
                lane.addPoint(lane.tickForX(mouse.x), lane.valueForY(mouse.y))
            }
        }

        onExited: lane.hoveredTick = -1
    }

    // Value and position while dragging
    Rectangle {
        visible: (lane.dragMode === 2 || lane.dragMode === 3 || lane.dragMode === 4) && input.pressedPoint !== null
        x: Math.min(lane.width - width - 4, Math.max(4, input.mouseX + 12))
        y: 2
        width: dragReadout.implicitWidth + 12
        height: 20
        radius: 3
        color: ui.theme.popupBackgroundColor
        border.color: dawTheme.line
        z: 10

        StyledTextLabel {
            id: dragReadout
            anchors.centerIn: parent
            font.pixelSize: 10
            text: {
                const point = input.pressedPoint
                if (!point)
                    return ""
                if (lane.dragMode === 4)
                    return qsTrc("appshell", "Curve")
                const tick = lane.dragMode === 3 ? Math.max(0, point.tick + lane.dragTickDelta) : point.tick
                const value = lane.dragMode === 2 ? Math.max(0, Math.min(1, Number(point.value) + lane.dragValueDelta))
                                                  : Number(point.value)
                const where = lane.transport ? lane.transport.positionTextForTick(tick) : String(tick)
                const count = lane.selectedCount > 1 ? "  ·  " + qsTrc("appshell", "%1 points").arg(lane.selectedCount) : ""
                return where + "  " + lane.valueText(value) + count
            }
        }
    }

    Keys.onDeletePressed: event => { event.accepted = lane.deleteSelection() }
    Keys.onEscapePressed: event => {
        if (lane.dragMode !== 0) {
            lane.cancelDrag()
            event.accepted = true
        } else if (lane.selectedCount > 0) {
            lane.clearSelection()
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
}
