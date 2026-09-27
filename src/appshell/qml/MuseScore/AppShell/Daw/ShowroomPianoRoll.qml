/* SPDX-License-Identifier: GPL-3.0-only */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Muse.Ui
import Muse.UiComponents

Rectangle {
    id: root

    readonly property int keyWidth: 72
    readonly property int rulerHeight: 25
    readonly property int rowHeight: 12
    readonly property color noteColor: "#D9A441"

    color: "#1E1E20"
    border.color: "#3A3A3C"

    Rectangle {
        id: titleBar
        width: parent.width
        height: 35
        color: "#29292C"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 11
            anchors.rightMargin: 11
            spacing: 9

            StyledIconLabel { iconCode: IconCode.MUSIC_NOTES; color: "#D9A441"; font.pixelSize: 15 }
            Text { text: qsTrc("appshell", "Piano Roll"); color: "#F1F1F2"; font.pixelSize: 12; font.bold: true }
            Text { text: qsTrc("appshell", "Midnight Keys · bars 9–13"); color: "#8F8F94"; font.pixelSize: 10 }
            Item { Layout.fillWidth: true }
            Text { text: qsTrc("appshell", "1/16"); color: "#B8B8BC"; font.pixelSize: 10 }
            StyledIconLabel { iconCode: IconCode.FIT_SELECTION; color: "#B8B8BC"; font.pixelSize: 14 }
        }
    }

    Item {
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: velocityLane.top

        Rectangle {
            x: root.keyWidth
            width: parent.width - root.keyWidth
            height: root.rulerHeight
            color: "#242426"

            Repeater {
                model: 5
                delegate: Text {
                    required property int index
                    x: index * (parent.width / 4) + 7
                    y: 6
                    text: 9 + index
                    color: "#96969B"
                    font.pixelSize: 9
                }
            }
        }

        Item {
            anchors.top: parent.top
            anchors.topMargin: root.rulerHeight
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            clip: true

            Repeater {
                model: 18
                delegate: Rectangle {
                    required property int index
                    y: index * root.rowHeight
                    width: parent.width
                    height: root.rowHeight
                    color: [1, 3, 6, 8, 10].indexOf((11 - index) % 12) >= 0 ? "#202022" : "#272729"
                    border.color: "#303034"

                    Rectangle {
                        width: root.keyWidth
                        height: parent.height
                        color: [1, 3, 6, 8, 10].indexOf((11 - parent.index) % 12) >= 0 ? "#171719" : "#D8D8DA"
                        border.color: "#3A3A3C"
                        Text {
                            anchors.centerIn: parent
                            visible: parent.parent.index === 11
                            text: "C4"
                            color: "#55555A"
                            font.pixelSize: 8
                        }
                    }
                }
            }

            Repeater {
                model: 17
                delegate: Rectangle {
                    required property int index
                    x: root.keyWidth + index * ((parent.width - root.keyWidth) / 16)
                    width: index % 4 === 0 ? 2 : 1
                    height: parent.height
                    color: index % 4 === 0 ? "#515157" : "#353539"
                }
            }

            Repeater {
                model: [
                    { "x": 0.03, "y": 0.60, "w": 0.10 }, { "x": 0.15, "y": 0.43, "w": 0.07 },
                    { "x": 0.24, "y": 0.54, "w": 0.11 }, { "x": 0.38, "y": 0.32, "w": 0.06 },
                    { "x": 0.46, "y": 0.49, "w": 0.14 }, { "x": 0.62, "y": 0.37, "w": 0.08 },
                    { "x": 0.72, "y": 0.56, "w": 0.10 }, { "x": 0.84, "y": 0.27, "w": 0.12 }
                ]

                delegate: Rectangle {
                    required property var modelData
                    x: root.keyWidth + modelData.x * (parent.width - root.keyWidth)
                    y: modelData.y * parent.height
                    width: modelData.w * (parent.width - root.keyWidth)
                    height: 9
                    radius: 2
                    color: root.noteColor
                    border.color: "#F4D78A"
                }
            }

            Rectangle {
                x: root.keyWidth + (parent.width - root.keyWidth) * 0.47
                width: 1
                height: parent.height
                color: "#F4C95D"
            }
        }
    }

    Rectangle {
        id: velocityLane
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 41
        color: "#242426"
        border.color: "#3A3A3C"

        Text { x: 10; y: 7; text: qsTrc("appshell", "Velocity"); color: "#8F8F94"; font.pixelSize: 9 }

        Repeater {
            model: [0.55, 0.76, 0.62, 0.91, 0.68, 0.82, 0.58, 0.74]
            delegate: Rectangle {
                required property int index
                required property real modelData
                x: root.keyWidth + index * Math.max(16, (parent.width - root.keyWidth - 12) / 8)
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 4
                width: 4
                height: 25 * modelData
                color: root.noteColor
            }
        }
    }
}
