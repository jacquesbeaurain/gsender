import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The run step (features/Probe/RunProbe): what to check, the probe circuit's
// light - green once the pin has triggered, which the routine waits for
// unless the settings skip the check or it is confirmed by hand - and Start
// Probe.
Popup {
    id: dialog
    objectName: "runProbe"

    property ProbeModel model

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 16
    width: Math.min(650, parent ? parent.width - 32 : 650)
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
    background: Rectangle {
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceElevated : Theme.gray[100]
        border.color: Theme.outlineSubtle
    }

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: qsTr("Probe - %1").arg(dialog.model ? dialog.model.commandId : "")
            font.pixelSize: Theme.fontLg
            font.bold: true
            color: Theme.dark ? Theme.contentPrimary : Theme.robin[700]
        }
        RowLayout {
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 3
                spacing: 10
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.contentPrimary
                    text: qsTr("1. Check the tool is positioned correctly (pictured).")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.contentPrimary
                    text: dialog.model && dialog.model.probe3D
                          ? qsTr("2. Gently push the probe needle to check the circuit is triggered properly (indicated by a green light).")
                          : qsTr("2. Lift your touch plate to the tool to check the circuit is good (indicated by a green light), then put it back where it was.")
                }
                Label {
                    visible: dialog.model && !dialog.model.probe3D
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.contentPrimary
                    text: qsTr("3. In some cases, holding the touch plate still while probing will give a more consistent measurement.")
                }
                Label {
                    visible: dialog.model && dialog.model.simulated
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.contentMuted
                    text: qsTr("Simulator: a touch plate has been placed under the bit. Confirm the circuit by hand.")
                }
                Item { Layout.fillHeight: true }
                GButton {
                    objectName: "confirmProbe"
                    visible: dialog.model && !dialog.model.circuitChecked
                    Layout.fillWidth: true
                    text: qsTr("Confirm Probe")
                    onClicked: {
                        dialog.model.confirmCircuit()
                        Backend.notify(qsTr("Probe Confirmed Manually"), "info")
                    }
                }
                GButton {
                    objectName: "startProbe"
                    Layout.fillWidth: true
                    variant: "primary"
                    enabled: dialog.model && dialog.model.circuitChecked && dialog.model.canClick
                    text: dialog.model && dialog.model.circuitChecked ? qsTr("Start Probe")
                                                                      : qsTr("Waiting for probe circuit check...")
                    onClicked: {
                        if (dialog.model.start()) {
                            Backend.notify(qsTr("Initiated probing cycle"), "info")
                            dialog.close()
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 2
                spacing: 8
                AnimatedImage {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180
                    fillMode: Image.PreserveAspectFit
                    source: dialog.model ? dialog.model.image : ""
                    playing: dialog.visible
                }
                // ProbeCircuitStatus.
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6
                    visible: dialog.model && dialog.model.connected
                    Rectangle {
                        objectName: "probeLight"
                        Layout.alignment: Qt.AlignHCenter
                        width: 32; height: 32; radius: 16
                        color: dialog.model && dialog.model.circuitChecked ? "#22c55e" : "#ef4444"
                        Icon {
                            anchors.centerIn: parent
                            name: dialog.model && dialog.model.circuitChecked ? "FaCheck" : "FaTimes"
                            color: "white"
                            width: 16; height: 16
                        }
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: dialog.model && dialog.model.circuitChecked ? qsTr("Touch detected") : qsTr("No Touch")
                        color: Theme.contentPrimary
                    }
                }
                Label {
                    visible: dialog.model && !dialog.model.connected
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("No device connected")
                    color: Theme.contentPrimary
                }
            }
        }
    }
}
