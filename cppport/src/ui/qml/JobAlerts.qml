import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The alerts at a job's end (workspace/Alerts): the Job End summary -
// status, time, errors - and the Maintenance Alert with the tasks now due
// and Reset Timers.
Item {
    id: alerts

    Connections {
        target: Backend
        function onJobEndSummary(completed, time, errors) {
            jobEnd.completed = completed
            jobEnd.time = time
            jobEnd.errors = errors
            jobEnd.open()
        }
        function onJobInterrupted(line) {
            interrupted.line = line
            interrupted.open()
        }
        function onMaintenanceDue(tasks) {
            maintenance.tasks = tasks
            maintenance.open()
        }
    }

    component AlertPopup: Popup {
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 24
        width: Math.min(460, parent ? parent.width - 32 : 460)
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
        background: Rectangle { radius: Theme.radius; color: Theme.dark ? Theme.surfaceElevated : "white" }
    }

    AlertPopup {
        id: jobEnd
        objectName: "jobEndSummary"
        property bool completed
        property string time
        property var errors: []
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: qsTr("Job End"); font.pixelSize: Theme.fontLg; font.bold: true; color: Theme.contentPrimary }
            RowLayout {
                Label { text: qsTr("Status:"); font.bold: true; color: Theme.contentPrimary }
                Label {
                    objectName: "jobEndStatus"
                    text: jobEnd.completed ? qsTr("COMPLETE") : qsTr("STOPPED")
                    color: jobEnd.completed ? "#22c55e" : "#ef4444"
                    font.bold: true
                }
            }
            RowLayout {
                Label { text: qsTr("Time:"); font.bold: true; color: Theme.contentPrimary }
                Label { text: jobEnd.time; color: Theme.contentPrimary }
            }
            Label { text: qsTr("Errors:"); font.bold: true; color: Theme.contentPrimary }
            Label {
                text: jobEnd.errors.length ? jobEnd.errors.join("\n") : qsTr("None")
                color: jobEnd.errors.length ? Theme.red[500] : Theme.contentMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            GButton {
                objectName: "jobEndClose"
                Layout.alignment: Qt.AlignRight
                text: qsTr("Close")
                variant: "primary"
                onClicked: jobEnd.close()
            }
        }
    }

    // gSender's recovery prompt: the job can resume from where it stopped.
    AlertPopup {
        id: interrupted
        objectName: "jobInterrupted"
        property real line
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: qsTr("Job interrupted"); font.pixelSize: Theme.fontLg; font.bold: true; color: Theme.contentPrimary }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.contentSecondary
                text: qsTr("The connection closed while the job was running, around line %1.").arg(interrupted.line)
                      + "\n\n" + qsTr("Reconnect (and home if needed), then use Start From Line to resume.")
            }
            GButton {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Close")
                variant: "primary"
                onClicked: interrupted.close()
            }
        }
    }

    AlertPopup {
        id: maintenance
        objectName: "maintenanceAlert"
        property var tasks: []
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: qsTr("Maintenance Alert"); font.pixelSize: Theme.fontLg; font.bold: true; color: Theme.contentPrimary }
            Label {
                text: qsTr("The following maintenance tasks are due:")
                color: Theme.contentSecondary
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Repeater {
                model: maintenance.tasks
                Label {
                    required property var modelData
                    text: "• <b>" + modelData.name + "</b>" + (modelData.description ? " - " + modelData.description : "")
                    textFormat: Text.StyledText
                    wrapMode: Text.Wrap
                    color: Theme.contentPrimary
                    Layout.fillWidth: true
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GButton { text: qsTr("Close"); variant: "outline"; onClicked: maintenance.close() }
                GButton {
                    objectName: "resetTimers"
                    text: qsTr("Reset Timers")
                    variant: "primary"
                    onClicked: {
                        Backend.resetMaintenanceTimers(maintenance.tasks.map(t => t.id))
                        maintenance.close()
                    }
                }
            }
        }
    }
}
