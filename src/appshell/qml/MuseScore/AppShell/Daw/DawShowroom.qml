/* SPDX-License-Identifier: GPL-3.0-only */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Muse.Ui
import Muse.UiComponents

Rectangle {
    id: root

    readonly property int libraryWidth: 210
    readonly property int trackHeaderWidth: 226
    readonly property int inspectorWidth: 228
    readonly property int trackHeight: 72
    readonly property var tracks: [
        { "name": qsTrc("appshell", "Midnight Keys"), "subtitle": qsTrc("appshell", "Studio Grand"), "color": "#D9A441", "icon": IconCode.MUSIC_NOTES, "armed": false },
        { "name": qsTrc("appshell", "Velvet Bass"), "subtitle": qsTrc("appshell", "Electric Bass"), "color": "#B46EC8", "icon": IconCode.AUDIO, "armed": false },
        { "name": qsTrc("appshell", "Neon Drums"), "subtitle": qsTrc("appshell", "Gold Room Kit"), "color": "#D96755", "icon": IconCode.PERCUSSION, "armed": true },
        { "name": qsTrc("appshell", "Lead Vocal"), "subtitle": qsTrc("appshell", "Audio input 1"), "color": "#4EA9A1", "icon": IconCode.MICROPHONE, "armed": false }
    ]

    color: "#18181A"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: "#2E2E30"
            border.color: "#3A3A3C"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 7

                ShowroomIconButton { iconCode: IconCode.REWIND_START_FILL }
                ShowroomIconButton { iconCode: IconCode.PLAY_FILL; emphasized: true }
                ShowroomIconButton { iconCode: IconCode.STOP_FILL }
                ShowroomIconButton { iconCode: IconCode.RECORD_FILL; active: true }
                ShowroomIconButton { iconCode: IconCode.LOOP; active: true }
                ShowroomIconButton { iconCode: IconCode.METRONOME }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: "#4B4B50" }

                ColumnLayout {
                    spacing: 0
                    Text { text: "009  03  240"; color: "#F1F1F2"; font.pixelSize: 15; font.family: "Consolas" }
                    Text { text: qsTrc("appshell", "BAR     BEAT     TICK"); color: "#77777D"; font.pixelSize: 7; font.letterSpacing: 1 }
                }

                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: "#4B4B50" }
                Text { text: "118.0"; color: "#ECECEE"; font.pixelSize: 13; font.bold: true }
                Text { text: qsTrc("appshell", "BPM"); color: "#85858A"; font.pixelSize: 8 }
                Text { text: "4 / 4"; color: "#B8B8BC"; font.pixelSize: 11 }

                Item { Layout.fillWidth: true }

                ShowroomIconButton { iconCode: IconCode.MIXER; label: qsTrc("appshell", "Mixer") }
                ShowroomIconButton { iconCode: IconCode.SETTINGS_COG }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: root.libraryWidth
                Layout.fillHeight: true
                color: "#262628"
                border.color: "#3A3A3C"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Text { text: qsTrc("appshell", "LIBRARY"); color: "#A7A7AC"; font.pixelSize: 10; font.bold: true; font.letterSpacing: 0.8 }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        radius: 7
                        color: "#1E1E20"
                        border.color: "#45454A"
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 9
                            spacing: 7
                            StyledIconLabel { iconCode: IconCode.SEARCH; color: "#88888E"; font.pixelSize: 13 }
                            Text { text: qsTrc("appshell", "Search sounds"); color: "#77777D"; font.pixelSize: 10 }
                        }
                    }

                    Repeater {
                        model: [
                            { "title": qsTrc("appshell", "Instruments"), "icon": IconCode.MUSIC_NOTES },
                            { "title": qsTrc("appshell", "Drum kits"), "icon": IconCode.PERCUSSION },
                            { "title": qsTrc("appshell", "Audio loops"), "icon": IconCode.WAVEFORM },
                            { "title": qsTrc("appshell", "Effects"), "icon": IconCode.PLUGIN }
                        ]

                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: 31
                            radius: 6
                            color: index === 0 ? "#3B3B3F" : "transparent"
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 9
                                spacing: 9
                                StyledIconLabel { iconCode: parent.parent.modelData.icon; color: index === 0 ? "#D9A441" : "#A7A7AC"; font.pixelSize: 14 }
                                Text { text: parent.parent.modelData.title; color: "#D8D8DA"; font.pixelSize: 11 }
                            }
                        }
                    }

                    Text { text: qsTrc("appshell", "RECENT"); color: "#77777D"; font.pixelSize: 9; font.bold: true; font.letterSpacing: 0.7 }
                    Repeater {
                        model: ["Studio Grand", "Gold Room Kit", "Velvet Bass", "Warm Strings"]
                        delegate: Text {
                            required property string modelData
                            Layout.leftMargin: 9
                            text: modelData
                            color: "#AFAFB4"
                            font.pixelSize: 10
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 29
                    color: "#242426"
                    border.color: "#3A3A3C"

                    Row {
                        x: root.trackHeaderWidth
                        height: parent.height
                        Repeater {
                            model: 17
                            delegate: Item {
                                required property int index
                                width: 72
                                height: parent.height
                                Rectangle { width: index % 4 === 0 ? 2 : 1; height: parent.height; color: index % 4 === 0 ? "#56565B" : "#343438" }
                                Text { x: 7; y: 7; visible: index % 4 === 0; text: 1 + index / 4; color: "#A0A0A5"; font.pixelSize: 9 }
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.trackHeight * root.tracks.length
                    clip: true

                    Repeater {
                        model: root.tracks
                        delegate: Item {
                            required property var modelData
                            required property int index
                            y: index * root.trackHeight
                            width: parent.width
                            height: root.trackHeight

                            ShowroomTrackHeader {
                                width: root.trackHeaderWidth
                                height: parent.height
                                trackName: parent.modelData.name
                                trackSubtitle: parent.modelData.subtitle
                                trackColor: parent.modelData.color
                                iconCode: parent.modelData.icon
                                selected: parent.index === 0
                                armed: parent.modelData.armed
                            }

                            Rectangle {
                                x: root.trackHeaderWidth
                                width: parent.width - x
                                height: parent.height
                                color: parent.index % 2 === 0 ? "#202022" : "#242426"
                                border.color: "#303034"

                                Repeater {
                                    model: 17
                                    delegate: Rectangle {
                                        required property int index
                                        x: index * 72
                                        width: index % 4 === 0 ? 2 : 1
                                        height: parent.height
                                        color: index % 4 === 0 ? "#444449" : "#2E2E32"
                                    }
                                }

                                ShowroomRegion {
                                    x: parent.parent.index === 0 ? 72 : parent.parent.index === 1 ? 18 : parent.parent.index === 2 ? 144 : 216
                                    y: 8
                                    width: parent.parent.index === 3 ? 330 : 246
                                    height: parent.height - 16
                                    regionName: parent.parent.index === 0 ? qsTrc("appshell", "Midnight Keys")
                                              : parent.parent.index === 1 ? qsTrc("appshell", "Bass Hook")
                                              : parent.parent.index === 2 ? qsTrc("appshell", "Verse Beat")
                                              : qsTrc("appshell", "Lead Vocal 01")
                                    regionColor: parent.parent.modelData.color
                                    selected: parent.parent.index === 0
                                    audioRegion: parent.parent.index === 3
                                }

                                ShowroomRegion {
                                    visible: parent.parent.index < 3
                                    x: parent.parent.index === 0 ? 354 : parent.parent.index === 1 ? 306 : 462
                                    y: 8
                                    width: 198
                                    height: parent.height - 16
                                    regionName: parent.parent.index === 2 ? qsTrc("appshell", "Fill") : qsTrc("appshell", "Variation")
                                    regionColor: parent.parent.modelData.color
                                }
                            }
                        }
                    }

                    Rectangle {
                        x: root.trackHeaderWidth + 72 * 8.55
                        width: 1
                        height: parent.height
                        color: "#F4C95D"
                        z: 10
                        Rectangle { width: 9; height: 9; x: -4; y: -1; rotation: 45; color: "#F4C95D" }
                    }
                }

                ShowroomPianoRoll {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 220
                }
            }

            ShowroomInspector {
                Layout.preferredWidth: root.inspectorWidth
                Layout.fillHeight: true
            }
        }
    }
}
