import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Stats: the overview (features/Stats/index): Your Machine - the connected
// port's results and times, the recent jobs, upcoming maintenance, the
// configuration - and Get Help: the diagnostic file, the resources,
// community and GitHub links, the latest alarms and errors.
Flickable {
    id: page
    objectName: "statsOverview"

    property StatsModel model
    signal showPage(string name)
    signal downloadDiagnostics()

    contentHeight: columns.implicitHeight + 96
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    component Heading: Label {
        font.pixelSize: 30
        font.bold: true
        color: Theme.contentPrimary
    }
    // ConfigRow: the label, a dotted leader and the value.
    component LeaderRow: RowLayout {
        property string label
        property string value
        Layout.fillWidth: true
        spacing: 6
        Label { text: parent.label; color: Theme.contentPrimary; font.pixelSize: Theme.fontSm }
        Item {
            Layout.fillWidth: true
            height: 2
            Row {
                anchors.fill: parent
                spacing: 4
                clip: true
                Repeater {
                    model: Math.ceil(parent.width / 6)
                    Rectangle { width: 2; height: 2; radius: 1; color: Theme.outline }
                }
            }
        }
        Label { text: parent.value; font.bold: true; font.pixelSize: Theme.fontSm; color: Theme.contentPrimary }
    }

    GridLayout {
        id: columns
        x: 16
        y: 16
        width: page.width - 32
        columns: page.width > 1100 ? 2 : 1
        columnSpacing: 32
        rowSpacing: 24

        // Your Machine.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 2
            Layout.alignment: Qt.AlignTop
            spacing: 16
            Heading { text: qsTr("Your Machine") }
            StatCard {
                Layout.fillWidth: true
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        CardHeader { Layout.fillWidth: true; title: qsTr("Stats") }
                        PieChart {
                            objectName: "jobResultsChart"
                            visible: page.model.connected && page.model.completeJobs + page.model.incompleteJobs > 0
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 240
                            Layout.preferredHeight: 250
                            labels: [qsTr("Complete"), qsTr("Incomplete")]
                            values: [page.model.completeJobs, page.model.incompleteJobs]
                            colors: ["#659dd2", "#C7813F"]
                            seriesLabel: qsTr("Jobs")
                        }
                        Label {
                            visible: !(page.model.connected && page.model.completeJobs + page.model.incompleteJobs > 0)
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredHeight: 210
                            verticalAlignment: Text.AlignVCenter
                            text: qsTr("No data to display")
                            color: Theme.contentMuted
                        }
                        Repeater {
                            model: page.model.statRows
                            LeaderRow {
                                required property var modelData
                                label: modelData.label
                                value: modelData.value
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: 10
                        CardHeader {
                            Layout.fillWidth: true
                            title: qsTr("Recent Jobs")
                            linkLabel: qsTr("More")
                            onLinkActivated: page.showPage("jobs")
                        }
                        Label {
                            visible: page.model.recentJobs.length === 0
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredHeight: 120
                            verticalAlignment: Text.AlignVCenter
                            text: qsTr("No Jobs recorded. Get carving!")
                            color: Theme.contentMuted
                        }
                        Repeater {
                            model: page.model.recentJobs
                            RowLayout {
                                required property var modelData
                                objectName: "recentJob"
                                Layout.fillWidth: true
                                spacing: 8
                                Icon {
                                    name: modelData.complete ? "LuCircleCheck" : "LuCircleX"
                                    color: modelData.complete ? Theme.green[500] : Theme.red[500]
                                    width: 18; height: 18
                                }
                                Label {
                                    text: modelData.file
                                    font.bold: true
                                    elide: Text.ElideRight
                                    color: Theme.contentPrimary
                                    Layout.fillWidth: true
                                }
                                Label { text: modelData.duration; color: Theme.contentSecondary; font.pixelSize: Theme.fontSm }
                                Rectangle {
                                    readonly property color tone: modelData.complete ? Theme.green[500] : Theme.red[500]
                                    implicitWidth: Math.max(76, status.implicitWidth + 12)
                                    implicitHeight: 22
                                    radius: 11
                                    color: Qt.rgba(tone.r, tone.g, tone.b, 0.2)
                                    border.color: tone
                                    Label {
                                        id: status
                                        anchors.centerIn: parent
                                        text: modelData.complete ? qsTr("Finished") : qsTr("Stopped")
                                        font.pixelSize: Theme.fontXs
                                        color: parent.tone
                                    }
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                StatCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    CardHeader {
                        Layout.fillWidth: true
                        title: qsTr("Upcoming Maintenance")
                        linkLabel: qsTr("Manage")
                        onLinkActivated: page.showPage("maintenance")
                    }
                    Repeater {
                        model: page.model.upcoming
                        MaintenanceReminder { required property var modelData; task: modelData; Layout.fillWidth: true }
                    }
                }
                StatCard {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    spacing: 2
                    CardHeader {
                        Layout.fillWidth: true
                        title: qsTr("Configuration")
                        linkLabel: qsTr("Change")
                        onLinkActivated: page.showPage("configuration")
                    }
                    Label {
                        objectName: "machineProfileName"
                        text: page.model.profile
                        font.bold: true
                        color: Theme.contentPrimary
                    }
                    Repeater {
                        model: page.model.configuration
                        LeaderRow {
                            required property var modelData
                            label: modelData.label
                            value: modelData.value
                        }
                    }
                }
            }
        }

        // Get Help.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: 16
            Heading { text: qsTr("Get Help") }
            DiagnosticCard {
                Layout.fillWidth: true
                onDownload: page.downloadDiagnostics()
            }
            Repeater {
                model: [
                    { icon: "FaBookBookmark", title: qsTr("Resources"), link: "https://resources.sienci.com/view/gs-using-gsender/",
                      text: qsTr("Learn about starting with gSender and how to use specific features") },
                    { icon: "ImBubbles4", title: qsTr("Community"), link: "https://forum.sienci.com/c/gsender/14",
                      text: qsTr("Have conversations with our friendly and helpful community") },
                    { icon: "FaGithub", title: qsTr("Github"), link: "https://github.com/Sienci-Labs/gsender",
                      text: qsTr("Submit issues or grab the latest version of gSender") }
                ]
                Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: linkRow.implicitHeight + 24
                    radius: 4
                    color: Theme.dark ? Theme.surfaceRaised : "white"
                    border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                    border.width: 2
                    Rectangle { width: parent.width; height: 2; color: Theme.blue[500] }
                    RowLayout {
                        id: linkRow
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 12
                        Rectangle {
                            width: 44; height: 44; radius: 4
                            gradient: Gradient {
                                GradientStop { position: 0; color: Theme.blue[500] }
                                GradientStop { position: 1; color: Theme.robin[300] }
                            }
                            Icon { anchors.centerIn: parent; name: modelData.icon; color: "white"; width: 22; height: 22 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: modelData.title; font.bold: true; color: Theme.contentPrimary }
                            Label {
                                text: modelData.text
                                wrapMode: Text.Wrap
                                font.pixelSize: Theme.fontSm
                                color: Theme.contentMuted
                                Layout.fillWidth: true
                            }
                        }
                        Icon { name: "GoArrowUpRight"; color: Theme.blue[500]; width: 20; height: 20 }
                    }
                    TapHandler { onTapped: Qt.openUrlExternally(modelData.link) }
                }
            }
            StatCard {
                Layout.fillWidth: true
                CardHeader {
                    Layout.fillWidth: true
                    title: qsTr("Alarms & Errors")
                    linkLabel: qsTr("View all")
                    onLinkActivated: page.showPage("alarms")
                }
                Label {
                    visible: page.model.alarmPreview.length === 0
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredHeight: 160
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("No Alarms or Errors recorded. Hooray!")
                    color: Theme.contentMuted
                }
                Repeater {
                    model: page.model.alarmPreview
                    Rectangle {
                        required property var modelData
                        readonly property color tone: modelData.alarm ? Theme.red[500] : "#eab308"
                        Layout.fillWidth: true
                        implicitHeight: 36
                        radius: 3
                        color: Qt.rgba(tone.r, tone.g, tone.b, 0.1)
                        Rectangle { width: 4; height: parent.height; color: parent.tone }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 8
                            Label { text: modelData.what; color: parent.parent.tone; Layout.fillWidth: true }
                            Label { text: modelData.when; color: parent.parent.tone; font.pixelSize: Theme.fontXs }
                        }
                    }
                }
            }
        }
    }
}
