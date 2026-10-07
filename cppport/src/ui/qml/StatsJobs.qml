import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Stats: Jobs (features/Stats/Jobs): the job history - searchable, newest
// first, sortable by its columns, as StatsModel lists it - and the jobs and
// run time per CNC.
Item {
    id: page
    objectName: "statsJobs"

    property StatsModel model
    signal clearRequested()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.bottomMargin: 72
        spacing: 24

        StatCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            RowLayout {
                Layout.fillWidth: true
                CardHeader { Layout.fillWidth: true; title: qsTr("Job History") }
                GButton {
                    objectName: "clearJobHistory"
                    text: qsTr("Clear")
                    iconName: "FaTrash"
                    iconSize: 14
                    fontSize: Theme.fontSm
                    onClicked: page.clearRequested()
                }
            }
            TextField {
                objectName: "jobSearch"
                Layout.fillWidth: true
                implicitHeight: 40
                placeholderText: qsTr("Search past jobs...")
                onTextChanged: page.model.jobSearch = text
            }
            // The header: tap to sort.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: [
                        { key: "file", label: qsTr("File Name"), width: 3 },
                        { key: "duration", label: qsTr("Duration"), width: 1 },
                        { key: "lines", label: qsTr("# Lines"), width: 1 },
                        { key: "start", label: qsTr("Start Time"), width: 2 },
                        { key: "complete", label: qsTr("Status"), width: 1 }
                    ]
                    Label {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: modelData.width
                        Layout.minimumHeight: 36
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.label + (page.model.jobSort === modelData.key ? (page.model.jobsAscending ? " ▲" : " ▼") : "")
                        font.bold: true
                        font.pixelSize: Theme.fontSm
                        TapHandler { onTapped: page.model.sortJobs(modelData.key) }
                    }
                }
            }
            ListView {
                objectName: "jobHistory"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: page.model.jobs
                ScrollBar.vertical: GScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: 40
                    color: index % 2 ? "transparent" : (Theme.dark ? Theme.surfaceElevated : Theme.gray[50])
                    RowLayout {
                        anchors.fill: parent
                        spacing: 8
                        Label { text: modelData.file; elide: Text.ElideRight; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                        Label { text: modelData.duration; Layout.fillWidth: true; Layout.preferredWidth: 1 }
                        Label { text: modelData.lines; Layout.fillWidth: true; Layout.preferredWidth: 1 }
                        Label { text: modelData.start; font.pixelSize: Theme.fontSm; Layout.fillWidth: true; Layout.preferredWidth: 2 }
                        Item {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.fillHeight: true
                            Icon {
                                anchors.centerIn: parent
                                name: modelData.complete ? "FaCheckCircle" : "FaCircleXmark"
                                color: modelData.complete ? Theme.green[500] : Theme.red[500]
                                width: 18; height: 18
                            }
                        }
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            spacing: 16
            StatCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                CardHeader { Layout.alignment: Qt.AlignHCenter; title: qsTr("Jobs per CNC") }
                PieChart {
                    objectName: "jobsPerCnc"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    labels: page.model.jobsPerCnc.labels || []
                    values: page.model.jobsPerCnc.values || []
                    colors: [Theme.robin[400], "#22415e", Theme.tw.red[600], Theme.orange[500], Theme.blue[500], Theme.tw.emerald[600]]
                    seriesLabel: qsTr("Jobs")
                }
            }
            StatCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                CardHeader { Layout.alignment: Qt.AlignHCenter; title: qsTr("Run Time per CNC") }
                PieChart {
                    objectName: "runTimePerCnc"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    doughnut: true
                    labels: page.model.runTimePerCnc.labels || []
                    values: page.model.runTimePerCnc.values || []
                    colors: [Theme.robin[400], Theme.tw.red[600], Theme.orange[500], Theme.blue[500], Theme.tw.emerald[600], "#22415e"]
                    // Upstream's tooltip says hours of what are milliseconds.
                    valueText: (ms) => qsTr("%1 hours").arg((ms / 3600000).toFixed(2))
                }
            }
        }
    }
}
