/* SPDX-License-Identifier: GPL-3.0-only */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Muse.Ui
import Muse.UiComponents

Rectangle {
    id: root

    color: "#262628"
    border.color: "#3A3A3C"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 13

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTrc("appshell", "REGION INSPECTOR")
                color: "#A7A7AC"
                font.pixelSize: 10
                font.bold: true
                font.letterSpacing: 0.7
            }
            Item { Layout.fillWidth: true }
            StyledIconLabel { iconCode: IconCode.SETTINGS_COG; color: "#A7A7AC"; font.pixelSize: 14 }
        }

        Text {
            text: qsTrc("appshell", "Midnight Keys")
            color: "#F1F1F2"
            font.pixelSize: 15
            font.bold: true
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#3A3A3C" }

        Repeater {
            model: [
                { "label": qsTrc("appshell", "Position"), "value": "9  1  1" },
                { "label": qsTrc("appshell", "Length"), "value": "4  0  0" },
                { "label": qsTrc("appshell", "Transpose"), "value": "+0 st" },
                { "label": qsTrc("appshell", "Velocity"), "value": "88" }
            ]

            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Text { text: parent.modelData.label; color: "#A7A7AC"; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
                Rectangle {
                    Layout.preferredWidth: 76
                    Layout.preferredHeight: 25
                    radius: 5
                    color: "#1E1E20"
                    border.color: "#45454A"
                    Text { anchors.centerIn: parent; text: parent.parent.modelData.value; color: "#E3E3E5"; font.pixelSize: 11 }
                }
            }
        }

        Text { text: qsTrc("appshell", "Gain"); color: "#A7A7AC"; font.pixelSize: 11 }
        Slider {
            Layout.fillWidth: true
            from: -24
            to: 12
            value: -2.5
        }

        Text { text: qsTrc("appshell", "Quantize strength"); color: "#A7A7AC"; font.pixelSize: 11 }
        Slider {
            Layout.fillWidth: true
            from: 0
            to: 100
            value: 76
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#3A3A3C" }

        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTrc("appshell", "Humanize"); color: "#D8D8DA"; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            StyledIconLabel { iconCode: IconCode.TICK; color: "#F4C95D"; font.pixelSize: 13 }
        }

        Item { Layout.fillHeight: true }
    }
}
