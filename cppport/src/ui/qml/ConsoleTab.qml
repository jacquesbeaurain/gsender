import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The Console (features/Console): the filters with their dots, Copy last 50,
// Clear; the lines - each with its type's icon and colour on the console's
// dark surface - following the output while at the bottom, with a button
// back to the latest; the command line with its history. Covered by "Not
// connected to a device" without a machine.
Item {
    id: tab
    objectName: "consoleTab"

    property ConsoleModel model: ConsoleModel {}

    readonly property var icons: ({
        gcode: { name: "LuChevronRight", color: Theme.consoleColors.gcodeIcon },
        response: { name: "LuDot", color: Theme.consoleColors.response },
        system: { name: "LuCog", color: Theme.consoleColors.system },
        warning: { name: "LuTriangleAlert", color: Theme.consoleColors.warning },
        error: { name: "LuCircleAlert", color: Theme.consoleColors.error },
        alarm: { name: "LuOctagonAlert", color: Theme.consoleColors.alarm }
    })

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        // The toolbar.
        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                contentWidth: filters.implicitWidth
                flickableDirection: Flickable.HorizontalFlick
                clip: true
                Row {
                    id: filters
                    spacing: 2
                    Repeater {
                        model: [
                            { key: "all", label: qsTr("All"), dot: "" },
                            { key: "gcode", label: qsTr("G-code"), dot: Theme.blue[500] },
                            { key: "response", label: qsTr("Responses"), dot: Theme.gray[400] },
                            { key: "system", label: qsTr("System"), dot: Theme.green[500] },
                            { key: "faults", label: qsTr("Faults"), dot: Theme.red[600] }
                        ]
                        Rectangle {
                            required property var modelData
                            readonly property bool selected: tab.model.filter === modelData.key
                            objectName: "consoleFilter_" + modelData.key
                            width: filterRow.implicitWidth + 16
                            height: 36
                            radius: 4
                            color: selected ? Theme.blue[500] : "transparent"
                            Row {
                                id: filterRow
                                anchors.centerIn: parent
                                spacing: 4
                                Rectangle {
                                    visible: modelData.dot !== ""
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 6; height: 6; radius: 3
                                    color: parent.parent.selected ? "white" : (modelData.dot || "transparent")
                                }
                                Label {
                                    text: modelData.label
                                    font.pixelSize: Theme.fontXs
                                    font.weight: Font.Medium
                                    color: parent.parent.selected ? "white" : Theme.contentSecondary
                                }
                            }
                            TapHandler { onTapped: tab.model.filter = modelData.key }
                        }
                    }
                }
            }
            GButton {
                objectName: "consoleCopy"
                variant: "ghost"
                iconName: "LuCopy"
                iconSize: 16
                onClicked: {
                    const n = tab.model.copyLast()
                    if (n > 0)
                        Backend.notify(qsTr("Copied last %1 commands to clipboard").arg(n), "success")
                }
            }
            GButton {
                objectName: "consoleClear"
                variant: "ghost"
                iconName: "LuEraser"
                iconSize: 16
                onClicked: {
                    tab.model.clear()
                    Backend.notify(qsTr("Console cleared"), "info")
                }
            }
        }

        // The lines.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 4
            color: Theme.consoleColors.surface
            border.color: Theme.consoleColors.border
            clip: true

            ListView {
                id: list
                objectName: "consoleList"
                anchors.fill: parent
                anchors.margins: 1
                model: tab.model
                boundsBehavior: Flickable.StopAtBounds
                // Follow the output while at the bottom (followOutput).
                property bool following: true
                onMovementEnded: following = atYEnd
                onCountChanged: if (following) Qt.callLater(positionViewAtEnd)
                onHeightChanged: if (following) Qt.callLater(positionViewAtEnd)
                Component.onCompleted: positionViewAtEnd()
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property string text
                    required property string kind
                    width: ListView.view.width
                    height: Math.max(34, line.implicitHeight + 8)
                    color: "transparent"
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.consoleColors.rowBorder
                    }
                    Icon {
                        id: mark
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        name: tab.icons[kind].name
                        color: tab.icons[kind].color
                        width: 14
                        height: 14
                    }
                    Label {
                        id: line
                        anchors.left: mark.right
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.text
                        wrapMode: Text.WrapAnywhere
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.fontXs
                        font.weight: kind === "alarm" ? Font.Medium : Font.Normal
                        color: Theme.consoleColors[kind]
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: tab.model.hasMessages ? qsTr("No messages match this filter") : qsTr("No console output yet")
                font.family: Theme.monoFont
                font.pixelSize: Theme.fontXs
                font.italic: true
                color: Theme.consoleColors.response
            }
            // Scroll to latest message.
            Rectangle {
                objectName: "consoleLatest"
                visible: !list.following && list.count > 0
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 8
                width: Theme.touchTarget
                height: Theme.touchTarget
                radius: width / 2
                color: Theme.blue[500]
                Icon { anchors.centerIn: parent; name: "LuArrowDown"; color: "white"; width: 16; height: 16 }
                TapHandler {
                    onTapped: {
                        list.following = true
                        list.positionViewAtEnd()
                    }
                }
            }
        }

        // The command line.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: input
                objectName: "consoleInput"
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                placeholderText: qsTr("Enter G-code here...")
                font.family: Theme.monoFont
                font.pixelSize: Theme.fontSm
                color: Theme.contentPrimary
                placeholderTextColor: Theme.contentMuted
                enabled: tab.model.connected
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.dark ? Theme.surfaceSunken : "white"
                    border.color: input.activeFocus ? Theme.ring : Theme.outline
                }
                function execute() {
                    if (tab.model.submit(text))
                        text = ""
                }
                onAccepted: execute()
                Keys.onUpPressed: {
                    const next = tab.model.historyStep(true)
                    if (next !== undefined)
                        text = next
                }
                Keys.onDownPressed: {
                    const next = tab.model.historyStep(false)
                    if (next !== undefined)
                        text = next
                }
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Backspace && text.length <= 1)
                        tab.model.resetHistory()
                    event.accepted = false
                }
            }
            GButton {
                objectName: "consoleSend"
                text: qsTr("Run")
                variant: "primary"
                enabled: tab.model.connected
                Layout.preferredHeight: 40
                Layout.preferredWidth: 96
                onClicked: input.execute()
            }
        }
    }

    // Without a machine.
    Rectangle {
        objectName: "consoleDisconnected"
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceRaised : Theme.gray[50]
        visible: !tab.model.connected
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 8
            Icon {
                Layout.alignment: Qt.AlignHCenter
                name: "LuUnplug"
                color: Theme.contentMuted
                width: 48
                height: 48
            }
            Label {
                text: qsTr("Not connected to a device")
                font.pixelSize: Theme.fontSm
                color: Theme.contentMuted
            }
        }
        TapHandler {}  // blocks the console beneath
    }
}
