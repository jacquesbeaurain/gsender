import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Stats: Maintenance (features/Stats/Maintenance): the tasks - searchable,
// the urgent and due first - each with its time, reset (the check) and edit
// (the pen); Add New Task, Reset All; the upcoming ones beside.
Item {
    id: page
    objectName: "statsMaintenance"

    property StatsModel model
    property string query: ""
    signal resetRequested(int id, string name)
    signal resetAllRequested()
    signal editRequested(int id)

    readonly property var shown: {
        const needle = query.trim().toLowerCase()
        return needle ? model.tasks.filter(t => t.search.includes(needle)) : model.tasks
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.bottomMargin: 72
        spacing: 24
        StatCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            CardHeader { Layout.fillWidth: true; title: qsTr("Maintenance") }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    objectName: "taskSearch"
                    Layout.fillWidth: true
                    implicitHeight: 40
                    placeholderText: qsTr("Search Tasks...")
                    color: Theme.contentPrimary
                    onTextChanged: page.query = text
                }
                GButton { objectName: "addTask"; text: qsTr("Add New Task"); fontSize: Theme.fontSm; onClicked: page.editRequested(-1) }
                GButton { objectName: "resetAllTasks"; text: qsTr("Reset All"); fontSize: Theme.fontSm; onClicked: page.resetAllRequested() }
            }
            ListView {
                objectName: "maintenanceTasks"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: page.shown
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    implicitHeight: Math.max(64, taskRow.implicitHeight + 16)
                    color: "transparent"
                    border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                    radius: 4
                    RowLayout {
                        id: taskRow
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 12
                        // determineTime(): the hours until due, "Due", or urgent.
                        ColumnLayout {
                            Layout.preferredWidth: 80
                            spacing: 0
                            Icon {
                                visible: modelData.state === "urgent"
                                Layout.alignment: Qt.AlignHCenter
                                name: "IoIosWarning"; color: Theme.red[500]; width: 20; height: 20
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: modelData.state === "hours" ? qsTr("%1 Hrs").arg(modelData.hours)
                                    : modelData.state === "due" ? qsTr("Due") : qsTr("Urgent!")
                                font.bold: modelData.state !== "hours"
                                color: modelData.state === "hours" ? Theme.green[500]
                                     : modelData.state === "due" ? "#E15C00" : Theme.red[500]
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: modelData.name; font.bold: true; font.pixelSize: Theme.fontBase; color: Theme.contentPrimary }
                            Label {
                                visible: modelData.description !== ""
                                text: modelData.description
                                wrapMode: Text.Wrap
                                color: Theme.contentSecondary
                                font.pixelSize: Theme.fontSm
                                Layout.fillWidth: true
                            }
                        }
                        GButton {
                            objectName: "resetTask_" + modelData.id
                            variant: "ghost"
                            iconName: "LuCircleCheck"
                            iconColor: Theme.green[500]
                            onClicked: page.resetRequested(modelData.id, modelData.name)
                        }
                        GButton {
                            objectName: "editTask_" + modelData.id
                            variant: "ghost"
                            iconName: "LuPen"
                            iconSize: 18
                            onClicked: page.editRequested(modelData.id)
                        }
                    }
                }
            }
        }
        StatCard {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            CardHeader { Layout.fillWidth: true; title: qsTr("Upcoming Maintenance") }
            Repeater {
                model: page.model.upcomingMore
                MaintenanceReminder { required property var modelData; task: modelData; Layout.fillWidth: true }
            }
        }
    }
}
