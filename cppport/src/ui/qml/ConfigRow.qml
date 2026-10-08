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
    // The first and last setting of a section's panel (its corners round).
    property bool first: false
    property bool last: false
    // In a subsection's fieldset (upstream draws one: a border round its
    // settings with the title in the top edge), and its last setting.
    property bool inSub: false
    property bool subLast: false
    signal openTool(string name)

    readonly property string kind: entry.kind || ""
    readonly property string key: entry.key || ""
    readonly property bool isSetting: kind === "setting" || kind === "eeprom"

    clip: true
    implicitHeight: kind === "section" ? 80 : kind === "subsection" ? 56
                  : Math.max(65, content.implicitHeight + 24) + (subLast ? 16 : 0)

    // ---- headings ----
    Label {
        visible: row.kind === "section"
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        text: row.entry.label || ""
        font.pixelSize: 30
    }
    // A subsection: the fieldset's legend on its top edge.
    Rectangle {
        visible: row.kind === "subsection"
        anchors.fill: parent
        anchors.topMargin: row.first ? 0 : -8
        anchors.bottomMargin: -8
        radius: 8
        color: Theme.secondary
    }
    Rectangle {
        visible: row.kind === "subsection"
        x: 20
        y: 30
        width: parent.width - 40
        height: Theme.hairline
        color: Theme.gray[200]
    }
    Label {
        visible: row.kind === "subsection"
        x: 28
        y: 30 - height / 2
        leftPadding: 4
        rightPadding: 4
        text: row.entry.label || ""
        font.pixelSize: 24
        color: Theme.primaryText
        background: Rectangle { color: Theme.secondary }
    }

    // ---- settings and wizards ----
    // The section's panel (gray-100), the row's divider, a change's mark.
    Rectangle {
        visible: row.isSetting || row.kind === "action"
        anchors.fill: parent
        anchors.topMargin: row.first ? 0 : -8
        anchors.bottomMargin: row.last ? 0 : -8
        radius: 8
        color: row.entry.changed ? (Theme.dark ? Qt.rgba(0.98, 0.8, 0.08, 0.12) : Theme.tw.yellow[50])
                                : Theme.secondary
        Rectangle {
            visible: !!row.entry.changed
            y: 8
            width: 4; height: parent.height - 16
            radius: 2
            color: Theme.tw.yellow[500]
        }
    }
    // The fieldset's edges.
    Rectangle { visible: row.inSub; x: 20; width: Theme.hairline; height: parent.height - (row.subLast ? 16 : 0); color: Theme.gray[200] }
    Rectangle { visible: row.inSub; x: parent.width - 20; width: Theme.hairline; height: parent.height - (row.subLast ? 16 : 0); color: Theme.gray[200] }
    Rectangle { visible: row.kind === "subsection"; x: 20; y: 30; width: Theme.hairline; height: parent.height - 30; color: Theme.gray[200] }
    Rectangle { visible: row.kind === "subsection"; x: parent.width - 20; y: 30; width: Theme.hairline; height: parent.height - 30; color: Theme.gray[200] }
    Rectangle {
        visible: row.inSub && row.subLast
        x: 20
        y: parent.height - 16 - Theme.hairline
        width: parent.width - 40
        height: Theme.hairline
        color: Theme.gray[200]
    }
    Rectangle {
        visible: (row.isSetting || row.kind === "action") && !row.last && !row.subLast
        anchors.bottom: parent.bottom
        x: row.inSub ? 20 : 14
        width: parent.width - (row.inSub ? 40 : 28)
        height: Theme.hairline
        color: Theme.gray[200]
    }
    // upstream's three columns: the label, the editor, what it does.
    readonly property real innerWidth: width - (inSub ? 64 : 40)
    RowLayout {
        id: content
        visible: row.isSetting || row.kind === "action"
        x: row.inSub ? 32 : 20
        width: row.innerWidth
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        // The label.
        RowLayout {
            visible: row.isSetting
            Layout.preferredWidth: row.innerWidth * 0.215
            Layout.maximumWidth: row.innerWidth * 0.215
            Layout.alignment: Qt.AlignVCenter
            spacing: 8
            Label {
                Layout.fillWidth: true
                text: row.entry.label || ""
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontBase
                color: Theme.contentBody
            }
        }

        // The editor.
        Loader {
            visible: row.isSetting
            Layout.preferredWidth: row.innerWidth * 0.305 - 28
            Layout.maximumWidth: row.innerWidth * 0.305 - 28
            Layout.rightMargin: 8
            Layout.alignment: Qt.AlignVCenter
            sourceComponent: !row.isSetting ? null
                             : row.kind === "eeprom" ? ({ switch: eepromSwitch, bits: eepromBits, exclusiveBits: eepromBits,
                                                          select: eepromSelect })[row.entry.editor] || eepromText
                             : ({ bool: boolEditor, number: numberEditor, length: numberEditor, speed: numberEditor,
                                  select: selectEditor, radio: radioEditor, text: textEditor, path: pathEditor, textarea: textareaEditor,
                                  location: locationEditor, ip: ipEditor, jog: jogEditor, event: eventEditor })[row.entry.type] || null
        }

        // Back to the default (upstream's BiReset, at the right of the control column).
        Item {
            visible: row.isSetting
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            Icon {
                objectName: "configReset_" + row.key
                visible: !!row.entry.modified
                anchors.centerIn: parent
                name: "BiReset"
                size: 28
                color: resetHover.hovered ? Theme.blue[600] : Theme.contentBody
                HoverHandler { id: resetHover }
                property string tooltip: row.entry.defaultText ? qsTr("Reset to default value (%1)").arg(row.entry.defaultText) : qsTr("Reset to default value")
                TapHandler { onTapped: row.kind === "eeprom" ? row.model.resetEeprom(row.key) : row.model.resetValue(row.key) }
            }
            // Upstream's FaMicrochip: this one lives in the machine's EEPROM.
            Icon {
                objectName: "configChip_" + row.key
                visible: row.kind === "eeprom"
                property string tooltip: qsTr("Machine setting")
                x: 43
                anchors.verticalCenter: parent.verticalCenter
                name: "FaMicrochip"
                size: 36
                color: Theme.robin[500]
            }
        }

        // What it does.
        Label {
            visible: row.isSetting
            Layout.fillWidth: true
            Layout.preferredWidth: row.innerWidth * 0.4
            Layout.leftMargin: row.innerWidth * 0.08 - 20   // it starts 60% along
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            // An EEPROM setting names itself and its default at the end.
            text: (row.entry.description || "")
                  + (row.kind === "eeprom" ? " (" + row.key + (row.entry.defaultText ? ", " + qsTr("Default") + " " + row.entry.defaultText : "") + ")" : "")
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSm
            color: Theme.contentMuted
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
        RowLayout {   // upstream's switch ends where the selects and boxes do
            Item {
                Layout.preferredWidth: 158
                Layout.preferredHeight: 40
                GSwitch {
                    objectName: "configValue_" + row.key
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: !!row.entry.value
                    onToggled: row.model.setValue(row.key, checked)
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: numberEditor
        RowLayout {
            spacing: 8
            // The unit sits inside the box, at its right (upstream's NumberSettingInput).
            NumberField {
                objectName: "configValue_" + row.key
                Layout.preferredWidth: 158
                horizontalAlignment: TextInput.AlignLeft
                suffix: row.entry.unit || ""
                value: Number(row.entry.value)
                decimals: row.entry.decimals !== undefined ? row.entry.decimals : 3
                onCommitted: (text) => row.model.setNumber(row.key, text)
            }
            Item { Layout.fillWidth: true }
        }
    }
    // upstream's RadioSettingInput: a radio button per option, stacked.
    Component {
        id: radioEditor
        Column {
            spacing: 4
            Repeater {
                model: row.entry.options || []
                Row {
                    id: option
                    required property string modelData
                    readonly property bool on: row.entry.value === modelData
                    objectName: "configValue_" + row.key + "_" + modelData
                    spacing: 8
                    Panel {
                        width: 24; height: 24; radius: 12
                        anchors.verticalCenter: parent.verticalCenter
                        color: option.on ? Theme.robin[500] : Theme.surfaceRaised
                        border.color: option.on ? Theme.robin[500] : Theme.blue[500]
                        Rectangle {
                            visible: option.on
                            anchors.centerIn: parent
                            width: 10; height: 10; radius: 5
                            color: "white"
                        }
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: option.modelData
                        font.bold: true
                    }
                    TapHandler { onTapped: row.model.setValue(row.key, option.modelData) }
                }
            }
        }
    }
    Component {
        id: selectEditor
        RowLayout {
            GSelect {
                objectName: "configValue_" + row.key
                Layout.preferredWidth: 160
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
                onAccepted: row.model.setFolder(row.key, selectedFolder)
            }
        }
    }
    Component {
        id: textareaEditor
        Panel {
            implicitHeight: 110
            radius: Theme.radiusSmall
            color: Theme.field
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
                        onCommitted: (text) => row.model.setPart(row.key, index, text)
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
                        onCommitted: (text) => row.model.setPart(row.key, index, text)
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
                        onCommitted: (text) => row.model.setPart(row.key, modelData.field, text)
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
                Label { text: qsTr("Enabled") }
            }
            Panel {
                Layout.fillWidth: true
                implicitHeight: 80
                radius: Theme.radiusSmall
                color: Theme.field
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
            Item {
                Layout.preferredWidth: 158
                Layout.preferredHeight: 40
                GSwitch {
                    objectName: "configValue_" + row.key
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: Number(row.entry.value) !== 0
                    enabled: row.model.idle
                    onToggled: row.model.setEeprom(row.key, checked ? "1" : "0")
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
    Component {
        id: eepromSelect
        RowLayout {
            GSelect {
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
        // BitfieldInput / ExclusiveBitfieldInput / AxisMaskInput: a switch for
        // each bit, their sum the value shown under them.
        Item {
        implicitHeight: bitColumn.implicitHeight
        ColumnLayout {
            id: bitColumn
            objectName: "configValue_" + row.key
            spacing: 8
            width: 158   // the switches end with the boxes and selects
            readonly property int value: Number(row.entry.value) || 0
            Repeater {
                model: row.entry.bits || []
                RowLayout {
                    required property string modelData
                    required property int index
                    readonly property bool on: (parent.value >> index) & 1
                    // Exclusive: the other bits only count with the first set.
                    readonly property bool usable: row.entry.editor !== "exclusiveBits" || index === 0 || (parent.value & 1)
                    Layout.fillWidth: true
                    Label {
                        text: modelData + ":"
                        font.pixelSize: Theme.fontSm
                        color: Theme.contentBody
                        Layout.fillWidth: true
                    }
                    GSwitch {
                        objectName: "configBit_" + row.key + "_" + index
                        checked: parent.on
                        enabled: parent.usable && row.model.idle
                        onToggled: {
                            row.model.toggleEepromBit(row.key, index)
                            checked = Qt.binding(() => parent.on)
                        }
                    }
                }
            }
            RowLayout {
                spacing: 8
                Label { text: qsTr("Value:"); font.pixelSize: Theme.fontXs; font.weight: Font.DemiBold; color: Theme.contentMuted }
                Panel {
                    implicitWidth: valueLabel.implicitWidth + 20
                    implicitHeight: 24
                    radius: 12
                    color: Theme.secondary
                    border.color: Theme.outline
                    Label {
                        id: valueLabel
                        objectName: "configBitValue_" + row.key
                        anchors.centerIn: parent
                        text: bitColumn.value
                        font.pixelSize: Theme.fontSm
                        font.weight: Font.DemiBold
                        color: Theme.contentMuted
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
                id: eepromField
                objectName: "configValue_" + row.key
                Layout.preferredWidth: 158
                implicitHeight: 40
                // The unit sits inside the box, at its right.
                rightPadding: eepromUnit.visible ? eepromUnit.implicitWidth + 16 : 8
                text: row.entry.value || ""
                enabled: row.model.idle
                onEditingFinished: row.model.setEeprom(row.key, text)
                Label {
                    id: eepromUnit
                    visible: text !== ""
                    text: row.entry.unit || ""
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.contentMuted
                    font.pixelSize: Theme.fontXs
                }
            }
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
