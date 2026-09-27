/* SPDX-License-Identifier: GPL-3.0-only */
pragma ComponentBehavior: Bound

import QtQuick
import Muse.Ui

//! Semantic DAW tokens. Surfaces, text and lines come from the application
//! theme so Score and DAW look like one program; gold is reserved for the
//! playhead and brand accent.
QtObject {
    // Accents
    readonly property color brandAccent: "#D9A441"
    readonly property color playhead: "#F4C95D"
    readonly property color focus: ui.theme.focusColor
    readonly property color warning: "#E7A448"

    // Surfaces and lines
    readonly property color workspace: ui.theme.backgroundPrimaryColor
    readonly property color panel: ui.theme.backgroundSecondaryColor
    readonly property color line: ui.theme.strokeColor
    readonly property color gridMeasure: Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g, ui.theme.fontPrimaryColor.b, 0.22)
    readonly property color gridBeat: Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g, ui.theme.fontPrimaryColor.b, 0.08)
    readonly property color laneAlternate: Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g, ui.theme.fontPrimaryColor.b, 0.025)
    readonly property color laneSelected: Qt.rgba(ui.theme.accentColor.r, ui.theme.accentColor.g, ui.theme.accentColor.b, 0.10)
    readonly property color pitchRowBlack: Qt.rgba(0, 0, 0, 0.16)
    readonly property color pitchRowOctave: Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g, ui.theme.fontPrimaryColor.b, 0.14)

    // Notes: filled with the track color; selection uses a light outline so it
    // is never confused with the gold playhead.
    readonly property color noteSelectedOutline: ui.theme.fontPrimaryColor
    readonly property color noteBorder: Qt.rgba(0, 0, 0, 0.45)

    // DevTools Showroom (mock data only)
    readonly property color noteFill: "#C8922E"
    readonly property color selectedNoteBorder: "#FFF0B3"
    readonly property color selectionOutline: "#F4C95D"
    readonly property color trackSurface: ui.theme.backgroundSecondaryColor
    readonly property color trackSurfaceSelected: Qt.lighter(ui.theme.backgroundSecondaryColor, 1.08)
    readonly property color trackBorder: ui.theme.strokeColor
    readonly property color trackBorderSelected: Qt.lighter(ui.theme.strokeColor, 1.3)

    // Piano keyboard
    readonly property color pianoBlackKey: "#202022"
    readonly property color pianoWhiteKey: "#E3E3E5"
    readonly property color pianoBlackKeyText: "#B8B8BC"
    readonly property color pianoWhiteKeyText: "#55555A"

    // Metrics (8 px rhythm)
    readonly property int toolbarHeight: 40
    readonly property int headerHeight: 36
    readonly property int rulerHeight: 28
    readonly property int trackRowHeight: 64
    readonly property int spacing: 8

    //! Categorical track palette chosen to stay distinguishable for common
    //! color-vision deficiencies (Okabe-Ito based). Gold/yellow is kept last so
    //! it does not compete with the playhead on small scores.
    readonly property var trackPalette: ["#56B4E9", "#E69F00", "#2BB38B", "#CC79A7", "#4F8FD6", "#E0703A", "#A08FE8", "#9CC95D"]

    function trackColor(index) {
        return trackPalette[Math.max(0, index) % trackPalette.length]
    }

    function contrastingText(background) {
        const linearBrightness = 0.2126 * background.r + 0.7152 * background.g + 0.0722 * background.b;
        return linearBrightness > 0.5 ? Qt.rgba(0.09, 0.075, 0.04, 1) : Qt.rgba(0.96, 0.96, 0.97, 1);
    }

    function secondaryContrastingText(background) {
        const primary = contrastingText(background);
        return Qt.rgba(primary.r, primary.g, primary.b, 0.72);
    }
}
