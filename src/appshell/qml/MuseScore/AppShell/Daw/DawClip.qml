/* SPDX-License-Identifier: GPL-3.0-only */
pragma ComponentBehavior: Bound

import QtQuick
import Muse.Ui
import Muse.UiComponents

//! A score-linked clip: the bars in which a part actually has notes. Its note
//! preview is painted by the lane (DawLaneNotes) so long clips stay cheap.
Rectangle {
    id: root

    DawTheme { id: dawTheme }

    required property color trackColor
    property string label: ""
    property bool selected: false
    property bool muted: false

    radius: 4
    color: Qt.rgba(trackColor.r, trackColor.g, trackColor.b, muted ? 0.12 : (selected ? 0.34 : 0.24))
    // Selection is shown by shape (outline), not only by color.
    border.width: selected ? 2 : 1
    border.color: selected ? dawTheme.noteSelectedOutline : Qt.rgba(trackColor.r, trackColor.g, trackColor.b, 0.55)
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 16
        radius: root.radius
        color: Qt.rgba(root.trackColor.r, root.trackColor.g, root.trackColor.b, root.muted ? 0.25 : 0.5)

        StyledTextLabel {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            text: root.label
            horizontalAlignment: Text.AlignLeft
            color: ui.theme.fontPrimaryColor
            font.pixelSize: 10
        }
    }
}
