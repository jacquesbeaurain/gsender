import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Stats: Jobs (features/Stats/Jobs): the job history - searchable, newest
// first, sortable by its columns - and the jobs and run time per CNC.
Item {
    id: page
    objectName: "statsJobs"

    property StatsModel model
    property string query: ""
    property string sortKey: "start"
    property bool ascending: false
    signal clearRequested()

    readonly property var shown: {
        const needle = query.trim().toLowerCase()
        let list = model.jobs.map((job, i) => Object.assign({ order: i }, job))
        if (needle)
            list = list.filter(job => job.search.includes(needle))
        const key = sortKey
        if (key !== "start" || ascending) {
            list.sort((a, b) => {
                const va = key === "start" ? -a.order : key === "duration" ? a.durationMs : a[key]
                const vb = key === "start" ? -b.order : key === "duration" ? b.durationMs : b[key]
                const order = va < vb ? -1 : va > vb ? 1 : 0
                return ascending ? order : -order
            })
        }
        return list
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
                color: Theme.contentPrimary
                onTextChanged: page.query = text
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
                        text: modelData.label + (page.sortKey === modelData.key ? (page.ascending ? " ▲" : " ▼") : "")
                        font.bold: true
                        font.pixelSize: Theme.fontSm
                        color: Theme.contentPrimary
                        TapHandler {
                            onTapped: {
                                if (page.sortKey === modelData.key)
                                    page.ascending = !page.ascending
                                else {
                                    page.sortKey = modelData.key
                                    page.ascending = true
                                }
                            }
                        }
                    }
                }
            }
            ListView {
                objectName: "jobHistory"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: page.shown
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: 40
                    color: index % 2 ? "transparent" : (Theme.dark ? Theme.surfaceElevated : Theme.gray[50])
                    RowLayout {
                        anchors.fill: parent
                        spacing: 8
                        Label { text: modelData.file; elide: Text.ElideRight; color: Theme.contentPrimary; Layout.fillWidth: true; Layout.preferredWidth: 3 }
                        Label { text: modelData.duration; color: Theme.contentPrimary; Layout.fillWidth: true; Layout.preferredWidth: 1 }
                        Label { text: modelData.lines; color: Theme.contentPrimary; Layout.fillWidth: true; Layout.preferredWidth: 1 }
                        Label { text: modelData.start; color: Theme.contentPrimary; font.pixelSize: Theme.fontSm; Layout.fillWidth: true; Layout.preferredWidth: 2 }
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
                    colors: ["#7ca7d0", "#22415e", "#dc2626", "#bb6a0c", "#3F85C7", "#059669"]
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
                    colors: ["#7ca7d0", "#dc2626", "#bb6a0c", "#3F85C7", "#059669", "#22415e"]
                    // Upstream's tooltip says hours of what are milliseconds.
                    valueText: (ms) => qsTr("%1 hours").arg((ms / 3600000).toFixed(2))
                }
            }
        }
    }
}
