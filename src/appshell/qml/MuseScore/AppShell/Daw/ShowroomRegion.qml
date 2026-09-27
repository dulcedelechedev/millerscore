/* SPDX-License-Identifier: GPL-3.0-only */

pragma ComponentBehavior: Bound

import QtQuick
import Muse.Ui

Rectangle {
    id: root
    DawTheme { id: dawTheme }

    required property string regionName
    required property color regionColor
    property bool selected: false
    property bool audioRegion: false
    property bool showContentPreview: true
    property string detailText: ""
    property var notePattern: [0.12, 0.34, 0.56, 0.28, 0.68, 0.44, 0.22, 0.51]

    radius: 6
    color: selected ? Qt.lighter(regionColor, 1.04) : Qt.darker(regionColor, 1.08)
    border.width: selected ? 2 : 1
    border.color: selected ? dawTheme.selectionOutline : Qt.lighter(regionColor, 1.28)
    clip: true

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 7
        anchors.rightMargin: 7
        anchors.topMargin: 7
        text: root.regionName
        elide: Text.ElideRight
        color: dawTheme.contrastingText(root.regionColor)
        font.pixelSize: 10
        font.bold: true
        z: 2
    }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 7
        anchors.rightMargin: 7
        anchors.bottomMargin: 7
        visible: root.detailText.length > 0
        text: root.detailText
        elide: Text.ElideRight
        color: dawTheme.secondaryContrastingText(root.regionColor)
        font.pixelSize: 8
        z: 2
    }

    Repeater {
        model: !root.showContentPreview ? 0 : root.audioRegion ? 44 : root.notePattern.length

        delegate: Rectangle {
            required property int index
            readonly property real amount: root.audioRegion ? 0.18 + Math.abs(Math.sin(index * 1.73)) * 0.62 : root.notePattern[index]
            x: root.audioRegion ? 8 + index * Math.max(2, (root.width - 16) / 44) : 9 + index * Math.max(12, (root.width - 26) / root.notePattern.length)
            y: root.audioRegion ? root.height * (0.58 - amount * 0.32) : 27 + amount * Math.max(6, root.height - 42)
            width: root.audioRegion ? 2 : Math.max(7, (root.width - 38) / root.notePattern.length)
            height: root.audioRegion ? Math.max(3, amount * root.height * 0.55) : 3
            radius: 1
            color: dawTheme.contrastingText(root.regionColor)
            opacity: 0.72
        }
    }
}
