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
    readonly property bool compact: Window.window && Window.window.width <= 1280
    implicitHeight: reminderRow.implicitHeight + 16
    radius: 16
    color: Qt.rgba(tone.r, tone.g, tone.b, 0.05)
    RowLayout {
        id: reminderRow
        anchors.fill: parent
        anchors.margins: 8
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Label { text: task.hours || ""; font.pixelSize: compact ? 20 : 30; color: parent.parent.parent.tone }
            Label {
                text: task.name || ""
                color: Theme.dark ? Theme.contentMuted : Theme.gray[700]
                font.pixelSize: compact ? Theme.fontSm : Theme.fontBase
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        Row {
            spacing: 32
            Label { text: task.word || ""; font.pixelSize: compact ? 20 : 30; color: parent.parent.parent.tone }
            Icon { name: "FaCircle"; width: 16; height: 16; color: parent.parent.parent.tone; anchors.verticalCenter: parent.verticalCenter }
        }
    }
}
