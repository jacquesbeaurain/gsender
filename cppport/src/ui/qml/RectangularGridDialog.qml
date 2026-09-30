import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import GSender

// Rectangular grid capture with the 3D probe (the feature/probe-mesh-capture
// branch's MeshProbe.tsx): the grid's spacing and point counts from where
// the probe is parked, the run with its progress and Stop; the probe
// circuit's light as the run step has it; and the points saved as a CSV.
Popup {
    id: dialog
    objectName: "rectangularGrid"

    property ProbeModel probe
    property GridCaptureModel model: GridCaptureModel {}

    function openCapture() {
        probe.beginCheck()
        model.begin()
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    // Never closed out from under a live capture: Stop is the way out.
    closePolicy: model.running ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 16
    width: Math.min(780, parent ? parent.width - 32 : 780)
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
    background: Rectangle {
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceElevated : Theme.gray[100]
        border.color: Theme.outlineSubtle
    }

    readonly property bool canStart: probe && probe.circuitChecked && model.canClick && !model.running

    FileDialog {
        id: saveDialog
        title: qsTr("Save Rectangular Grid")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV files (*.csv)"), qsTr("All files (*)")]
        onAccepted: {
            const result = dialog.model.save(selectedFile)
            Backend.notify(result.message, result.ok ? "success" : "error")
        }
    }

    Connections {
        target: dialog.model
        property string last: "idle"
        function onChanged() {
            const status = dialog.model.status
            if (status === last)
                return
            last = status
            if (status === "failed")
                Backend.notify(dialog.model.statusText, "error")
            else if (status === "done")
                Backend.notify(dialog.model.statusText, "success")
        }
    }

    component Field: ColumnLayout {
        property alias label: fieldLabel.text
        property alias text: input.text
        property alias suffix: suffixLabel.text
        property alias field: input
        spacing: 2
        Label {
            id: fieldLabel
            font.pixelSize: Theme.fontSm
            color: Theme.contentSecondary
        }
        TextField {
            id: input
            Layout.fillWidth: true
            implicitHeight: 40
            enabled: !dialog.model.running
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            selectByMouse: true
            color: Theme.contentPrimary
            font.pixelSize: Theme.fontSm
            rightPadding: suffixLabel.text ? suffixLabel.implicitWidth + 12 : 8
            Label {
                id: suffixLabel
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: Theme.fontSm
                color: Theme.contentMuted
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            Layout.fillWidth: true
            text: qsTr("Capture Rectangular Grid with 3D Probe")
            font.pixelSize: Theme.fontLg
            font.bold: true
            color: Theme.dark ? Theme.contentPrimary : Theme.robin[700]
        }

        RowLayout {
            spacing: 16
            // The grid.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 3
                Layout.alignment: Qt.AlignTop
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.contentPrimary
                    text: qsTr("1. Jog the probe over the first point of the grid, at a height it can safely travel across the work at.")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.contentPrimary
                    text: qsTr("2. Gently push the probe needle to check the circuit is triggered properly (indicated by a green light).")
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.contentPrimary
                    text: qsTr("3. The grid runs from there in +X and +Y, and the probe returns to this height between points.")
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 6
                    columnSpacing: 12
                    Field { id: dx; objectName: "gridDx"; Layout.fillWidth: true; label: qsTr("X spacing"); text: "10"; suffix: dialog.model.units }
                    Field { id: nx; objectName: "gridNx"; Layout.fillWidth: true; label: qsTr("X points"); text: "4" }
                    Field { id: dy; objectName: "gridDy"; Layout.fillWidth: true; label: qsTr("Y spacing"); text: "10"; suffix: dialog.model.units }
                    Field { id: ny; objectName: "gridNy"; Layout.fillWidth: true; label: qsTr("Y points"); text: "4" }
                }
                GButton {
                    objectName: "gridStart"
                    Layout.fillWidth: true
                    variant: dialog.model.running ? "error" : "primary"
                    enabled: dialog.model.running || dialog.canStart
                    text: dialog.model.running ? qsTr("Stop")
                        : dialog.probe && dialog.probe.circuitChecked
                          ? qsTr("Start Grid (%1 points)").arg(Math.max(1, Math.min(200, parseInt(nx.text) || 1))
                                                               * Math.max(1, Math.min(200, parseInt(ny.text) || 1)))
                          : qsTr("Waiting for probe circuit check...")
                    onClicked: {
                        if (dialog.model.running) {
                            dialog.model.stop()
                            return
                        }
                        if (!dialog.model.startGrid(dx.text, nx.text, dy.text, ny.text))
                            Backend.notify(qsTr("Spacing must be a number"), "error")
                    }
                }
            }
            // ProbeCircuitStatus, as the run step shows it.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 2
                Layout.alignment: Qt.AlignTop
                spacing: 8
                Grid {
                    Layout.alignment: Qt.AlignHCenter
                    columns: 4
                    spacing: 8
                    Repeater {
                        model: 16
                        Rectangle { width: 8; height: 8; radius: 4; color: Theme.contentMuted }
                    }
                }
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 6
                    visible: dialog.model.connected
                    Rectangle {
                        objectName: "gridProbeLight"
                        Layout.alignment: Qt.AlignHCenter
                        width: 32; height: 32; radius: 16
                        color: dialog.probe && dialog.probe.circuitChecked ? "#22c55e" : "#ef4444"
                        Icon {
                            anchors.centerIn: parent
                            name: dialog.probe && dialog.probe.circuitChecked ? "FaCheck" : "FaTimes"
                            color: "white"
                            width: 16; height: 16
                        }
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: dialog.probe && dialog.probe.circuitChecked ? qsTr("Touch detected") : qsTr("No Touch")
                        color: Theme.contentPrimary
                    }
                }
                Label {
                    visible: !dialog.model.connected
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("No device connected")
                    color: Theme.contentPrimary
                }
                GButton {
                    objectName: "gridConfirmProbe"
                    visible: dialog.probe && !dialog.probe.circuitChecked
                    Layout.fillWidth: true
                    text: qsTr("Confirm Probe")
                    onClicked: {
                        dialog.probe.confirmCircuit()
                        Backend.notify(qsTr("Probe Confirmed Manually"), "info")
                    }
                }
                Label {
                    visible: dialog.model.simulated
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSm
                    color: Theme.contentMuted
                    text: qsTr("Simulator: a sloping surface has been placed under the probe. Confirm the circuit by hand.")
                }
            }
        }

        // The run and the points held.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label {
                objectName: "gridStatus"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                visible: text !== ""
                text: dialog.model.statusText
                font.pixelSize: Theme.fontSm
                color: dialog.model.status === "failed" ? Theme.red[500] : Theme.contentSecondary
            }
            Label {
                objectName: "gridPoints"
                Layout.fillWidth: true
                text: dialog.model.pointCount === 0 ? qsTr("No points captured")
                    : qsTr("%1 points - last %2").arg(dialog.model.pointCount).arg(dialog.model.lastPoint)
                font.pixelSize: Theme.fontSm
                color: Theme.contentSecondary
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GButton {
                objectName: "gridClear"
                text: qsTr("Clear")
                enabled: !dialog.model.running && dialog.model.pointCount > 0
                onClicked: dialog.model.clear()
            }
            Item { Layout.fillWidth: true }
            GButton {
                objectName: "gridSave"
                text: qsTr("Save CSV...")
                iconName: "FaDownload"
                enabled: !dialog.model.running && dialog.model.pointCount > 0
                onClicked: {
                    saveDialog.currentFile = "file:///" + dialog.model.defaultFileName()
                    saveDialog.open()
                }
            }
            GButton {
                objectName: "gridClose"
                text: qsTr("Close")
                enabled: !dialog.model.running
                onClicked: dialog.close()
            }
        }
    }
}
