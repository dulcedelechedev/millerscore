/* SPDX-License-Identifier: GPL-3.0-only */
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Muse.Ui
import Muse.UiComponents
import MuseScore.AppShell

//! Inspector for the selected track. The track comes from DawTracksModel (the
//! only track selector in the workspace); the sound assignment is the same one
//! the Mixer edits.
Rectangle {
    id: root

    DawTheme { id: dawTheme }

    required property var tracksModel
    required property var instrumentModel

    property NavigationSection navigationSection: null
    property int navigationOrderStart: 0

    readonly property bool hasTrack: tracksModel.selectedIndex >= 0
    readonly property color trackColor: dawTheme.trackColor(tracksModel.selectedColorIndex)
    readonly property int loadState: instrumentModel.loadState
    readonly property bool warningState: loadState === DawInstrumentModel.Missing
                                         || loadState === DawInstrumentModel.Failed
    readonly property string soundButtonText: {
        const title = instrumentModel.currentTitle.length > 0
                      ? instrumentModel.currentTitle : qsTrc("appshell", "Choose a sound")
        return instrumentModel.currentVendor.length > 0
               ? qsTrc("appshell", "%1 — %2").arg(title).arg(instrumentModel.currentVendor) : title
    }
    readonly property string statusText: {
        switch (loadState) {
        case DawInstrumentModel.NoTrack: return qsTrc("appshell", "Playback is starting…")
        case DawInstrumentModel.Scanning: return qsTrc("appshell", "Looking for installed sounds…")
        case DawInstrumentModel.Switching: return qsTrc("appshell", "Switching sound…")
        case DawInstrumentModel.Missing: return qsTrc("appshell", "This sound is not installed on this computer. The assignment is kept.")
        case DawInstrumentModel.Failed: return qsTrc("appshell", "Installed sounds could not be read. The assignment is kept; try Refresh list.")
        default: return instrumentModel.currentBackend.length > 0
                        ? qsTrc("appshell", "Plays through %1").arg(instrumentModel.currentBackend)
                        : qsTrc("appshell", "Choose a sound for this instrument")
        }
    }

    color: dawTheme.panel

    function buildSoundMenu() {
        const categories = []
        const missing = []
        const source = instrumentModel.resources

        function leaf(resource) {
            let title = resource.title
            if (resource.backend === "Muse Sounds" && resource.category.length > 0)
                title = resource.category + " · " + title
            return {
                id: resource.value,
                title: title,
                enabled: !resource.missing,
                checkable: true,
                checked: resource.selected,
                subitems: [],
                includeInFilteredLists: true,
                isFilterCategory: false
            }
        }

        function categoryTitle(backend) {
            return backend === "SoundFont" ? qsTrc("appshell", "SoundFonts") : backend
        }

        for (let index = 0; index < source.length; ++index) {
            const resource = source[index]
            if (resource.missing) {
                missing.push({
                    id: "missing:" + resource.value,
                    title: resource.text,
                    enabled: false,
                    checkable: true,
                    checked: true,
                    subitems: [],
                    includeInFilteredLists: true,
                    isFilterCategory: false
                })
                continue
            }

            const title = categoryTitle(resource.backend)
            let category = null
            for (let categoryIndex = 0; categoryIndex < categories.length; ++categoryIndex) {
                if (categories[categoryIndex].title === title) {
                    category = categories[categoryIndex]
                    break
                }
            }
            if (!category) {
                category = { title: title, checked: false, groups: [] }
                categories.push(category)
            }

            const groupTitle = resource.group.length > 0 ? resource.group : qsTrc("appshell", "Other")
            let group = null
            for (let groupIndex = 0; groupIndex < category.groups.length; ++groupIndex) {
                if (category.groups[groupIndex].title === groupTitle) {
                    group = category.groups[groupIndex]
                    break
                }
            }
            if (!group) {
                group = { title: groupTitle, checked: false, items: [] }
                category.groups.push(group)
            }
            const item = leaf(resource)
            group.items.push(item)
            group.checked = group.checked || item.checked
            category.checked = category.checked || item.checked
        }

        const menu = missing.slice()
        if (missing.length > 0 && categories.length > 0)
            menu.push({})
        for (let categoryIndex = 0; categoryIndex < categories.length; ++categoryIndex) {
            const category = categories[categoryIndex]
            const groups = []
            for (let groupIndex = 0; groupIndex < category.groups.length; ++groupIndex) {
                const group = category.groups[groupIndex]
                groups.push({
                    id: "daw-group:" + category.title + ":" + group.title,
                    title: group.title,
                    enabled: true,
                    checkable: true,
                    checked: group.checked,
                    subitems: group.items,
                    includeInFilteredLists: false,
                    isFilterCategory: true
                })
            }
            menu.push({
                id: "daw-category:" + category.title,
                title: category.title,
                enabled: true,
                checkable: true,
                checked: category.checked,
                subitems: groups,
                includeInFilteredLists: false,
                isFilterCategory: true
            })
        }
        return menu
    }

    NavigationPanel {
        id: navPanel
        name: "DawTrackInspector"
        section: root.navigationSection
        order: root.navigationOrderStart
        direction: NavigationPanel.Vertical
        accessible.name: qsTrc("appshell", "Track inspector")
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: dawTheme.line
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // Track identity
        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 4
                Layout.preferredHeight: 32
                radius: 2
                color: root.hasTrack ? root.trackColor : dawTheme.line
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                StyledTextLabel {
                    Layout.fillWidth: true
                    text: root.hasTrack ? root.tracksModel.selectedName : qsTrc("appshell", "No track selected")
                    horizontalAlignment: Text.AlignLeft
                    font: ui.theme.largeBodyBoldFont
                    displayTruncatedTextOnHover: true
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    text: qsTrc("appshell", "Instrument track")
                    horizontalAlignment: Text.AlignLeft
                    color: ui.theme.fontSecondaryColor
                }
            }
        }

        SeparatorLine { Layout.fillWidth: true; Layout.leftMargin: -16; Layout.rightMargin: -16 }

        // Sound
        StyledTextLabel {
            text: qsTrc("appshell", "Sound")
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
        }

        FlatButton {
            id: soundButton
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            enabled: root.hasTrack && root.instrumentModel.trackAvailable
            text: root.soundButtonText
            orientation: Qt.Horizontal
            toolTipTitle: root.soundButtonText
            toolTipDescription: root.statusText
            navigation.panel: navPanel
            navigation.row: 1
            navigation.accessible.name: qsTrc("appshell", "Sound for %1").arg(root.tracksModel.selectedName)
            onClicked: soundMenu.toggleOpened(root.buildSoundMenu())

            FlatButtonMenuIndicatorTriangle {}

            StyledMenuLoader {
                id: soundMenu
                // StyledMenu constrains its height to the anchor item. Using
                // the 30 px button here collapses a searchable hierarchy into
                // a thin strip. Anchor to the window content, as the standard
                // workspace/view menus do, while the loader's parent still
                // supplies the button-relative popup origin.
                menuAnchorItem: root.Window.window ? root.Window.window.contentItem : null
                isSearchable: true
                accessibleName: qsTrc("appshell", "Installed sounds")
                onHandleMenuItem: function(itemId) {
                    root.instrumentModel.selectInstrument(itemId)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            StyledBusyIndicator {
                visible: root.loadState === DawInstrumentModel.Scanning || root.loadState === DawInstrumentModel.Switching
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
            }

            StyledIconLabel {
                visible: root.loadState === DawInstrumentModel.Missing || root.loadState === DawInstrumentModel.Failed
                iconCode: IconCode.WARNING
                color: dawTheme.warning
            }

            StyledTextLabel {
                Layout.fillWidth: true
                text: root.statusText
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                maximumLineCount: 3
                color: root.warningState ? dawTheme.warning : ui.theme.fontSecondaryColor
            }
        }

        FlatButton {
            visible: root.instrumentModel.editorAvailable
            Layout.fillWidth: true
            icon: IconCode.PLUGIN
            text: qsTrc("appshell", "Open plug-in")
            orientation: Qt.Horizontal
            toolTipTitle: qsTrc("appshell", "Open the plug-in's own window")
            navigation.panel: navPanel
            navigation.row: 2
            navigation.accessible.name: qsTrc("appshell", "Open the plug-in window")
            onClicked: root.instrumentModel.openEditor()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            FlatButton {
                Layout.fillWidth: true
                icon: IconCode.PLUS
                text: qsTrc("appshell", "Install SoundFont…")
                orientation: Qt.Horizontal
                toolTipTitle: qsTrc("appshell", "Install an .sf2 or .sf3 file")
                navigation.panel: navPanel
                navigation.row: 3
                navigation.accessible.name: qsTrc("appshell", "Install a SoundFont")
                onClicked: root.instrumentModel.installSoundFont()
            }

            FlatButton {
                Layout.fillWidth: true
                icon: IconCode.UPDATE
                text: qsTrc("appshell", "Refresh list")
                orientation: Qt.Horizontal
                toolTipTitle: qsTrc("appshell", "Look for newly installed sounds")
                navigation.panel: navPanel
                navigation.row: 4
                navigation.accessible.name: qsTrc("appshell", "Refresh installed sounds")
                onClicked: root.instrumentModel.refreshResources()
            }
        }

        SeparatorLine { Layout.fillWidth: true; Layout.leftMargin: -16; Layout.rightMargin: -16 }

        // Playback
        StyledTextLabel {
            text: qsTrc("appshell", "Playback")
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            FlatButton {
                Layout.fillWidth: true
                enabled: root.hasTrack
                icon: IconCode.MUTE
                text: qsTrc("appshell", "Mute")
                orientation: Qt.Horizontal
                accentButton: root.tracksModel.selectedMuted
                navigation.panel: navPanel
                navigation.row: 5
                navigation.accessible.name: root.tracksModel.selectedMuted ? qsTrc("appshell", "Unmute") : qsTrc("appshell", "Mute")
                onClicked: root.tracksModel.toggleMute(root.tracksModel.selectedIndex)
            }

            FlatButton {
                Layout.fillWidth: true
                enabled: root.hasTrack
                icon: IconCode.SOLO
                text: qsTrc("appshell", "Solo")
                orientation: Qt.Horizontal
                accentButton: root.tracksModel.selectedSolo
                navigation.panel: navPanel
                navigation.row: 6
                navigation.accessible.name: root.tracksModel.selectedSolo ? qsTrc("appshell", "Unsolo") : qsTrc("appshell", "Solo")
                onClicked: root.tracksModel.toggleSolo(root.tracksModel.selectedIndex)
            }
        }

        FlatButton {
            Layout.fillWidth: true
            icon: IconCode.MIXER
            text: qsTrc("appshell", "Volume, pan and effects…")
            orientation: Qt.Horizontal
            toolTipTitle: qsTrc("appshell", "Show the Mixer")
            navigation.panel: navPanel
            navigation.row: 7
            navigation.accessible.name: qsTrc("appshell", "Open volume, pan and effects in the Mixer")
            onClicked: root.tracksModel.openMixer()
        }

        Item { Layout.fillHeight: true }
    }
}
