import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Go To Location (DRO/component/GoTo): absolute or incremental work
// coordinates, or machine coordinates (homing enabled and homed); the fields
// start at the position (zeros for incremental), in the workspace units.
Popup {
    id: popup
    objectName: "goToPopup"

    property DroModel model
    property string mode: "ABS"

    function fill() {
        const values = model.goToPrefill(mode)
        for (let i = 0; i < 4; ++i)
            fields.itemAt(i).value = values[i]
    }

    padding: 16
    width: 280
    onOpened: {
        if (mode === "MCS" && !model.machineCoordinatesAvailable)
            mode = "ABS"
        fill()
    }
    onModeChanged: if (opened) fill()

    background: Rectangle {
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceElevated : "white"
        border.color: Theme.outline
    }

    contentItem: ColumnLayout {
        spacing: 8
        Label {
            text: qsTr("Go To Location")
            font.pixelSize: Theme.fontBase
            color: Theme.contentPrimary
        }
        // The mode switch.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.touchTarget
            radius: 4
            color: "transparent"
            border.color: Theme.outline
            clip: true
            RowLayout {
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                Repeater {
                    model: ["ABS", "INC", "MCS"]
                    Rectangle {
                        required property string modelData
                        readonly property bool available: modelData !== "MCS" || popup.model.machineCoordinatesAvailable
                        objectName: "goToMode" + modelData
                        visible: modelData !== "MCS" || popup.model.homingEnabled
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: popup.mode === modelData ? Theme.blue[600] : "transparent"
                        opacity: available ? 1 : 0.4
                        Label {
                            anchors.centerIn: parent
                            text: parent.modelData
                            font.pixelSize: Theme.fontXs
                            font.weight: Font.Medium
                            color: popup.mode === parent.modelData ? "white" : Theme.contentSecondary
                        }
                        TapHandler {
                            onTapped: if (parent.available) popup.mode = parent.modelData
                        }
                    }
                }
            }
        }
        Repeater {
            id: fields
            model: ["X", "Y", "Z", "A"]
            RowLayout {
                required property string modelData
                required property int index
                property alias value: field.value
                Layout.fillWidth: true
                Label {
                    text: parent.modelData
                    font.family: Theme.monoFont
                    font.bold: true
                    color: Theme.contentPrimary
                    Layout.preferredWidth: 20
                }
                NumberField {
                    id: field
                    objectName: "goTo" + parent.modelData
                    Layout.fillWidth: true
                    implicitHeight: Theme.touchTarget
                    suffix: parent.index === 3 ? "°" : popup.model.units
                    enabled: parent.index === 1 ? !popup.model.rotaryMode
                           : parent.index === 2 ? popup.mode !== "MCS"
                           : parent.index === 3 ? popup.model.goToAEnabled : true
                    onCommitted: (text) => value = Number(text)
                }
            }
        }
        GButton {
            objectName: "goToGo"
            Layout.fillWidth: true
            variant: "primary"
            text: qsTr("Go!")
            enabled: popup.model.canClick
            onClicked: {
                // Take what is being typed too.
                forceActiveFocus()
                popup.model.goTo(popup.mode, fields.itemAt(0).value, fields.itemAt(1).value,
                                 fields.itemAt(2).value, fields.itemAt(3).value)
            }
        }
    }
}
