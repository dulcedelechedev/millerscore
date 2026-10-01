/* SPDX-License-Identifier: GPL-3.0-only */

import QtQuick
import Muse.Ui
import Muse.UiComponents

Rectangle {
    id: root

    property int iconCode: IconCode.NONE
    property string label: ""
    property bool active: false
    property bool emphasized: false

    implicitWidth: label.length > 0 ? 84 : 32
    implicitHeight: 32
    radius: 7
    color: emphasized ? "#C8922E" : active ? "#3B3B3F" : "transparent"
    border.width: active && !emphasized ? 1 : 0
    border.color: "#66666C"

    Row {
        anchors.centerIn: parent
        spacing: 7

        StyledIconLabel {
            anchors.verticalCenter: parent.verticalCenter
            iconCode: root.iconCode
            color: root.emphasized ? "#17130A" : ui.theme.fontPrimaryColor
            font.pixelSize: 15
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.label.length > 0
            text: root.label
            color: root.emphasized ? "#17130A" : ui.theme.fontPrimaryColor
            font.pixelSize: 11
            font.bold: root.emphasized
        }
    }
}
