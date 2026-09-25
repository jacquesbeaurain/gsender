import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Add New Task / Edit Task (MaintenanceAddTaskDialog, MaintenanceEditTaskDialog
// and their MaintenanceTaskForm): the name, the range of hours it is due in,
// the description; Delete (asked first) when editing.
Popup {
    id: dialog
    objectName: "maintenanceTaskDialog"

    property StatsModel model
    property int taskId: -1
    signal deleteRequested(int id, string name)

    function openFor(id) {
        taskId = id
        const task = id >= 0 ? model.task(id) : ({})
        name.text = task.name || ""
        start.text = task.rangeStart || ""
        end.text = task.rangeEnd || ""
        description.text = task.description || ""
        nameError.text = ""
        rangeError.text = ""
        open()
    }
    function submit() {
        nameError.text = model.nameProblem(name.text)
        rangeError.text = model.rangeProblem(start.text, end.text)
        if (nameError.text || rangeError.text)
            return
        if (model.saveTask(taskId, name.text, start.text, end.text, description.text))
            close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 24
    width: Math.min(520, parent ? parent.width - 32 : 520)
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
    background: Rectangle {
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceElevated : "white"
        border.color: Theme.outlineSubtle
    }

    contentItem: ColumnLayout {
        spacing: 8
        Label {
            text: dialog.taskId >= 0 ? qsTr("Edit Task") : qsTr("Add New Task")
            font.pixelSize: Theme.fontLg
            font.bold: true
            color: Theme.contentPrimary
        }
        Label { text: qsTr("Task Name"); color: Theme.contentPrimary }
        TextField {
            id: name
            objectName: "taskName"
            Layout.fillWidth: true
            implicitHeight: 40
            color: Theme.contentPrimary
        }
        Label { id: nameError; visible: text !== ""; color: Theme.red[500]; font.pixelSize: Theme.fontSm }
        Label { text: qsTr("Maintenance Range (hours)"); color: Theme.contentPrimary }
        RowLayout {
            Layout.fillWidth: true
            TextField { id: start; objectName: "taskRangeStart"; Layout.fillWidth: true; implicitHeight: 40; inputMethodHints: Qt.ImhFormattedNumbersOnly; color: Theme.contentPrimary }
            Label { text: qsTr("to"); color: Theme.contentMuted }
            TextField { id: end; objectName: "taskRangeEnd"; Layout.fillWidth: true; implicitHeight: 40; inputMethodHints: Qt.ImhFormattedNumbersOnly; color: Theme.contentPrimary }
        }
        Label { id: rangeError; visible: text !== ""; color: Theme.red[500]; font.pixelSize: Theme.fontSm }
        Label { text: qsTr("Description"); color: Theme.contentPrimary }
        TextArea {
            id: description
            objectName: "taskDescription"
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            wrapMode: TextEdit.Wrap
            color: Theme.contentPrimary
            background: Rectangle { radius: Theme.radiusSmall; color: Theme.dark ? Theme.surfaceSunken : "white"; border.color: Theme.outline }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GButton {
                objectName: "deleteTask"
                visible: dialog.taskId >= 0
                variant: "error"
                text: qsTr("Delete")
                onClicked: { dialog.close(); dialog.deleteRequested(dialog.taskId, name.text) }
            }
            Item { Layout.fillWidth: true }
            GButton { text: qsTr("Cancel"); variant: "outline"; onClicked: dialog.close() }
            GButton {
                objectName: "submitTask"
                variant: "primary"
                text: dialog.taskId >= 0 ? qsTr("Save") : qsTr("Add")
                onClicked: dialog.submit()
            }
        }
    }
}
