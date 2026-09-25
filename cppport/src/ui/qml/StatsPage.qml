import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import GSender

// Stats (features/Stats): Overview, Jobs, Maintenance, Alarms and About,
// chosen from the menu floating at the bottom (StatMenu).
Item {
    id: stats
    objectName: "statsPage"

    property StatsModel model: StatsModel {}
    property string current: "overview"
    readonly property var pages: ["overview", "jobs", "maintenance", "alarms", "about"]

    function showPage(name) {
        if (name === "configuration")
            Backend.openPage("config")
        else if (pages.includes(name))
            current = name
    }

    Rectangle { anchors.fill: parent; color: Theme.dark ? Theme.surfaceBase : Theme.gray[50] }

    StackLayout {
        anchors.fill: parent
        currentIndex: stats.pages.indexOf(stats.current)
        StatsOverview {
            model: stats.model
            onShowPage: (name) => stats.showPage(name)
            onDownloadDiagnostics: diagnostics.open()
        }
        StatsJobs {
            model: stats.model
            onClearRequested: confirm.ask(qsTr("Delete Job History"), qsTr("Are you sure you want to delete all job history?"),
                                          () => stats.model.clearJobHistory())
        }
        StatsMaintenance {
            model: stats.model
            onResetRequested: (id, name) => confirm.ask(qsTr("Reset Maintenance Timer"),
                qsTr("Are you sure you want to reset the maintenance timer for %1? Only do this if you have just performed this maintenance task.").arg(name),
                () => stats.model.resetTask(id))
            onResetAllRequested: confirm.ask(qsTr("Reset All Tasks"), qsTr("Are you sure you want to reset the times for every Maintenance Task?"),
                                             () => stats.model.resetAllTasks())
            onEditRequested: (id) => taskDialog.openFor(id)
        }
        StatsAlarms {
            model: stats.model
            onClearRequested: confirm.ask(qsTr("Delete History"), qsTr("Are you sure you want to delete all alarm/error history?"),
                                          () => stats.model.clearAlarms())
            onDownloadDiagnostics: diagnostics.open()
        }
        StatsAbout { model: stats.model }
    }

    // StatMenu.
    Rectangle {
        objectName: "statMenu"
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        anchors.horizontalCenter: parent.horizontalCenter
        width: menuRow.implicitWidth + 8
        height: menuRow.implicitHeight + 8
        radius: Theme.radiusSmall
        color: Theme.dark ? Theme.surfaceElevated : "white"
        border.color: Theme.dark ? Theme.outline : Theme.gray[200]
        Row {
            id: menuRow
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: [
                    { key: "overview", label: qsTr("Overview") },
                    { key: "jobs", label: qsTr("Jobs") },
                    { key: "maintenance", label: qsTr("Maintenance") },
                    { key: "alarms", label: qsTr("Alarms") },
                    { key: "about", label: qsTr("About") }
                ]
                Rectangle {
                    required property var modelData
                    readonly property bool active: stats.current === modelData.key
                    objectName: "statMenu_" + modelData.key
                    width: menuLabel.implicitWidth + 24
                    height: Theme.touchTarget
                    radius: 8
                    color: active ? Qt.rgba(0x3F / 255, 0x85 / 255, 0xC7 / 255, 0.3) : "transparent"
                    Label {
                        id: menuLabel
                        anchors.centerIn: parent
                        text: modelData.label
                        font.pixelSize: Theme.fontSm
                        font.weight: Font.Medium
                        color: parent.active ? Theme.blue[500] : (Theme.dark ? Theme.contentPrimary : Theme.gray[600])
                    }
                    TapHandler { onTapped: stats.current = modelData.key }
                }
            }
        }
    }

    ConfirmDialog {
        id: confirm
        objectName: "statsConfirm"
        property var action: null
        function ask(title, message, then) {
            confirm.title = title
            confirm.message = message
            action = then
            open()
        }
        actionText: qsTr("Yes")
        onAccepted: if (action) action()
    }
    MaintenanceTaskDialog {
        id: taskDialog
        model: stats.model
        onDeleteRequested: (id, name) => confirm.ask(qsTr("Delete Task"), qsTr("Are you sure you want to delete this task?") + "\n\n" + name,
                                                     () => stats.model.deleteTask(id))
    }
    FileDialog {
        id: diagnostics
        title: qsTr("Download Diagnostic File")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("ZIP archives (*.zip)")]
        currentFile: "file://" + stats.model.diagnosticsName()
        onAccepted: {
            const error = stats.model.writeDiagnostics(selectedFile)
            if (error)
                Backend.notify(qsTr("Failed to generate diagnostic file") + ": " + error, "error")
            else
                Backend.notify(qsTr("Diagnostic file downloaded successfully!"), "success")
        }
    }
}
