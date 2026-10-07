import QtCore
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import GSender

// Gamepad (features/Gamepad): the profiles (ProfileList: open one, delete
// it, add one for the pad whose button is pressed, Help), and a profile
// (Profile): its name, whether its pad is connected, Import and Export, the
// buttons' actions (ButtonActionsTable, SetShortcut) and the joystick
// options (JoystickOptions). While the page is open the pads run nothing:
// pressing a button lights its row.
ToolPage {
    id: tool
    objectName: "gamepadTool"
    title: qsTr("Gamepad")

    property GamepadModel model: GamepadModel { objectName: "gamepad" }

    // The profile list.
    Item {
        anchors.fill: parent
        visible: tool.model.current < 0

        ColumnLayout {
            anchors.centerIn: parent
            visible: tool.model.profiles.length === 0
            spacing: 32
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("No Profiles, Click the Button Below to Add One")
                font.pixelSize: 24
                color: Theme.contentPrimary
            }
            Loader {
                Layout.alignment: Qt.AlignHCenter
                // One of the two at a time, so their buttons' names are unique.
                active: tool.model.profiles.length === 0
                sourceComponent: actionArea
            }
        }

        Flickable {
            anchors.fill: parent
            anchors.topMargin: 8
            visible: tool.model.profiles.length > 0
            contentHeight: list.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: list
                width: parent.width
                spacing: 16
                Label {
                    visible: !tool.model.available
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("This build cannot read gamepads (it has no SDL3); profiles can still be edited.")
                    color: Theme.contentMuted
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 16
                    columnSpacing: 16
                    Repeater {
                        model: tool.model.profiles.length
                        Rectangle {
                            id: card
                            required property int index
                            readonly property var profile: tool.model.profiles[index] || ({})
                            objectName: "gamepadProfile_" + index
                            Layout.fillWidth: true
                            Layout.preferredHeight: 140
                            radius: Theme.radius
                            color: cardTap.pressed ? Theme.gray[300] : (Theme.dark ? Theme.surfaceRaised : Theme.gray[100])
                            border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                            border.width: Theme.hairline
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 16
                                spacing: 12
                                Label {
                                    Layout.fillWidth: true
                                    Layout.rightMargin: Theme.touchTarget
                                    horizontalAlignment: Text.AlignHCenter
                                    text: card.profile.name || ""
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.fontXl
                                    font.weight: Font.DemiBold
                                    color: Theme.contentPrimary
                                }
                                Item { Layout.fillHeight: true }
                                Icon {
                                    Layout.alignment: Qt.AlignHCenter
                                    name: "FaGamepad"
                                    color: card.profile.connected ? Theme.green[500] : Theme.contentPrimary
                                    width: 48; height: 48
                                }
                            }
                            TapHandler { id: cardTap; onTapped: tool.model.current = card.index }
                            GButton {
                                objectName: "gamepadDelete_" + card.index
                                anchors.top: parent.top
                                anchors.right: parent.right
                                variant: "ghost"
                                iconName: "FaTimes"
                                iconColor: Theme.red[500]
                                iconSize: 16
                                implicitWidth: Theme.touchTarget
                                onClicked: {
                                    deleteConfirm.index = card.index
                                    deleteConfirm.open()
                                }
                            }
                        }
                    }
                }
                Loader { active: tool.model.profiles.length > 0; sourceComponent: actionArea }
            }
        }
    }

    Component {
        id: actionArea
        RowLayout {
            spacing: 16
            GButton {
                objectName: "gamepadAdd"
                variant: "secondary"
                iconName: "FaPlus"
                iconSize: 16
                text: qsTr("Add New Gamepad Profile")
                onClicked: addPopup.open()
            }
            GButton {
                objectName: "gamepadHelp"
                variant: "warning"
                text: qsTr("Help")
                onClicked: helpPopup.open()
            }
        }
    }

    // A profile.
    ColumnLayout {
        anchors.fill: parent
        visible: tool.model.current >= 0
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            GButton {
                objectName: "gamepadBack"
                variant: "secondary"
                iconName: "LuArrowLeft"
                iconSize: 22
                text: qsTr("Back to Profiles")
                onClicked: tool.model.current = -1
            }
            Item { Layout.fillWidth: true }
            TextField {
                id: nameField
                objectName: "gamepadName"
                Layout.preferredWidth: 280
                implicitHeight: Theme.touchTarget
                horizontalAlignment: TextInput.AlignHCenter
                font.pixelSize: Theme.fontXl
                font.bold: true
                color: Theme.contentPrimary
                text: tool.model.name
                // A long name shows its start.
                onTextChanged: if (!activeFocus) cursorPosition = 0
                onEditingFinished: {
                    if (text.trim() !== "" && text.trim() !== tool.model.name) {
                        tool.model.rename(text)
                        Backend.notify(qsTr("Updated Profile Name"), "info")
                    }
                    text = Qt.binding(() => tool.model.name)
                }
            }
            Rectangle {
                objectName: "gamepadStatus"
                implicitWidth: statusLabel.implicitWidth + 32
                implicitHeight: 36
                radius: height / 2
                color: tool.model.padConnected ? Theme.green[500] : Theme.gray[500]
                Label {
                    id: statusLabel
                    anchors.centerIn: parent
                    text: tool.model.padConnected ? qsTr("Connected") : qsTr("Disconnected")
                    color: "white"
                }
            }
            Item { Layout.fillWidth: true }
            GButton {
                objectName: "gamepadImport"
                variant: "secondary"
                iconName: "FaFileImport"
                iconSize: 16
                text: qsTr("Import")
                onClicked: importDialog.open()
            }
            GButton {
                objectName: "gamepadExport"
                variant: "secondary"
                iconName: "FaFileExport"
                iconSize: 16
                text: qsTr("Export")
                onClicked: {
                    exportDialog.selectedFile = StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/" + tool.model.exportName()
                    exportDialog.open()
                }
            }
            GButton {
                objectName: "gamepadProfileHelp"
                variant: "warning"
                text: qsTr("Help")
                onClicked: helpPopup.open()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            spacing: 16

            // Button Actions.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 8
                Label { text: qsTr("Button Actions"); font.pixelSize: Theme.fontXl; font.bold: true; color: Theme.contentPrimary }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("Assign a \"Lockout\" button for gamepad safety, or a \"2nd Action\" button to use like a Function key and give your gamepad double the functions!")
                    color: Theme.contentPrimary
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radius
                    color: Theme.dark ? Theme.surfaceRaised : "white"
                    border.color: Theme.dark ? Theme.outline : Theme.gray[300]
                    border.width: Theme.hairline
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
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 8
                                Label { text: qsTr("Button"); font.bold: true; color: Theme.contentPrimary; Layout.preferredWidth: 100 }
                                Label { text: qsTr("Action"); font.bold: true; color: Theme.contentPrimary; Layout.fillWidth: true; Layout.preferredWidth: 1 }
                                Label { text: qsTr("2nd Action"); font.bold: true; color: Theme.contentPrimary; Layout.fillWidth: true; Layout.preferredWidth: 1 }
                            }
                        }
                        ListView {
                            id: buttonTable
                            objectName: "gamepadButtons"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            // By count, so a change keeps the table where it is.
                            model: tool.model.buttons.length
                            ScrollBar.vertical: GScrollBar {}
                            delegate: Item {
                                id: row
                                required property int index
                                readonly property var button: tool.model.buttons[index] || ({})
                                readonly property bool down: !!tool.model.pressed[button.value]
                                width: buttonTable.width
                                implicitHeight: 60
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 8
                                    TextField {
                                        objectName: "gamepadLabel_" + row.button.value
                                        Layout.preferredWidth: 100
                                        implicitHeight: 40
                                        horizontalAlignment: TextInput.AlignHCenter
                                        text: row.button.label !== undefined ? row.button.label : ""
                                        color: row.down ? "white" : Theme.contentPrimary
                                        background: Rectangle {
                                            radius: Theme.radiusSmall
                                            color: row.down ? Theme.green[500] : (Theme.dark ? Theme.surfaceSunken : "white")
                                            border.color: row.down ? Theme.green[500] : Theme.outline
                                            border.width: Theme.hairline
                                        }
                                        onEditingFinished: tool.model.setLabel(row.button.value, text)
                                    }
                                    // The lockout and 2nd-action buttons: their role over both columns.
                                    Rectangle {
                                        visible: row.button.role !== ""
                                        Layout.fillWidth: true
                                        implicitHeight: 44
                                        radius: Theme.radiusSmall
                                        color: row.down ? Theme.green[500] : (Theme.dark ? Theme.surfaceElevated : Theme.gray[200])
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 8
                                            anchors.rightMargin: 4
                                            Label {
                                                objectName: "gamepadRole_" + row.button.value
                                                Layout.fillWidth: true
                                                text: row.button.role === "lockout" ? qsTr("Lockout") : qsTr("Activate 2nd Actions")
                                                color: row.down ? "white" : Theme.contentPrimary
                                            }
                                            GButton {
                                                objectName: "gamepadRoleClear_" + row.button.value
                                                variant: "ghost"
                                                iconName: "FaTrash"
                                                iconColor: Theme.red[500]
                                                iconSize: 16
                                                implicitWidth: Theme.touchTarget
                                                onClicked: row.button.role === "lockout" ? tool.model.setLockout(row.button.value, false)
                                                                                         : tool.model.setModifier(row.button.value, false)
                                            }
                                        }
                                    }
                                    GamepadActionCell {
                                        visible: row.button.role === ""
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        namePrefix: "gamepadPrimary_" + row.button.value
                                        actionId: row.button.primary || ""
                                        title: row.button.primaryTitle || ""
                                        highlighted: actionId !== "" && row.down && !tool.model.modifierHeld
                                        onEdit: actionPopup.edit(row.button.value, false)
                                        onClear: tool.model.clearAction(row.button.value, false)
                                    }
                                    GamepadActionCell {
                                        visible: row.button.role === ""
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        namePrefix: "gamepadSecondary_" + row.button.value
                                        actionId: row.button.secondary || ""
                                        title: row.button.secondaryTitle || ""
                                        highlighted: actionId !== "" && row.down && tool.model.modifierHeld
                                        onEdit: actionPopup.edit(row.button.value, true)
                                        onClear: tool.model.clearAction(row.button.value, true)
                                    }
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: Theme.dark ? Theme.outline : Theme.gray[200]
                                }
                            }
                        }
                    }
                }
            }

            // Joystick Options.
            ColumnLayout {
                Layout.preferredWidth: Math.max(420, tool.width * 0.4)
                Layout.maximumWidth: Math.max(420, tool.width * 0.4)
                Layout.fillHeight: true
                spacing: 8
                Label { text: qsTr("Joystick Options"); font.pixelSize: Theme.fontXl; font.bold: true; color: Theme.contentPrimary }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radius
                    color: "transparent"
                    border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                    border.width: Theme.hairline
                    Flickable {
                        anchors.fill: parent
                        anchors.margins: 8
                        contentHeight: options.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        GridLayout {
                            id: options
                            width: parent.width
                            columns: 4
                            rowSpacing: 8
                            columnSpacing: 8

                            readonly property var axes: tool.model.axes
                            readonly property bool modifier: tool.model.modifierHeld
                            function moved(stick, direction) {
                                const a = axes
                                const base = stick === "stick1" ? 0 : 2
                                const x = a.length > base ? a[base] : 0
                                const y = a.length > base + 1 ? a[base + 1] : 0
                                return direction === "horizontal" ? x !== 0 : direction === "vertical" ? y !== 0 : x !== 0 || y !== 0
                            }
                            function mpg(stick, field) {
                                const s = tool.model.joystick[stick]
                                return !!(s && s.mpgMode[field])
                            }

                            Item { Layout.row: 0; Layout.column: 0; Layout.preferredWidth: 1; Layout.fillWidth: true; implicitHeight: 1 }
                            Label { Layout.row: 0; Layout.column: 1; text: qsTr("Action"); font.bold: true; color: Theme.contentPrimary; Layout.preferredWidth: 1; Layout.fillWidth: true }
                            Label { Layout.row: 0; Layout.column: 2; text: qsTr("2nd Action"); font.bold: true; color: Theme.contentPrimary; Layout.preferredWidth: 1; Layout.fillWidth: true }
                            Label { Layout.row: 0; Layout.column: 3; text: qsTr("Invert"); font.bold: true; color: Theme.contentPrimary; Layout.preferredWidth: 60 }

                            Repeater {
                                model: [
                                    { stick: "stick1", direction: "horizontal", label: qsTr("Stick 1 left/right") },
                                    { stick: "stick1", direction: "vertical", label: qsTr("Stick 1 up/down") },
                                    { stick: "stick1", direction: "mpgMode", label: qsTr("Stick 1 MPG") },
                                    { stick: "stick2", direction: "horizontal", label: qsTr("Stick 2 left/right") },
                                    { stick: "stick2", direction: "vertical", label: qsTr("Stick 2 up/down") },
                                    { stick: "stick2", direction: "mpgMode", label: qsTr("Stick 2 MPG") }
                                ]
                                delegate: Label {
                                    required property var modelData
                                    Layout.row: 1 + index
                                    Layout.column: 0
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    required property int index
                                    text: modelData.label
                                    wrapMode: Text.Wrap
                                    color: Theme.contentPrimary
                                }
                            }
                            Repeater {
                                model: 12
                                delegate: GSelect {
                                    id: axisBox
                                    required property int index
                                    readonly property var row: tool.stickRows[Math.floor(index / 2)]
                                    readonly property string field: index % 2 ? "secondaryAction" : "primaryAction"
                                    readonly property var action: (tool.model.joystick[row.stick] || {})[row.direction] || ({})
                                    readonly property bool isMpg: row.direction === "mpgMode"
                                    // An MPG axis turns the stick's two directions off.
                                    readonly property bool active: (isMpg ? options.mpg(row.stick, field) : !options.mpg(row.stick, field))
                                                                   && options.moved(row.stick, row.direction)
                                                                   && options.modifier === (field === "secondaryAction")
                                    objectName: "gamepadAxis_" + row.stick + "_" + row.direction + "_" + field
                                    Layout.row: 1 + Math.floor(index / 2)
                                    Layout.column: 1 + index % 2
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    implicitHeight: 40
                                    enabled: isMpg || !options.mpg(row.stick, field)
                                    model: ["None", "X", "Y", "Z", "A"]
                                    currentIndex: Math.max(0, ["", "x", "y", "z", "a"].indexOf(action[field] || ""))
                                    onActivated: (i) => tool.model.setStick(row.stick, row.direction, field, ["", "x", "y", "z", "a"][i])
                                    background: Rectangle {
                                        radius: Theme.radiusSmall
                                        color: axisBox.active ? Theme.green[500] : (!axisBox.enabled ? Theme.surfaceDisabled : (Theme.dark ? Theme.surfaceSunken : "white"))
                                        border.color: axisBox.active ? Theme.green[500] : Theme.outline
                                        border.width: Theme.hairline
                                    }
                                    contentItem: Label {
                                        leftPadding: 8
                                        text: axisBox.displayText
                                        verticalAlignment: Text.AlignVCenter
                                        horizontalAlignment: Text.AlignHCenter
                                        color: axisBox.active ? "white" : Theme.contentPrimary
                                    }
                                }
                            }
                            Repeater {
                                model: 6
                                delegate: Item {
                                    required property int index
                                    readonly property var row: tool.stickRows[index]
                                    Layout.row: 1 + index
                                    Layout.column: 3
                                    Layout.preferredWidth: 60
                                    implicitHeight: Theme.touchTarget
                                    GSwitch {
                                        objectName: "gamepadInvert_" + parent.row.stick + "_" + parent.row.direction
                                        anchors.verticalCenter: parent.verticalCenter
                                        checked: !!((tool.model.joystick[parent.row.stick] || {})[parent.row.direction] || {}).isReversed
                                        enabled: parent.row.direction === "mpgMode"
                                                 || !(options.mpg(parent.row.stick, "primaryAction") && options.mpg(parent.row.stick, "secondaryAction"))
                                        onToggled: tool.model.setStick(parent.row.stick, parent.row.direction, "isReversed", checked)
                                    }
                                }
                            }

                            Label {
                                Layout.row: 7
                                Layout.column: 0
                                text: qsTr("Zero threshold")
                                color: Theme.contentPrimary
                            }
                            NumberField {
                                objectName: "gamepadZeroThreshold"
                                Layout.row: 7
                                Layout.column: 1
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                decimals: 0
                                suffix: "%"
                                value: tool.model.joystick.zeroThreshold || 0
                                onCommitted: (text) => tool.model.setOption("zeroThreshold", Number(text))
                            }
                            Label {
                                Layout.row: 8
                                Layout.column: 0
                                text: qsTr("Movement override")
                                color: Theme.contentPrimary
                            }
                            NumberField {
                                objectName: "gamepadMovementOverride"
                                Layout.row: 8
                                Layout.column: 1
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                suffix: "%"
                                value: tool.model.joystick.movementDistanceOverride || 0
                                onCommitted: (text) => tool.model.setOption("movementDistanceOverride", Number(text))
                            }
                            Label {
                                Layout.row: 9
                                Layout.column: 0
                                text: qsTr("Fixed speed mode")
                                color: Theme.contentPrimary
                            }
                            RowLayout {
                                Layout.row: 9
                                Layout.column: 1
                                Layout.columnSpan: 3
                                Layout.fillWidth: true
                                spacing: 8
                                GSwitch {
                                    objectName: "gamepadFixedSpeed"
                                    checked: !!tool.model.joystick.fixedSpeedMode
                                    onToggled: tool.model.setOption("fixedSpeedMode", checked)
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: qsTr("For gamepads that don't work well with variable speed jogging")
                                    font.pixelSize: Theme.fontXs
                                    color: Theme.contentMuted
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    readonly property var stickRows: [
        { stick: "stick1", direction: "horizontal" },
        { stick: "stick1", direction: "vertical" },
        { stick: "stick1", direction: "mpgMode" },
        { stick: "stick2", direction: "horizontal" },
        { stick: "stick2", direction: "vertical" },
        { stick: "stick2", direction: "mpgMode" }
    ]

    ConfirmDialog {
        id: deleteConfirm
        objectName: "gamepadDeleteConfirm"
        property int index: -1
        title: qsTr("Delete Gamepad Profile")
        message: qsTr("Are you sure you want to delete this gamepad profile?")
        actionText: qsTr("Delete")
        actionVariant: "error"
        onAccepted: {
            tool.model.removeProfile(index)
            Backend.notify(qsTr("Removed Gamepad Profile"), "info")
        }
    }

    // Add Gamepad Profile (ProfileModal): press a button on the pad.
    Popup {
        id: addPopup
        objectName: "gamepadAddPopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 24
        width: Math.min(460, parent ? parent.width - 32 : 460)
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.dark ? Theme.surfaceElevated : "white"
            border.color: Theme.outlineSubtle
            border.width: Theme.hairline
        }
        onAboutToShow: {
            tool.model.resetDetection()
            addName.text = ""
        }

        Connections {
            target: tool.model
            function onDetectChanged() {
                if (addPopup.visible && tool.model.detectedState === "available" && addName.text === "") {
                    addName.text = tool.model.detectedId
                    addName.forceActiveFocus()
                }
            }
        }

        contentItem: ColumnLayout {
            spacing: 16
            Label { text: qsTr("Add Gamepad Profile"); font.pixelSize: Theme.fontLg; font.bold: true; color: Theme.contentPrimary }
            ColumnLayout {
                objectName: "gamepadAvailability"
                Layout.fillWidth: true
                spacing: 8
                readonly property string state: tool.model.detectedState
                Icon {
                    Layout.alignment: Qt.AlignHCenter
                    name: parent.state === "available" ? "FaCheck" : parent.state === "exists" ? "FaTimes" : "FaGamepad"
                    color: parent.state === "available" ? Theme.green[600] : parent.state === "exists" ? Theme.red[500] : Theme.primaryText
                    width: 24; height: 24
                }
                Label {
                    objectName: "gamepadAvailabilityText"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: parent.state === "available" ? qsTr("Profile Is Available")
                        : parent.state === "exists" ? qsTr("Profile Already Exists")
                        : qsTr("Connect your device and press any button on it")
                    color: parent.state === "available" ? Theme.green[600] : parent.state === "exists" ? Theme.red[500] : Theme.contentPrimary
                }
                Label {
                    visible: parent.state !== ""
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("Device ID: %1").arg(tool.model.detectedId)
                    font.pixelSize: Theme.fontXs
                    color: Theme.contentMuted
                }
            }
            TextField {
                id: addName
                objectName: "gamepadAddName"
                Layout.fillWidth: true
                implicitHeight: 40
                placeholderText: qsTr("Enter a profile name here...")
            }
            GButton {
                objectName: "gamepadAddConfirm"
                Layout.fillWidth: true
                variant: "primary"
                text: qsTr("Add New Profile")
                enabled: tool.model.detectedState === "available" && addName.text.trim() !== ""
                onClicked: {
                    if (tool.model.addProfile(addName.text)) {
                        Backend.notify(qsTr("Created New Gamepad Profile"), "info")
                        addPopup.close()
                    }
                }
            }
        }
    }

    // Set Gamepad Profile Shortcut (SetShortcut): an action for a button,
    // or the button as the lockout or 2nd-action one.
    Popup {
        id: actionPopup
        objectName: "gamepadActionPopup"

        property int button: -1
        property bool secondary: false
        property string selected
        property bool changed: false
        readonly property var mapping: {
            const list = tool.model.buttons
            for (let i = 0; i < list.length; ++i) {
                if (list[i].value === button)
                    return list[i]
            }
            return {}
        }

        function edit(value, second) {
            button = value
            secondary = second
            selected = (second ? mapping.secondary : mapping.primary) || ""
            changed = false
            open()
        }

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 24
        width: parent ? Math.min(1200, parent.width * 0.85) : 900
        height: parent ? Math.min(760, parent.height - 64) : 700
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.dark ? Theme.surfaceElevated : "white"
            border.color: Theme.outlineSubtle
            border.width: Theme.hairline
        }

        contentItem: ColumnLayout {
            spacing: 12
            Label { text: qsTr("Set Gamepad Profile Shortcut"); font.pixelSize: Theme.fontLg; font.bold: true; color: Theme.contentPrimary }
            Label { text: qsTr("Use the gamepad to set the shortcut for the current button."); color: Theme.contentMuted }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusSmall
                    color: "transparent"
                    border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                    border.width: Theme.hairline
                    clip: true
                    Flickable {
                        id: actionList
                        objectName: "gamepadActionList"
                        anchors.fill: parent
                        anchors.margins: 8
                        contentHeight: categories.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: GScrollBar {}
                        ColumnLayout {
                            id: categories
                            width: actionList.width - 12
                            spacing: 12
                            Repeater {
                                model: tool.model.actionCategories.length
                                RowLayout {
                                    id: category
                                    required property int index
                                    readonly property var entry: tool.model.actionCategories[index] || ({ category: "", actions: [] })
                                    Layout.fillWidth: true
                                    spacing: 12
                                    Rectangle {
                                        Layout.preferredWidth: 140
                                        Layout.alignment: Qt.AlignTop
                                        implicitHeight: 32
                                        radius: 4
                                        color: Theme.dark ? Theme.surfaceRaised : Theme.gray[200]
                                        Label {
                                            anchors.centerIn: parent
                                            text: category.entry.category
                                            font.pixelSize: Theme.fontSm
                                            color: Theme.contentPrimary
                                        }
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Repeater {
                                            model: category.entry.actions.length
                                            GButton {
                                                required property int index
                                                readonly property var shortcut: category.entry.actions[index] || ({})
                                                objectName: "gamepadAction_" + (shortcut.id || "")
                                                variant: shortcut.id === actionPopup.selected ? "primary" : "secondary"
                                                text: shortcut.title || ""
                                                fontSize: Theme.fontSm
                                                onClicked: {
                                                    if (actionPopup.selected !== shortcut.id) {
                                                        actionPopup.selected = shortcut.id
                                                        actionPopup.changed = true
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // The lockout and 2nd-action buttons take no action.
                    Rectangle {
                        anchors.fill: parent
                        visible: actionPopup.mapping.role === "lockout" || actionPopup.mapping.role === "modifier"
                        color: Qt.rgba(0.5, 0.5, 0.5, 0.4)
                        TapHandler {}
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: 200
                    Layout.alignment: Qt.AlignTop
                    spacing: 8
                    Label { text: qsTr("Use as Lockout button"); color: Theme.contentPrimary }
                    GSwitch {
                        objectName: "gamepadLockoutSwitch"
                        checked: actionPopup.mapping.role === "lockout"
                        onToggled: {
                            tool.model.setLockout(actionPopup.button, checked)
                            actionPopup.changed = true
                        }
                    }
                    Label { text: qsTr("Use as 2nd Action button"); color: Theme.contentPrimary; Layout.topMargin: 24 }
                    GSwitch {
                        objectName: "gamepadModifierSwitch"
                        checked: actionPopup.mapping.role === "modifier"
                        onToggled: {
                            tool.model.setModifier(actionPopup.button, checked)
                            actionPopup.changed = true
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Label { text: qsTr("Shortcut:"); color: Theme.contentPrimary }
                Label {
                    text: actionPopup.mapping.label !== undefined ? actionPopup.mapping.label : ""
                    font.family: "monospace"
                    color: Theme.contentPrimary
                }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("Action:"); color: Theme.contentPrimary }
                Rectangle {
                    implicitWidth: selectedTitle.implicitWidth + 24
                    implicitHeight: 32
                    color: Theme.primary
                    Label {
                        id: selectedTitle
                        objectName: "gamepadSelectedAction"
                        anchors.centerIn: parent
                        color: "white"
                        text: actionPopup.mapping.role === "lockout" ? qsTr("Lockout Button")
                            : actionPopup.mapping.role === "modifier" ? qsTr("Activate Secondary Action Button")
                            : actionPopup.selected !== "" ? tool.model.actionTitle(actionPopup.selected) : "..."
                    }
                }
                Item { Layout.fillWidth: true }
                GButton {
                    objectName: "gamepadCancelShortcut"
                    variant: "outline"
                    text: qsTr("Cancel")
                    onClicked: actionPopup.close()
                }
                GButton {
                    objectName: "gamepadSetShortcut"
                    variant: "primary"
                    text: qsTr("Set Shortcut")
                    enabled: actionPopup.changed
                    onClicked: {
                        if (actionPopup.selected !== "")
                            tool.model.setAction(actionPopup.button, actionPopup.secondary, actionPopup.selected)
                        actionPopup.close()
                        Backend.notify(qsTr("Button Shortcut Set"), "info")
                    }
                }
            }
        }
    }

    // Help with Gamepad.
    ConfirmDialog {
        id: helpPopup
        objectName: "gamepadHelpDialog"
        title: qsTr("Help with Gamepad")
        message: qsTr("Your gamepad setup needs to work correctly for shortcuts to behave as expected. If you are experiencing issues, use this online diagnostics tool to verify its stability: hardwaretester.com/gamepad")
        actionText: qsTr("Hardware Tester")
        cancelText: qsTr("Close")
        onAccepted: Qt.openUrlExternally("https://hardwaretester.com/gamepad")
    }

    FileDialog {
        id: importDialog
        title: qsTr("Import Gamepad Profile")
        nameFilters: [qsTr("Gamepad profiles (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const result = tool.model.importFile(selectedFile)
            Backend.notify(result.message, result.ok ? "success" : "error")
        }
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export Gamepad Profile")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Gamepad profiles (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const result = tool.model.exportFile(selectedFile)
            Backend.notify(result.message, result.ok ? "success" : "error")
        }
    }
}
