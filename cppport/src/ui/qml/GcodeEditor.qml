import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The G-code Editor (features/Visualizer/GcodeEditor), over the visualizer:
// the file's lines - tap one's text to edit it, its box to select it (Shift
// for a range) - with the job's progress while it runs (read-only then),
// search (Ctrl+F) and jump to line floating top right, scroll to top; below,
// Jump, Search, Select all, Copy, Delete, Revert and Save.
Panel {
    id: editor
    objectName: "gcodeEditor"

    property GcodeEditorModel model: GcodeEditorModel {}
    property int editingRow: -1
    readonly property int lineHeight: 32

    function openFile() {
        model.load()
        editingRow = -1
        searchBox.visible = false
        jumpBox.visible = false
        visible = true
        list.positionViewAtBeginning()
    }
    function close() {
        editingRow = -1
        visible = false
    }
    function showRow(row) {
        if (row < 0)
            return
        const top = row * lineHeight
        if (top < list.contentY || top + lineHeight > list.contentY + list.height)
            list.positionViewAtIndex(row, ListView.Center)
    }

    visible: false
    radius: Theme.radiusSmall
    border.color: Theme.dark ? Theme.outline : "transparent"

    component Chip: Rectangle {
        property alias text: chipLabel.text
        property color fg
        implicitWidth: chipLabel.implicitWidth + 16
        implicitHeight: 24
        radius: 12
        Label { id: chipLabel; anchors.centerIn: parent; font.pixelSize: Theme.fontXs; font.weight: Font.Medium; color: parent.fg }
    }

    TapHandler {}  // nothing beneath takes the taps
    Shortcut {
        sequences: [StandardKey.Find]
        enabled: editor.visible
        onActivated: { jumpBox.visible = false; searchBox.visible = true; searchField.forceActiveFocus() }
    }
    Connections {
        target: editor.model
        function onJobChanged() {
            if (editor.model.jobRunning) {
                editor.editingRow = -1
                editor.showRow(editor.model.runningRow)
            }
        }
        function onSearchChanged() { editor.showRow(editor.model.currentMatchRow) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The header.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 12
            Label {
                text: qsTr("G-code Editor")
                font.pixelSize: Theme.fontLg
                font.weight: Font.DemiBold
            }
            Chip {
                objectName: "editorJobState"
                visible: editor.model.jobRunning
                text: editor.model.jobState === "Paused" ? qsTr("Paused") : qsTr("Running")
                color: editor.model.jobState === "Paused" ? Theme.tw.yellow[100] : Theme.tw.green[100]
                fg: editor.model.jobState === "Paused" ? Theme.tw.yellow[700] : Theme.tw.green[700]
            }
            Chip {
                objectName: "editorSelected"
                visible: editor.model.selectedCount > 0
                text: qsTr("%1 selected").arg(editor.model.selectedCount)
                color: Theme.tw.blue[100]
                fg: Theme.blue[700]
            }
            Label {
                visible: editor.model.count > 0
                text: qsTr("%1 lines").arg(editor.model.count.toLocaleString(Qt.locale(), "f", 0))
                font.pixelSize: Theme.fontXs
                color: Theme.contentMuted
            }
            Item { Layout.fillWidth: true }
            GButton {
                objectName: "editorClose"
                tooltip: qsTr("Close editor")
                variant: "ghost"
                iconName: "LuX"
                iconSize: 16
                onClicked: editor.close()
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.outline }

        // The lines.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.secondary
            clip: true

            ListView {
                id: list
                objectName: "editorLines"
                anchors.fill: parent
                model: editor.model
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: 20 * editor.lineHeight  // OVERSCAN
                ScrollBar.vertical: GScrollBar {}
                // A tap below the lines clears the selection.
                footer: Item {
                    width: list.width
                    height: Math.max(editor.lineHeight, list.height - editor.model.count * editor.lineHeight)
                    TapHandler { onTapped: editor.model.clearSelection() }
                }
                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string text
                    required property string styled
                    required property bool selected
                    required property bool matched
                    required property bool currentMatch
                    required property string status
                    readonly property bool running: editor.model.jobRunning
                    objectName: "editorLine_" + index
                    width: ListView.view.width
                    height: editor.lineHeight
                    color: currentMatch ? (Theme.dark ? Qt.rgba(0.44, 0.26, 0.03, 0.4) : Theme.tw.yellow[200])
                         : matched ? (Theme.dark ? Qt.rgba(0.44, 0.26, 0.03, 0.2) : Theme.tw.yellow[50])
                         : selected ? (Theme.dark ? Qt.rgba(0.12, 0.23, 0.54, 0.3) : Theme.tw.blue[100])
                         : status === "processed" ? (Theme.dark ? Qt.rgba(0.08, 0.33, 0.18, 0.2) : Theme.tw.green[50])
                         : status === "current" ? (Theme.dark ? Qt.rgba(0.44, 0.26, 0.03, 0.3) : Theme.tw.yellow[100])
                         : status === "upcoming" ? (Theme.dark ? Qt.rgba(0.12, 0.23, 0.54, 0.1) : Theme.tw.blue[50])
                         : index % 2 === 0 ? (Theme.dark ? Theme.surfaceElevated : Theme.gray[200]) : "transparent"
                    border.width: currentMatch ? 2 : 0
                    border.color: Theme.tw.yellow[500]
                    // The job's progress down the left edge.
                    Rectangle {
                        visible: row.status !== "none"
                        width: row.status === "current" ? 4 : 2
                        height: parent.height
                        color: row.status === "processed" ? Theme.tw.green[500] : row.status === "current" ? Theme.tw.yellow[500] : Theme.tw.blue[300]
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 12
                        // The box.
                        Item {
                            objectName: "editorSelect_" + row.index
                            Layout.preferredWidth: 32
                            Layout.fillHeight: true
                            opacity: row.running ? 0.5 : 1
                            Rectangle {
                                anchors.centerIn: parent
                                width: 20; height: 20; radius: 4
                                color: row.selected ? Theme.blue[500] : "transparent"
                                border.width: row.selected ? 0 : 2
                                border.color: Theme.dark ? Theme.outline : Theme.gray[400]
                                Icon { anchors.centerIn: parent; visible: row.selected; name: "LuCheck"; color: "white"; size: 12 }
                            }
                            TapHandler {
                                acceptedModifiers: Qt.NoModifier
                                onTapped: editor.model.toggleSelected(row.index, false)
                            }
                            TapHandler {
                                acceptedModifiers: Qt.ShiftModifier  // the range from the last one
                                onTapped: editor.model.toggleSelected(row.index, true)
                            }
                        }
                        Label {
                            Layout.minimumWidth: 40
                            horizontalAlignment: Text.AlignRight
                            text: (row.index + 1) + (row.status === "current" && row.index === editor.model.runningRow ? " ▶" : "")
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.fontSm
                            font.weight: row.selected || row.status === "current" ? Font.Medium : Font.Normal
                            color: row.selected ? Theme.blue[700] : row.status === "current" ? Theme.tw.yellow[900]
                                 : row.status === "processed" ? Theme.tw.green[700] : Theme.contentMuted
                        }
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Label {
                                visible: editor.editingRow !== row.index
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                text: row.styled || " "
                                textFormat: Text.StyledText
                                font.family: Theme.monoFont
                                font.pixelSize: Theme.fontSm
                            }
                            TapHandler {
                                enabled: !row.running
                                onTapped: editor.editingRow = row.index
                            }
                            TextField {
                                id: lineField
                                objectName: "editorLineField"
                                visible: editor.editingRow === row.index
                                anchors.fill: parent
                                text: row.text
                                font.family: Theme.monoFont
                                font.pixelSize: Theme.fontSm
                                background: Rectangle {
                                    color: "transparent"
                                    radius: 4
                                    border.color: Theme.blue[400]
                                    border.width: 2
                                }
                                onVisibleChanged: if (visible) { forceActiveFocus(); selectAll() }
                                onTextEdited: editor.model.setLine(row.index, text)
                                onAccepted: editor.editingRow = -1
                                Keys.onEscapePressed: editor.editingRow = -1
                                onActiveFocusChanged: if (!activeFocus && editor.editingRow === row.index) editor.editingRow = -1
                            }
                        }
                    }
                }
            }

            // Search, floating top right.
            Panel {
                id: searchBox
                objectName: "editorSearchBox"
                visible: false
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 12
                width: searchRow.implicitWidth + 16
                height: searchRow.implicitHeight + 16
                radius: 4
                color: Theme.secondary
                border.color: Theme.outline
                RowLayout {
                    id: searchRow
                    anchors.centerIn: parent
                    spacing: 6
                    TextField {
                        id: searchField
                        objectName: "editorSearch"
                        Layout.preferredWidth: 180
                        implicitHeight: 36
                        placeholderText: qsTr("Search...")
                        onTextChanged: editor.model.setSearch(text)
                        Keys.onReturnPressed: (event) => (event.modifiers & Qt.ShiftModifier) ? editor.model.previousMatch() : editor.model.nextMatch()
                    }
                    Label {
                        objectName: "editorMatches"
                        text: editor.model.matchCount > 0 ? (editor.model.currentMatch + 1) + "/" + editor.model.matchCount : "0/0"
                        font.pixelSize: Theme.fontXs
                        color: Theme.contentMuted
                    }
                    GButton { tooltip: qsTr("Previous match (Shift+Enter)"); iconName: "LuChevronUp"; iconSize: 14; enabled: editor.model.matchCount > 0; onClicked: editor.model.previousMatch() }
                    GButton { objectName: "editorNextMatch"; tooltip: qsTr("Next match (Enter)"); iconName: "LuChevronDown"; iconSize: 14; enabled: editor.model.matchCount > 0; onClicked: editor.model.nextMatch() }
                    GButton { tooltip: qsTr("Close search"); iconName: "LuX"; iconSize: 14; onClicked: { searchField.text = ""; searchBox.visible = false } }
                }
            }
            // Jump to line, floating top right.
            Panel {
                id: jumpBox
                objectName: "editorJumpBox"
                visible: false
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 12
                width: jumpRow.implicitWidth + 16
                height: jumpRow.implicitHeight + 16
                radius: 4
                color: Theme.secondary
                border.color: Theme.outline
                function jump() {
                    const row = editor.model.jumpRow(jumpField.text)
                    if (row >= 0) {
                        list.positionViewAtIndex(row, ListView.Center)
                        editor.model.clearSelection()
                        editor.model.toggleSelected(row, false)
                    }
                }
                RowLayout {
                    id: jumpRow
                    anchors.centerIn: parent
                    spacing: 6
                    Label { text: qsTr("Jump to line:"); font.pixelSize: Theme.fontXs; color: Theme.contentMuted }
                    TextField {
                        id: jumpField
                        objectName: "editorJump"
                        Layout.preferredWidth: 100
                        implicitHeight: 36
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 1 }
                        onAccepted: jumpBox.jump()
                    }
                    Label {
                        text: Math.floor(list.contentY / editor.lineHeight + list.height / 2 / editor.lineHeight) + "/" + editor.model.count
                        font.pixelSize: Theme.fontXs
                        color: Theme.contentMuted
                    }
                    GButton { objectName: "editorJumpGo"; tooltip: qsTr("Jump To Line"); iconName: "VscDebugStart"; iconSize: 14; enabled: jumpField.text !== ""; onClicked: jumpBox.jump() }
                    GButton { tooltip: qsTr("Close jump to line"); iconName: "LuX"; iconSize: 14; onClicked: { jumpField.text = ""; jumpBox.visible = false } }
                }
            }
            // Scroll to top, once away from it.
            GButton {
                objectName: "editorScrollTop"
                tooltip: qsTr("Scroll to top")
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                visible: list.contentY > 500
                enabled: !editor.model.jobRunning
                variant: "outline"
                iconName: "LuArrowUp"
                iconSize: 16
                onClicked: list.positionViewAtBeginning()
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.outline }

        // The actions.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 12
            Item { Layout.fillWidth: true }
            GButton {
                objectName: "editorJumpButton"
                tooltip: qsTr("Jump to Line")
                variant: "outline"
                iconName: "PiMouseScroll"
                onClicked: { searchBox.visible = false; jumpBox.visible = !jumpBox.visible; if (jumpBox.visible) jumpField.forceActiveFocus() }
            }
            GButton {
                objectName: "editorSearchButton"
                tooltip: qsTr("Search (Ctrl+F / Cmd+F)")
                variant: "outline"
                iconName: "LuSearch"
                iconSize: 16
                onClicked: { jumpBox.visible = false; searchBox.visible = !searchBox.visible; if (searchBox.visible) searchField.forceActiveFocus() }
            }
            GButton {
                objectName: "editorSelectAll"
                tooltip: editor.model.selectedCount === editor.model.count && editor.model.count > 0 ? qsTr("Deselect all lines") : qsTr("Select all lines")
                variant: editor.model.count > 0 && editor.model.selectedCount === editor.model.count ? "primary" : "outline"
                iconName: "LuCopyCheck"
                iconSize: 16
                enabled: !editor.model.jobRunning
                onClicked: editor.model.toggleSelectAll()
            }
            GButton {
                objectName: "editorCopy"
                tooltip: editor.model.jobRunning ? qsTr("Copy disabled while job is running")
                         : editor.model.selectedCount > 0 ? qsTr("Copy %1 selected line(s)").arg(editor.model.selectedCount)
                         : qsTr("Copy all lines")
                variant: "outline"
                iconName: "LuCopy"
                iconSize: 16
                enabled: !editor.model.jobRunning
                onClicked: Backend.notify(editor.model.copy(), "info")
            }
            GButton {
                objectName: "editorDelete"
                tooltip: editor.model.jobRunning ? qsTr("Delete disabled while job is running")
                         : qsTr("Delete %1 selected line(s)").arg(editor.model.selectedCount)
                variant: "outline"
                iconName: "LuTrash2"
                iconSize: 16
                iconColor: enabled ? Theme.red[600] : foreground
                enabled: !editor.model.jobRunning && editor.model.selectedCount > 0
                onClicked: editor.model.deleteSelected()
            }
            GButton {
                objectName: "editorRevert"
                tooltip: editor.model.jobRunning ? qsTr("Revert disabled while job is running") : qsTr("Revert to original content")
                variant: "outline"
                iconName: "LuRotateCcw"
                iconSize: 16
                enabled: editor.model.hasChanges && !editor.model.jobRunning
                onClicked: if (editor.model.revertChanges()) Backend.notify(qsTr("Reverted to original content"), "info")
            }
            GButton {
                objectName: "editorSave"
                tooltip: editor.model.jobRunning ? qsTr("Save disabled while job is running") : qsTr("Save changes")
                variant: "primary"
                iconName: "LuSave"
                iconSize: 16
                enabled: editor.model.hasChanges && !editor.model.jobRunning
                onClicked: if (editor.model.save()) Backend.notify(qsTr("G-code saved successfully"), "success")
            }
        }
    }
}
