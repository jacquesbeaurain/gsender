import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Machine Information (features/MachineInfo): the firmware version, the CNC
// modals, the input pins (green on, red off), the tool, and the stepper
// lock ($1=255).
Popup {
    id: popup
    objectName: "machineInfo"

    property StatusModel model

    padding: 16
    width: 460
    background: Rectangle {
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceElevated : "white"
        border.color: Theme.outline
    }
    contentItem: ColumnLayout {
        spacing: 10
        Label {
            textFormat: Text.StyledText
            text: qsTr("<b>Firmware version:</b> %1").arg(popup.model.firmwareVersion)
            color: Theme.contentPrimary
        }
        RowLayout {
            spacing: 16
            Layout.fillWidth: true
            GridLayout {
                columns: 2
                rowSpacing: 4
                Layout.alignment: Qt.AlignTop
                Label { text: qsTr("CNC Modals"); font.bold: true; color: Theme.contentPrimary; Layout.columnSpan: 2 }
                Repeater {
                    model: popup.model.modals.length * 2
                    Label {
                        required property int index
                        readonly property var row: popup.model.modals[Math.floor(index / 2)] || ({})
                        text: index % 2 === 0 ? row.label : row.value
                        font.pixelSize: Theme.fontSm
                        font.family: index % 2 === 0 ? font.family : Theme.monoFont
                        color: index % 2 === 0 ? Theme.contentMuted : Theme.contentPrimary
                    }
                }
            }
            GridLayout {
                columns: 2
                rowSpacing: 4
                Layout.alignment: Qt.AlignTop
                Label { text: qsTr("Pins"); font.bold: true; color: Theme.contentPrimary; Layout.columnSpan: 2 }
                Repeater {
                    model: popup.model.pins.length * 2
                    Item {
                        required property int index
                        readonly property var pin: popup.model.pins[Math.floor(index / 2)] || ({})
                        implicitWidth: index % 2 === 0 ? label.implicitWidth : 40
                        implicitHeight: 20
                        Label {
                            id: label
                            visible: parent.index % 2 === 0
                            text: parent.pin.label || ""
                            font.pixelSize: Theme.fontSm
                            color: Theme.contentMuted
                        }
                        Rectangle {
                            visible: parent.index % 2 === 1
                            anchors.fill: parent
                            radius: 4
                            color: parent.pin.on ? "#22c55e" : "#ef4444"
                            Label {
                                anchors.centerIn: parent
                                text: parent.parent.pin.on ? qsTr("On") : qsTr("Off")
                                font.pixelSize: Theme.fontXs
                                color: "white"
                            }
                        }
                    }
                }
            }
        }
        Label {
            visible: popup.model.tool >= 0
            text: qsTr("Current tool: T%1").arg(popup.model.tool)
            color: Theme.contentPrimary
        }
        RowLayout {
            GSwitch {
                objectName: "stepperLock"
                checked: popup.model.stepperLocked
                enabled: popup.model.connected
                onToggled: popup.model.setStepperLock(checked)
            }
            Label { text: qsTr("Lock stepper motors"); color: Theme.contentPrimary }
        }
    }
}
