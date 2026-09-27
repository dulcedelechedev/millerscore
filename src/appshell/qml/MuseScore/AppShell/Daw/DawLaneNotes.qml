/* SPDX-License-Identifier: GPL-3.0-only */
pragma ComponentBehavior: Bound

import QtQuick
import Muse.Ui

//! Paints the real score notes of one lane as a compact preview. It covers the
//! visible viewport only; each clip's notes are scaled to that clip's own pitch
//! range so the melodic contour stays readable in a short lane.
Canvas {
    id: root

    property var clips: []
    property color noteColor: "white"
    property real pixelsPerTick: 0.1
    property real viewX: 0
    property real topInset: 18
    property real bottomInset: 4

    // A canvas ignores paint requests until it is available.
    onAvailableChanged: requestPaint()
    onClipsChanged: requestPaint()
    onPixelsPerTickChanged: requestPaint()
    onViewXChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onNoteColorChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        if (!clips || clips.length === 0 || pixelsPerTick <= 0 || width <= 0)
            return

        const startTick = viewX / pixelsPerTick
        const endTick = (viewX + width) / pixelsPerTick
        const usableHeight = Math.max(4, height - topInset - bottomInset)
        ctx.fillStyle = noteColor

        for (let c = 0; c < clips.length; ++c) {
            const clip = clips[c]
            if (clip.endTick < startTick || clip.startTick > endTick)
                continue
            const span = Math.max(12, clip.maxPitch - clip.minPitch)
            const barHeight = Math.max(2, Math.min(4, usableHeight / span))
            // Flat triples: offset from clip start, duration, pitch.
            const notes = clip.notes
            const center = (clip.maxPitch + clip.minPitch) / 2
            for (let n = 0; n + 2 < notes.length; n += 3) {
                const tick = clip.startTick + notes[n]
                const duration = notes[n + 1]
                if (tick > endTick || tick + duration < startTick)
                    continue
                const x = tick * pixelsPerTick - viewX
                const w = Math.max(2, duration * pixelsPerTick - 1)
                const normalized = (notes[n + 2] - center) / span
                const y = topInset + (usableHeight - barHeight) * (0.5 - normalized)
                ctx.fillRect(x, y, w, barHeight)
            }
        }
    }
}
