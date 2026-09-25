import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import GSender

// A Config row: a section's or subsection's heading, a setting (gSender's
// or the board's) with its editor - changed ones marked, those away from
// their default resettable - or a section's wizard (test buttons, pins).
Item {
    id: row
    objectName: "config_" + key

    property var entry: ({})
    property ConfigModel model
    signal openTool(string name)

    readonly property string kind: entry.kind || ""
    readonly property string key: entry.key || ""
    readonly property bool isSetting: kind === "setting" || kind === "eeprom"

    implicitHeight: kind === "section" ? 64 : kind === "subsection" ? 44 : content.implicitHeight + 20

    // ---- headings ----
    Label {
        visible: row.kind === "section"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        text: row.entry.label || ""
        font.pixelSize: 26
        font.bold: true
        color: Theme.contentPrimary
    }
    Label {
        visible: row.kind === "subsection"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 6
        text: row.entry.label || ""
        font.pixelSize: Theme.fontLg
        font.bold: true
        color: Theme.contentSecondary
    }

    // ---- settings and wizards ----
    Rectangle {
        visible: row.isSetting || row.kind === "action"
        anchors.fill: parent
        anchors.topMargin: 2
        anchors.bottomMargin: 2
        radius: Theme.radius
        color: row.entry.changed ? (Theme.dark ? Qt.rgba(0.98, 0.8, 0.08, 0.12) : "#fefce8")
                                : (Theme.dark ? Theme.surfaceRaised : "white")
        border.color: Theme.dark ? Theme.outline : Theme.gray[200]
        Rectangle {
            visible: !!row.entry.changed
            width: 4; height: parent.height
            radius: 2
            color: "#eab308"
        }
    }
    RowLayout {
        id: content
        visible: row.isSetting || row.kind === "action"
        x: 16
        width: parent.width - 32
        anchors.verticalCenter: parent.verticalCenter
        spacing: 16

        // The label and what it does.
        ColumnLayout {
            visible: row.isSetting
            Layout.preferredWidth: 1
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2
            RowLayout {
                spacing: 8
                Rectangle {
                    visible: row.kind === "eeprom"
                    implicitWidth: keyLabel.implicitWidth + 10
                    implicitHeight: 20
                    radius: 4
                    color: Theme.dark ? Theme.surfaceElevated : Theme.gray[100]
                    Label {
                        id: keyLabel
                        anchors.centerIn: parent
                        text: row.key
                        font.family: "monospace"
                        font.pixelSize: Theme.fontXs
                        color: Theme.contentSecondary
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: row.entry.label || ""
                    font.bold: true
                    wrapMode: Text.Wrap
                    color: Theme.contentPrimary
                }
            }
            Label {
                visible: text !== ""
                Layout.fillWidth: true
                text: row.entry.description || ""
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSm
                color: Theme.contentMuted
            }
        }

        // The editor.
        Loader {
            visible: row.isSetting
            Layout.preferredWidth: 1
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            sourceComponent: !row.isSetting ? null
                             : row.kind === "eeprom" ? ({ switch: eepromSwitch, bits: eepromBits, exclusiveBits: eepromBits,
                                                          select: eepromSelect })[row.entry.editor] || eepromText
                             : ({ bool: boolEditor, number: numberEditor, length: numberEditor, speed: numberEditor,
                                  select: selectEditor, text: textEditor, path: pathEditor, textarea: textareaEditor,
                                  location: locationEditor, ip: ipEditor, jog: jogEditor, event: eventEditor })[row.entry.type] || null
        }

        // Back to the default.
        Item {
            visible: row.isSetting
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            GButton {
                objectName: "configReset_" + row.key
                visible: !!row.entry.modified
                anchors.centerIn: parent
                variant: "ghost"
                iconName: "FaRedo"
                iconSize: 14
                ToolTip.visible: hovered
                ToolTip.text: row.entry.defaultText ? qsTr("Reset to default value (%1)").arg(row.entry.defaultText) : qsTr("Reset to default value")
                onClicked: row.kind === "eeprom" ? row.model.resetEeprom(row.key) : row.model.resetValue(row.key)
            }
        }

        // A section's wizard.
        Loader {
            visible: row.kind === "action"
            Layout.fillWidth: true
            sourceComponent: row.kind === "action" ? actionRow : null
        }
    }

    // ---- gSender's settings ----
    Component {
        id: boolEditor
        RowLayout {
            GSwitch {
                objectName: "configValue_" + row.key
                checked: !!row.entry.value
                onToggled: row.model.setValue(row.key, checked)
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: numberEditor
        RowLayout {
            spacing: 8
            NumberField {
                objectName: "configValue_" + row.key
                Layout.preferredWidth: 160
                value: Number(row.entry.value)
                decimals: row.entry.decimals !== undefined ? row.entry.decimals : 3
                onCommitted: (text) => {
                    const n = Number(text)
                    if (text !== "" && !isNaN(n))
                        row.model.setValue(row.key, Math.min(row.entry.max, Math.max(row.entry.min, n)))
                }
            }
            Label { text: row.entry.unit || ""; color: Theme.contentMuted; font.pixelSize: Theme.fontSm }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: selectEditor
        RowLayout {
            ComboBox {
                objectName: "configValue_" + row.key
                Layout.preferredWidth: 240
                model: row.entry.options || []
                currentIndex: (row.entry.options || []).indexOf(row.entry.value)
                onActivated: (index) => row.model.setValue(row.key, row.entry.options[index])
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: textEditor
        TextField {
            objectName: "configValue_" + row.key
            implicitHeight: 40
            text: row.entry.value || ""
            onEditingFinished: row.model.setValue(row.key, text)
        }
    }
    Component {
        id: pathEditor
        RowLayout {
            spacing: 8
            TextField {
                id: pathField
                objectName: "configValue_" + row.key
                Layout.fillWidth: true
                implicitHeight: 40
                placeholderText: qsTr("Application data folder")
                text: row.entry.value || ""
                onEditingFinished: row.model.setValue(row.key, text)
            }
            GButton {
                text: qsTr("Browse...")
                onClicked: folder.open()
            }
            FolderDialog {
                id: folder
                title: row.entry.label || ""
                onAccepted: {
                    const path = decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//, ""))
                    row.model.setValue(row.key, path)
                }
            }
        }
    }
    Component {
        id: textareaEditor
        Rectangle {
            implicitHeight: 110
            radius: Theme.radiusSmall
            color: Theme.dark ? Theme.surfaceSunken : "white"
            border.color: area.activeFocus ? Theme.ring : Theme.outline
            ScrollView {
                anchors.fill: parent
                anchors.margins: 4
                TextArea {
                    id: area
                    objectName: "configValue_" + row.key
                    text: row.entry.value || ""
                    font.family: "monospace"
                    font.pixelSize: Theme.fontSm
                    color: Theme.contentPrimary
                    placeholderText: qsTr("; No commands set")
                    background: null
                    onActiveFocusChanged: if (!activeFocus) row.model.setValue(row.key, text)
                }
            }
        }
    }
    Component {
        id: locationEditor
        ColumnLayout {
            spacing: 6
            RowLayout {
                spacing: 6
                Repeater {
                    model: ["X", "Y", "Z"]
                    NumberField {
                        required property string modelData
                        required property int index
                        objectName: "configValue_" + row.key + "_" + modelData
                        Layout.fillWidth: true
                        value: row.entry.value ? row.entry.value[index] : 0
                        leftPadding: 24
                        onCommitted: (text) => {
                            const n = Number(text)
                            if (text === "" || isNaN(n))
                                return
                            const at = row.entry.value.slice()
                            at[index] = n
                            row.model.setValue(row.key, at)
                        }
                        Label {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.modelData
                            font.bold: true
                            color: Theme.contentMuted
                        }
                    }
                }
                Label { text: "mm"; color: Theme.contentMuted; font.pixelSize: Theme.fontSm }
            }
            RowLayout {
                spacing: 8
                GButton {
                    objectName: "configUseCurrent_" + row.key
                    text: qsTr("Use current")
                    enabled: row.model.connected
                    onClicked: row.model.useCurrentPosition(row.key)
                }
                GButton {
                    visible: row.key === "park"
                    text: qsTr("Go to")
                    enabled: row.model.idle
                    onClicked: row.model.goToLocation(row.key)
                }
            }
        }
    }
    Component {
        id: ipEditor
        RowLayout {
            spacing: 4
            Repeater {
                model: 4
                RowLayout {
                    required property int index
                    spacing: 4
                    Label { visible: index > 0; text: "."; color: Theme.contentMuted }
                    NumberField {
                        objectName: "configValue_" + row.key + "_" + index
                        Layout.preferredWidth: 64
                        horizontalAlignment: TextInput.AlignHCenter
                        decimals: 0
                        value: row.entry.value ? row.entry.value[index] : 0
                        onCommitted: (text) => {
                            const n = Number(text)
                            if (text === "" || isNaN(n))
                                return
                            const ip = row.entry.value.slice()
                            ip[index] = Math.max(0, Math.min(255, Math.round(n)))
                            row.model.setValue(row.key, ip)
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: jogEditor
        GridLayout {
            columns: 4
            columnSpacing: 6
            rowSpacing: 2
            Repeater {
                model: [
                    { field: "xyStep", label: qsTr("XY"), unit: row.entry.unit },
                    { field: "zStep", label: qsTr("Z"), unit: row.entry.unit },
                    { field: "aStep", label: qsTr("A"), unit: qsTr("deg") },
                    { field: "feedrate", label: qsTr("Speed"), unit: row.entry.unit + "/min" }
                ]
                ColumnLayout {
                    required property var modelData
                    spacing: 2
                    Layout.fillWidth: true
                    Label { text: modelData.label + " (" + modelData.unit + ")"; color: Theme.contentMuted; font.pixelSize: Theme.fontXs }
                    NumberField {
                        objectName: "configValue_" + row.key + "_" + modelData.field
                        Layout.fillWidth: true
                        value: row.entry.value ? row.entry.value[modelData.field] : 0
                        onCommitted: (text) => {
                            const n = Number(text)
                            if (text === "" || isNaN(n) || n < 0)
                                return
                            const speeds = Object.assign({}, row.entry.value)
                            speeds[modelData.field] = n
                            row.model.setValue(row.key, speeds)
                        }
                    }
                }
            }
        }
    }
    Component {
        id: eventEditor
        ColumnLayout {
            spacing: 6
            RowLayout {
                GSwitch {
                    objectName: "configValue_" + row.key + "_enabled"
                    checked: !!(row.entry.value && row.entry.value.enabled)
                    onToggled: row.model.setValue(row.key, { enabled: checked, commands: row.entry.value.commands })
                }
                Label { text: qsTr("Enabled"); color: Theme.contentPrimary }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 80
                radius: Theme.radiusSmall
                color: Theme.dark ? Theme.surfaceSunken : "white"
                border.color: commands.activeFocus ? Theme.ring : Theme.outline
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 4
                    TextArea {
                        id: commands
                        objectName: "configValue_" + row.key + "_commands"
                        text: row.entry.value ? row.entry.value.commands : ""
                        font.family: "monospace"
                        font.pixelSize: Theme.fontSm
                        color: Theme.contentPrimary
                        placeholderText: qsTr("; No commands set")
                        background: null
                        onActiveFocusChanged: if (!activeFocus)
                            row.model.setValue(row.key, { enabled: row.entry.value.enabled, commands: text })
                    }
                }
            }
        }
    }

    // ---- the board's settings ----
    Component {
        id: eepromSwitch
        RowLayout {
            GSwitch {
                objectName: "configValue_" + row.key
                checked: Number(row.entry.value) !== 0
                enabled: row.model.idle
                onToggled: row.model.setEeprom(row.key, checked ? "1" : "0")
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: eepromSelect
        RowLayout {
            ComboBox {
                objectName: "configValue_" + row.key
                Layout.preferredWidth: 260
                model: row.entry.bits || []
                currentIndex: Number(row.entry.value)
                enabled: row.model.idle
                onActivated: (index) => row.model.setEeprom(row.key, String(index))
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: eepromBits
        // BitfieldInput / ExclusiveBitfieldInput / AxisMaskInput: the bits
        // chosen, their sum the value.
        Flow {
            objectName: "configValue_" + row.key
            spacing: 6
            readonly property int value: Number(row.entry.value) || 0
            Repeater {
                model: row.entry.bits || []
                Rectangle {
                    required property string modelData
                    required property int index
                    readonly property bool on: (parent.value >> index) & 1
                    // Exclusive: the other bits only count with the first set.
                    readonly property bool usable: row.entry.editor !== "exclusiveBits" || index === 0 || (parent.value & 1)
                    objectName: "configBit_" + row.key + "_" + index
                    implicitWidth: bitLabel.implicitWidth + 20
                    implicitHeight: 32
                    radius: 16
                    opacity: usable && row.model.idle ? 1 : 0.5
                    color: on ? Theme.blue[500] : "transparent"
                    border.color: on ? Theme.blue[500] : Theme.outline
                    Label {
                        id: bitLabel
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: Theme.fontSm
                        color: parent.on ? "white" : Theme.contentPrimary
                    }
                    TapHandler {
                        enabled: parent.usable && row.model.idle
                        onTapped: {
                            const mask = 1 << parent.index
                            const value = parent.on ? (parent.parent.value & ~mask) : (parent.parent.value | mask)
                            row.model.setEeprom(row.key, String(value))
                        }
                    }
                }
            }
        }
    }
    Component {
        id: eepromText
        RowLayout {
            spacing: 8
            TextField {
                objectName: "configValue_" + row.key
                Layout.preferredWidth: 200
                implicitHeight: 40
                horizontalAlignment: TextInput.AlignRight
                text: row.entry.value || ""
                enabled: row.model.idle
                onEditingFinished: row.model.setEeprom(row.key, text)
            }
            Label { text: row.entry.unit || ""; color: Theme.contentMuted; font.pixelSize: Theme.fontSm }
            Item { Layout.fillWidth: true }
        }
    }

    // ---- the sections' wizards ----
    Component {
        id: actionRow
        RowLayout {
            spacing: 8
            Label {
                text: ({ keyboardShortcuts: qsTr("Keyboard shortcuts"), squareXY: qsTr("Square up CNC rails"),
                         jogWizard: qsTr("Test motion"), probePin: qsTr("Probe pin"), limitPins: qsTr("Limit switches"),
                         spindleTest: qsTr("Test spindle"), laserTest: qsTr("Test laser"),
                         outputsTest: qsTr("Accessory outputs"), aJog: qsTr("Test rotary") })[row.key] || ""
                font.bold: true
                color: Theme.contentPrimary
                Layout.preferredWidth: 200
            }
            // Pins lit while the status report lists them.
            Repeater {
                model: row.key === "probePin" ? ["P"] : row.key === "limitPins" ? ["X", "Y", "Z", "A"] : []
                Rectangle {
                    required property string modelData
                    objectName: "configPin_" + modelData
                    readonly property bool lit: row.model.pinState.indexOf(modelData) >= 0
                    width: 30; height: 26; radius: 4
                    color: lit ? Theme.green[500] : Theme.gray[500]
                    Label { anchors.centerIn: parent; text: parent.modelData; color: "white"; font.bold: true }
                }
            }
            Repeater {
                model: ({
                    spindleTest: [{ label: qsTr("For"), command: "M3 S1000" }, { label: qsTr("Rev"), command: "M4 S1000" }, { label: qsTr("Stop"), command: "M5 S0" }],
                    laserTest: [{ label: qsTr("Laser On"), command: "G1F1 M3 S1" }, { label: qsTr("Laser Off"), command: "M5 S0" }],
                    outputsTest: ["M3", "M4", "M5", "M7", "M8", "M9"].map(c => ({ label: c, command: c }))
                })[row.key] || []
                GButton {
                    required property var modelData
                    text: modelData.label
                    enabled: row.model.connected
                    onClicked: row.model.sendTest(modelData.command)
                }
            }
            Repeater {
                model: row.key === "jogWizard" ? ["X", "Y", "Z"].flatMap(a => [{ axis: a, distance: -10 }, { axis: a, distance: 10 }])
                       : row.key === "aJog" ? [{ axis: "A", distance: -10 }, { axis: "A", distance: 10 }] : []
                GButton {
                    required property var modelData
                    objectName: "configJog_" + modelData.axis + (modelData.distance < 0 ? "Minus" : "Plus")
                    text: qsTr("Jog %1%2").arg(modelData.axis).arg(modelData.distance < 0 ? "-" : "+")
                    enabled: row.model.idle
                    onClicked: row.model.jogAxis(modelData.axis, modelData.distance)
                }
            }
            GButton {
                visible: row.key === "keyboardShortcuts" || row.key === "squareXY"
                objectName: "configOpen_" + row.key
                text: row.key === "squareXY" ? qsTr("Square XY...") : qsTr("Edit shortcuts...")
                onClicked: row.openTool(row.key === "squareXY" ? "squaring" : "shortcuts")
            }
            Item { Layout.fillWidth: true }
        }
    }
}
