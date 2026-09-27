/* SPDX-License-Identifier: GPL-3.0-only */
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Muse.Ui
import Muse.UiComponents

//! Header of one score-derived DAW track. All state comes from DawTracksModel;
//! Mute and Solo are the same controls the Mixer changes.
Rectangle {
    id: root

    DawTheme { id: dawTheme }

    required property string trackName
    required property string soundName
    required property color trackColor
    property bool selected: false
    property bool muted: false
    property bool solo: false

    property NavigationPanel navigationPanel: null
    property int navigationRow: 0

    //! `modifiers`: Ctrl adds or removes the track, Shift selects a range of tracks
    signal selectRequested(int modifiers)
    signal muteRequested()
    signal soloRequested()

    color: selected ? Qt.tint(dawTheme.panel, Qt.rgba(ui.theme.accentColor.r, ui.theme.accentColor.g, ui.theme.accentColor.b, 0.22))
                    : dawTheme.panel

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: dawTheme.line
    }

    MouseArea {
        anchors.fill: parent
        onClicked: mouse => {
            trackNavCtrl.requestActiveByInteraction()
            root.selectRequested(mouse.modifiers)
        }
    }

    NavigationControl {
        id: trackNavCtrl
        name: "DawTrackHeader"
        enabled: root.enabled && root.visible
        panel: root.navigationPanel
        row: root.navigationRow
        column: 0
        accessible.role: MUAccessible.Button
        accessible.name: qsTrc("appshell", "Select track %1").arg(root.trackName)
        accessible.visualItem: root
        onTriggered: root.selectRequested(Qt.NoModifier)
    }

    NavigationFocusBorder {
        navigationCtrl: trackNavCtrl
        drawOutsideParent: false
    }

    Rectangle {
        width: root.selected ? 6 : 3
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: root.trackColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 8
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            StyledTextLabel {
                Layout.fillWidth: true
                text: root.trackName
                horizontalAlignment: Text.AlignLeft
                font: ui.theme.bodyBoldFont
                opacity: root.muted ? 0.6 : 1
                displayTruncatedTextOnHover: true
            }

            StyledTextLabel {
                Layout.fillWidth: true
                text: root.soundName.length > 0 ? root.soundName : qsTrc("appshell", "No sound assigned")
                horizontalAlignment: Text.AlignLeft
                color: ui.theme.fontSecondaryColor
                font: ui.theme.bodyFont
                displayTruncatedTextOnHover: true
            }
        }

        FlatButton {
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            minWidth: 28
            margins: 0
            icon: IconCode.MUTE
            accentButton: root.muted
            toolTipTitle: root.muted ? qsTrc("appshell", "Unmute") : qsTrc("appshell", "Mute")
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRow
            navigation.column: 1
            navigation.accessible.name: qsTrc("appshell", "Mute %1").arg(root.trackName)
            onClicked: root.muteRequested()
        }

        FlatButton {
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            minWidth: 28
            margins: 0
            icon: IconCode.SOLO
            accentButton: root.solo
            toolTipTitle: root.solo ? qsTrc("appshell", "Unsolo") : qsTrc("appshell", "Solo")
            navigation.panel: root.navigationPanel
            navigation.row: root.navigationRow
            navigation.column: 2
            navigation.accessible.name: qsTrc("appshell", "Solo %1").arg(root.trackName)
            onClicked: root.soloRequested()
        }
    }
}
