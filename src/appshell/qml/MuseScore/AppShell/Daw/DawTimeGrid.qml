/* SPDX-License-Identifier: GPL-3.0-only */
pragma ComponentBehavior: Bound

import QtQuick
import Muse.Ui

//! Paints the musical grid for the visible viewport only, from the score's own
//! measure list (DawTransportModel.measures). Arranger, ruler and piano roll use
//! this one component, so bars, meters and pickups always line up.
Canvas {
    id: root

    DawTheme { id: dawTheme }

    property var measures: []
    property real pixelsPerTick: 0.1
    property real viewX: 0
    property bool ruler: false
    property bool drawBeats: true

    // Optional pitch rows (piano roll background).
    property bool drawPitchRows: false
    property real viewY: 0
    property real rowHeight: 14
    property int highestPitch: 84
    property int lowestPitch: 36

    function requestRepaint() { requestPaint() }

    onAvailableChanged: requestPaint()
    onMeasuresChanged: requestPaint()
    onPixelsPerTickChanged: requestPaint()
    onViewXChanged: requestPaint()
    onViewYChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onHighestPitchChanged: requestPaint()
    onLowestPitchChanged: requestPaint()

    Connections {
        target: ui.theme
        function onThemeChanged() { root.requestPaint() }
    }

    function firstVisibleMeasure(startTick) {
        let low = 0
        let high = measures.length - 1
        while (low < high) {
            const mid = Math.floor((low + high + 1) / 2)
            if (measures[mid].startTick <= startTick)
                low = mid
            else
                high = mid - 1
        }
        return Math.max(0, low)
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()

        if (drawPitchRows) {
            const firstRow = Math.max(0, Math.floor(viewY / rowHeight))
            const lastRow = Math.min(highestPitch - lowestPitch, Math.ceil((viewY + height) / rowHeight))
            for (let row = firstRow; row <= lastRow; ++row) {
                const pitch = highestPitch - row
                const y = row * rowHeight - viewY
                if ([1, 3, 6, 8, 10].indexOf(pitch % 12) >= 0) {
                    ctx.fillStyle = dawTheme.pitchRowBlack
                    ctx.fillRect(0, y, width, rowHeight)
                }
                if (pitch % 12 === 0) {
                    ctx.fillStyle = dawTheme.pitchRowOctave
                    ctx.fillRect(0, y + rowHeight - 1, width, 1)
                }
            }
        }

        if (!measures || measures.length === 0 || pixelsPerTick <= 0)
            return

        const startTick = viewX / pixelsPerTick
        const endTick = (viewX + width) / pixelsPerTick
        const labelFont = ui.theme.bodyFont.family
        ctx.font = "11px \"" + labelFont + "\""
        ctx.textBaseline = "top"

        // Skip labels that would collide; keep every n-th bar number.
        let labelEvery = 1
        const firstIndex = firstVisibleMeasure(startTick)
        if (ruler && measures.length > 0) {
            const typicalWidth = (measures[firstIndex].endTick - measures[firstIndex].startTick) * pixelsPerTick
            while (typicalWidth * labelEvery < 34 && labelEvery < 64)
                labelEvery *= 2
        }

        let previousSignature = ""
        if (firstIndex > 0)
            previousSignature = measures[firstIndex - 1].numerator + "/" + measures[firstIndex - 1].denominator

        for (let index = firstIndex; index < measures.length; ++index) {
            const measure = measures[index]
            if (measure.startTick > endTick)
                break

            const x = Math.round(measure.startTick * pixelsPerTick - viewX) + 0.5
            ctx.fillStyle = dawTheme.gridMeasure
            ctx.fillRect(x - 0.5, ruler ? height * 0.45 : 0, 1, ruler ? height * 0.55 : height)

            const beatWidth = measure.beatTicks * pixelsPerTick
            if (drawBeats && beatWidth >= 10) {
                ctx.fillStyle = dawTheme.gridBeat
                for (let beat = measure.startTick + measure.beatTicks; beat < measure.endTick; beat += measure.beatTicks) {
                    const beatX = Math.round(beat * pixelsPerTick - viewX)
                    ctx.fillRect(beatX, ruler ? height * 0.72 : 0, 1, ruler ? height * 0.28 : height)
                }
            }

            if (ruler) {
                const signature = measure.numerator + "/" + measure.denominator
                if (measure.label.length > 0 && index % labelEvery === 0) {
                    ctx.fillStyle = ui.theme.fontPrimaryColor
                    ctx.fillText(measure.label, x + 4, 3)
                }
                if (signature !== previousSignature) {
                    ctx.fillStyle = ui.theme.fontSecondaryColor
                    const labelWidth = measure.label.length > 0 ? ctx.measureText(measure.label).width + 10 : 4
                    ctx.fillText(signature, x + labelWidth, 3)
                }
                previousSignature = signature
            }
        }
    }
}
