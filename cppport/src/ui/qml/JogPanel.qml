import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Jogging (features/Jogging): the XY wheel with its stop button, the Z (and
// A) tabs; the step and speed fields with their - and + buttons; the
// Precise / Normal / Rapid presets.
//
// Sizes follow upstream's page at the 1280 px window (max-xl: the wheel and
// tabs are scaled to 90%; the fields and presets are not).
ColumnLayout {
    id: jog
    objectName: "jogPanel"

    property JogModel model: JogModel {}

    // Upstream's max-xl:scale-90 on the wheel row, below the xl breakpoint.
    readonly property real js: Window.window && Window.window.width <= 1280 ? 0.9 : 1

    spacing: 0

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: 86 * jog.js
        JogWheel {
            model: jog.model
            Layout.preferredWidth: 180 * jog.js
            Layout.preferredHeight: 180 * jog.js
        }
        TabJog {
            objectName: "jogZ"
            Layout.preferredWidth: 45 * jog.js
            Layout.preferredHeight: 168 * jog.js
            canJog: jog.model.canJog
            onPressedDirection: (direction) => jog.model.press(0, 0, direction)
            onReleased: jog.model.release()
        }
        TabJog {
            objectName: "jogA"
            visible: jog.model.showA
            Layout.preferredWidth: 45 * jog.js
            Layout.preferredHeight: 168 * jog.js
            labels: "JogALabels"
            canJog: jog.model.canJog
            onPressedDirection: (direction) => jog.model.pressA(direction)
            onReleased: jog.model.release()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 36
        Layout.leftMargin: 44
        Layout.rightMargin: 35
        spacing: 8

        GridLayout {
            columns: jog.model.showA ? 2 : 1
            rowSpacing: 8.7
            columnSpacing: 8
            Layout.alignment: Qt.AlignVCenter
            Repeater {
                model: [
                    { field: "xy", label: "XY" },
                    { field: "z", label: "Z" },
                    { field: "a", label: "A°" },
                    { field: "feedrate", label: "at" }
                ]
                RowLayout {
                    id: input
                    required property var modelData
                    visible: modelData.field !== "a" || jog.model.showA
                    spacing: 0
                    Label {
                        text: input.modelData.label
                        font.pixelSize: Theme.fontSm
                        color: Theme.gray[400]
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 24
                        Layout.rightMargin: 9
                    }
                    GButton {
                        objectName: "jogMinus" + input.modelData.field
                        iconName: "FaMinus"
                        iconSize: 12
                        implicitWidth: 23
                        implicitHeight: 24
                        onClicked: jog.model.nudge(input.modelData.field, false)
                    }
                    NumberField {
                        objectName: "jogField" + input.modelData.field
                        Layout.preferredWidth: 65
                        implicitHeight: 24
                        font.pixelSize: Theme.fontBase
                        horizontalAlignment: TextInput.AlignLeft
                        leftPadding: 4
                        topPadding: 0
                        bottomPadding: 0
                        verticalAlignment: TextInput.AlignVCenter
                        color: Theme.dark ? Theme.contentPrimary : Theme.robin[500]
                        background: Rectangle {   // upstream's input: white (raised, outlined in dark)
                            radius: 4
                            color: Theme.dark ? Theme.surfaceRaised : "white"
                            border.color: parent.activeFocus ? Theme.ring : (Theme.dark ? Theme.outline : "transparent")
                        }
                        value: input.modelData.field === "xy" ? jog.model.xyStep
                             : input.modelData.field === "z" ? jog.model.zStep
                             : input.modelData.field === "a" ? jog.model.aStep : jog.model.feedrate
                        onCommitted: (text) => jog.model.setField(input.modelData.field, Number(text))
                    }
                    GButton {
                        objectName: "jogPlus" + input.modelData.field
                        iconName: "FaPlus"
                        iconSize: 12
                        implicitWidth: 23
                        implicitHeight: 24
                        onClicked: jog.model.nudge(input.modelData.field, true)
                    }
                }
            }
        }
        Item { Layout.fillWidth: true }
        // The presets (SpeedSelector).
        Rectangle {
            implicitWidth: 92
            implicitHeight: presets.implicitHeight
            radius: 8
            color: Theme.dark ? Theme.surfaceRaised : "white"
            border.color: Theme.outlineSubtle
            ColumnLayout {
                id: presets
                anchors.fill: parent
                spacing: 0
                Repeater {
                    model: ["Precise", "Normal", "Rapid", "Custom"]
                    Rectangle {
                        required property string modelData
                        readonly property bool active: jog.model.preset === modelData
                        objectName: "preset" + modelData
                        Layout.fillWidth: true
                        implicitHeight: 26
                        radius: 4
                        color: active ? Qt.rgba(0x52 / 255, 0x91 / 255, 0xcd / 255, 0.3) : "transparent"
                        Label {
                            anchors.centerIn: parent
                            text: qsTr(parent.modelData)
                            font.pixelSize: Theme.fontSm
                            font.weight: parent.active ? Font.DemiBold : Font.Normal
                            color: parent.active ? (Theme.dark ? Theme.blue[300] : Theme.blue[700]) : Theme.contentPrimary
                        }
                        TapHandler { onTapped: jog.model.selectPreset(parent.modelData) }
                    }
                }
            }
        }
    }
}
