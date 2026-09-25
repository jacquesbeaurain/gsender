import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Keyboard Shortcuts (features/Keyboard): the switch for them all, a
// search and a category; the table - each action's keys (tap to change),
// category and switch; Reset All to Defaults (asked first). Changes are
// kept as they are made.
ToolPage {
    id: tool
    objectName: "keyboardShortcutsTool"
    title: qsTr("Keyboard Shortcuts")

    property ShortcutsModel model: ShortcutsModel { objectName: "shortcuts" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            GSwitch {
                objectName: "shortcutsEnabled"
                checked: tool.model.enabled
                onToggled: tool.model.enabled = checked
            }
            Label { text: qsTr("Enable keyboard shortcuts"); color: Theme.contentPrimary }
            Item { Layout.fillWidth: true }
            TextField {
                objectName: "shortcutsSearch"
                Layout.preferredWidth: 240
                implicitHeight: 40
                placeholderText: qsTr("Search shortcuts")
                onTextChanged: tool.model.search = text
            }
            ComboBox {
                objectName: "shortcutsCategory"
                Layout.preferredWidth: 200
                model: tool.model.categories
                onActivated: (index) => tool.model.category = tool.model.categories[index]
            }
            GButton {
                objectName: "shortcutsReset"
                variant: "outline"
                text: qsTr("Reset All to Defaults")
                onClicked: resetConfirm.open()
            }
        }

        // The table.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: "transparent"
            border.color: Theme.dark ? Theme.outline : Theme.gray[200]
            clip: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    color: Theme.dark ? Theme.surfaceRaised : Theme.gray[100]
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 12
                        Label { text: qsTr("Action"); font.bold: true; color: Theme.contentPrimary; Layout.fillWidth: true }
                        Label { text: qsTr("Shortcut"); font.bold: true; color: Theme.contentPrimary; Layout.preferredWidth: 200 }
                        Label { text: qsTr("Category"); font.bold: true; color: Theme.contentPrimary; Layout.preferredWidth: 160 }
                        Label { text: qsTr("Active"); font.bold: true; color: Theme.contentPrimary; Layout.preferredWidth: 60 }
                    }
                }
                ListView {
                    id: table
                    objectName: "shortcutsTable"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    // By count, so a change keeps the table where it is.
                    model: tool.model.rows.length
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: row
                        required property int index
                        readonly property var action: tool.model.rows[index] || ({})
                        objectName: "shortcutRow_" + (action.id || "")
                        width: table.width
                        implicitHeight: 52
                        color: index % 2 ? (Theme.dark ? Theme.surfaceRaised : Theme.gray[50]) : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 12
                            Label {
                                text: row.action.title || ""
                                color: Theme.contentPrimary
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Item {
                                Layout.preferredWidth: 200
                                Layout.fillHeight: true
                                Rectangle {
                                    objectName: "shortcutKeys_" + (row.action.id || "")
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.max(72, keysLabel.implicitWidth + 24)
                                    height: 32
                                    radius: 4
                                    color: keysTap.pressed ? Theme.gray[300] : (Theme.dark ? Theme.surfaceElevated : "white")
                                    border.color: Theme.dark ? Theme.outline : Theme.gray[300]
                                    Label {
                                        id: keysLabel
                                        anchors.centerIn: parent
                                        text: row.action.keys || qsTr("None")
                                        font.family: "monospace"
                                        font.pixelSize: Theme.fontSm
                                        color: row.action.keys ? Theme.contentPrimary : Theme.contentMuted
                                    }
                                    TapHandler { id: keysTap; onTapped: keyEditor.edit(row.action.id, row.action.title) }
                                }
                            }
                            Label {
                                text: row.action.category || ""
                                color: Theme.contentSecondary
                                font.pixelSize: Theme.fontSm
                                elide: Text.ElideRight
                                Layout.preferredWidth: 160
                            }
                            Item {
                                Layout.preferredWidth: 60
                                Layout.fillHeight: true
                                GSwitch {
                                    objectName: "shortcutActive_" + (row.action.id || "")
                                    anchors.verticalCenter: parent.verticalCenter
                                    checked: !!row.action.active
                                    onToggled: tool.model.setActive(row.action.id, checked)
                                }
                            }
                        }
                    }
                }
            }
        }
        Label {
            text: qsTr("Tap a shortcut to change it. Jogging shortcuts jog while held.")
            font.pixelSize: Theme.fontSm
            color: Theme.contentMuted
        }
    }

    // Recording an action's keys.
    Popup {
        id: keyEditor
        objectName: "shortcutEditor"

        property string actionId
        property string actionTitle
        property string keys
        property string conflict

        function edit(id, title) {
            actionId = id
            actionTitle = title
            keys = tool.model.keys(id)
            conflict = ""
            open()
        }

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 24
        width: Math.min(460, parent ? parent.width - 32 : 460)
        onOpened: capture.forceActiveFocus()
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.dark ? Theme.surfaceElevated : "white"
            border.color: Theme.outlineSubtle
        }

        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: keyEditor.actionTitle
                font.pixelSize: Theme.fontLg
                font.bold: true
                color: Theme.contentPrimary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label { text: qsTr("Press the new keys:"); color: Theme.contentSecondary }
            Rectangle {
                id: capture
                objectName: "shortcutCapture"
                // The window's shortcuts leave these keys alone.
                property bool capturesKeys: true
                Layout.fillWidth: true
                implicitHeight: 56
                radius: 6
                focus: true
                color: Theme.dark ? Theme.surfaceSunken : Theme.gray[50]
                border.color: activeFocus ? Theme.ring : Theme.outline
                border.width: activeFocus ? 2 : 1
                Label {
                    objectName: "shortcutCaptured"
                    anchors.centerIn: parent
                    text: keyEditor.keys ? tool.model.nativeKeys(keyEditor.keys) : qsTr("None")
                    font.family: "monospace"
                    font.pixelSize: Theme.fontLg
                    color: Theme.contentPrimary
                }
                TapHandler { onTapped: capture.forceActiveFocus() }
                Keys.onPressed: (event) => {
                    const keys = tool.model.keyFor(event.key, event.modifiers, event.text)
                    if (keys !== "") {
                        keyEditor.keys = keys
                        keyEditor.conflict = ""
                    }
                    event.accepted = true
                }
            }
            Label {
                objectName: "shortcutConflict"
                visible: keyEditor.conflict !== ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.red[500]
                text: qsTr("%1 is already used by \"%2\".").arg(tool.model.nativeKeys(keyEditor.keys)).arg(keyEditor.conflict)
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 8
                GButton {
                    objectName: "shortcutNone"
                    variant: "outline"
                    text: qsTr("None")
                    onClicked: { keyEditor.keys = ""; keyEditor.conflict = ""; capture.forceActiveFocus() }
                }
                GButton {
                    objectName: "shortcutDefault"
                    variant: "outline"
                    text: qsTr("Default")
                    onClicked: { keyEditor.keys = tool.model.defaultKeys(keyEditor.actionId); keyEditor.conflict = ""; capture.forceActiveFocus() }
                }
                Item { Layout.fillWidth: true }
                GButton {
                    objectName: "shortcutCancel"
                    variant: "outline"
                    text: qsTr("Cancel")
                    onClicked: keyEditor.close()
                }
                GButton {
                    objectName: "shortcutSave"
                    variant: "primary"
                    text: qsTr("Save")
                    onClicked: {
                        keyEditor.conflict = tool.model.setKeys(keyEditor.actionId, keyEditor.keys)
                        if (keyEditor.conflict === "")
                            keyEditor.close()
                        else
                            capture.forceActiveFocus()  // ready for other keys
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: resetConfirm
        objectName: "shortcutsResetConfirm"
        title: qsTr("Reset All to Defaults")
        message: qsTr("Every keyboard shortcut goes back to gSender's own keys. Continue?")
        actionText: qsTr("Reset")
        onAccepted: tool.model.resetAll()
    }
}
