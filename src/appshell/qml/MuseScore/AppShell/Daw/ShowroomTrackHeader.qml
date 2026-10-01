/* SPDX-License-Identifier: GPL-3.0-only */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Muse.Ui
import Muse.UiComponents

Rectangle {
    id: root
    DawTheme { id: dawTheme }

    required property string trackName
    required property string trackSubtitle
    required property color trackColor
    required property int iconCode
    property bool selected: false
    property bool muted: false
    property bool solo: false
    property bool armed: false
    property bool showRecordArm: true

    signal trackClicked()
    signal muteClicked()
    signal soloClicked()
    signal armClicked()

    color: selected ? dawTheme.trackSurfaceSelected : dawTheme.trackSurface
    border.color: selected ? dawTheme.trackBorderSelected : dawTheme.trackBorder

    MouseArea {
        anchors.fill: parent
        onClicked: root.trackClicked()
    }

    Rectangle {
        width: 4
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: root.trackColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 13
        anchors.rightMargin: 10
        spacing: 9

        Rectangle {
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            radius: 8
            color: Qt.darker(root.trackColor, 1.35)

            StyledIconLabel {
                anchors.centerIn: parent
                iconCode: root.iconCode
                color: dawTheme.contrastingText(root.trackColor)
                font.pixelSize: 16
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: root.trackName
                elide: Text.ElideRight
                color: ui.theme.fontPrimaryColor
                font.pixelSize: 12
                font.bold: true
            }

            Text {
                Layout.fillWidth: true
                text: root.trackSubtitle
                elide: Text.ElideRight
                color: ui.theme.fontSecondaryColor
                font.pixelSize: 9
            }
        }

        Row {
            spacing: 4

            Repeater {
                model: root.showRecordArm ? [
                    {
                        "label": "M",
                        "active": root.muted,
                        "action": "mute",
                        "hint": qsTrc("appshell", "Mute")
                    },
                    {
                        "label": "S",
                        "active": root.solo,
                        "action": "solo",
                        "hint": qsTrc("appshell", "Solo")
                    },
                    {
                        "label": "R",
                        "active": root.armed,
                        "action": "arm",
                        "hint": qsTrc("appshell", "Record enable")
                    }
                ] : [
                    {
                        "label": "M",
                        "active": root.muted,
                        "action": "mute",
                        "hint": qsTrc("appshell", "Mute")
                    },
                    {
                        "label": "S",
                        "active": root.solo,
                        "action": "solo",
                        "hint": qsTrc("appshell", "Solo")
                    }
                ]

                delegate: FlatButton {
                    required property var modelData
                    width: 28
                    height: 28
                    minWidth: 28
                    margins: 0
                    text: modelData.label
                    accentButton: modelData.active
                    accentColor: root.trackColor
                    toolTipTitle: modelData.hint
                    onClicked: {
                        if (modelData.action === "mute")
                            root.muteClicked()
                        else if (modelData.action === "solo")
                            root.soloClicked()
                        else
                            root.armClicked()
                    }
                }
            }
        }
    }
}
