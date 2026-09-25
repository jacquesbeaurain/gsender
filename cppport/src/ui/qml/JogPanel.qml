import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Jogging (features/Jogging): the XY wheel with its stop button, the Z (and
// A) tabs; the step and speed fields with their - and + buttons; the
// Rapid / Normal / Precise presets.
ColumnLayout {
    id: jog
    objectName: "jogPanel"

    property JogModel model: JogModel {}

    spacing: 8

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: 24
        JogWheel {
            model: jog.model
            Layout.preferredWidth: 180
            Layout.preferredHeight: 180
        }
        TabJog {
            objectName: "jogZ"
            canJog: jog.model.canJog
            onPressedDirection: (direction) => jog.model.press(0, 0, direction)
            onReleased: jog.model.release()
        }
        TabJog {
            objectName: "jogA"
            visible: jog.model.showA
            labels: "JogALabels"
            canJog: jog.model.canJog
            onPressedDirection: (direction) => jog.model.pressA(direction)
            onReleased: jog.model.release()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        GridLayout {
            columns: jog.model.showA ? 2 : 1
            rowSpacing: 4
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
                    spacing: 4
                    Label {
                        text: input.modelData.label
                        font.pixelSize: Theme.fontSm
                        color: Theme.contentSecondary
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: 22
                    }
                    GButton {
                        objectName: "jogMinus" + input.modelData.field
                        variant: "ghost"
                        text: "−"
                        implicitWidth: 36
                        implicitHeight: 38
                        onClicked: jog.model.nudge(input.modelData.field, false)
                    }
                    NumberField {
                        objectName: "jogField" + input.modelData.field
                        Layout.preferredWidth: 76
                        implicitHeight: 38
                        horizontalAlignment: TextInput.AlignHCenter
                        value: input.modelData.field === "xy" ? jog.model.xyStep
                             : input.modelData.field === "z" ? jog.model.zStep
                             : input.modelData.field === "a" ? jog.model.aStep : jog.model.feedrate
                        onCommitted: (text) => jog.model.setField(input.modelData.field, Number(text))
                    }
                    GButton {
                        objectName: "jogPlus" + input.modelData.field
                        variant: "ghost"
                        text: "+"
                        implicitWidth: 36
                        implicitHeight: 38
                        onClicked: jog.model.nudge(input.modelData.field, true)
                    }
                }
            }
        }
        Item { Layout.fillWidth: true }
        // The presets (SpeedSelector).
        Rectangle {
            implicitWidth: 104
            implicitHeight: presets.implicitHeight + 8
            radius: 4
            color: "transparent"
            border.color: Theme.outlineSubtle
            ColumnLayout {
                id: presets
                anchors.fill: parent
                anchors.margins: 4
                spacing: 2
                Repeater {
                    model: ["Rapid", "Normal", "Precise"]
                    Rectangle {
                        required property string modelData
                        readonly property bool active: jog.model.preset === modelData
                        objectName: "preset" + modelData
                        Layout.fillWidth: true
                        implicitHeight: Theme.touchTarget - 4
                        radius: 4
                        color: active ? Qt.rgba(0x52 / 255, 0x91 / 255, 0xcd / 255, 0.3) : "transparent"
                        border.color: active ? Theme.blue[500] : "transparent"
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
