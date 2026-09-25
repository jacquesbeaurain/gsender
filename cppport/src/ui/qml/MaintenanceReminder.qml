import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A MaintenancePreview entry: the hours to go, the task, and how pressing
// it is (Low, Soon, Due, Urgent!) in its colour.
Rectangle {
    property var task: ({})
    readonly property color tone: task.color || Theme.green[500]

    objectName: "maintenanceReminder"
    implicitHeight: reminderRow.implicitHeight + 16
    radius: 14
    color: Qt.rgba(tone.r, tone.g, tone.b, 0.08)
    RowLayout {
        id: reminderRow
        anchors.fill: parent
        anchors.margins: 8
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Label { text: task.hours || ""; font.pixelSize: 24; color: parent.parent.parent.tone }
            Label { text: task.name || ""; color: Theme.contentMuted; elide: Text.ElideRight; Layout.fillWidth: true }
        }
        Label { text: (task.word || "") + "  ●"; font.pixelSize: 24; color: parent.parent.tone }
    }
}
