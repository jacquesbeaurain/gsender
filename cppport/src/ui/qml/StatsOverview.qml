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

    // max-xl: the Your Machine column spans the page and Get Help goes below,
    // in two columns.
    readonly property bool compact: Window.window && Window.window.width <= 1280

    contentHeight: columns.implicitHeight + 112
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: GScrollBar { inset: 16 }

    component Heading: Label {
        font.pixelSize: 30
        font.bold: true
        color: Theme.contentPrimary
    }
    // ConfigRow: the label, a dotted leader and the value (leading-7, my-3).
    component LeaderRow: RowLayout {
        property string label
        property string value
        Layout.fillWidth: true
        Layout.preferredHeight: 27
        spacing: 6
        Label {
            text: parent.label
            color: Theme.contentBody
            font.pixelSize: Theme.fontBase
        }
        Item {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            height: 2
            Row {
                anchors.fill: parent
                spacing: 2
                clip: true
                Repeater {
                    model: Math.ceil(parent.width / 4)
                    Rectangle { width: 2; height: 2; color: Theme.outline }
                }
            }
        }
        Label { text: parent.value; font.bold: true; font.pixelSize: Theme.fontBase; color: Theme.contentPrimary }
    }

    GridLayout {
        id: columns
        x: 32
        y: 14
        width: page.width - 68   // upstream's fixed-content-area and mr-5
        columns: page.compact ? 1 : 2
        columnSpacing: 64
        rowSpacing: 16

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
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: 8
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
                        Item {   // h-52, the placeholder centred
                            visible: !(page.model.connected && page.model.completeJobs + page.model.incompleteJobs > 0)
                            Layout.fillWidth: true
                            Layout.preferredHeight: 208
                            StatEmpty {
                                anchors.centerIn: parent
                                icon: "FaChartPie"
                                text: qsTr("No data to display")
                            }
                        }
                        ColumnLayout {   // px-10
                            Layout.fillWidth: true
                            Layout.leftMargin: 40
                            Layout.rightMargin: 40
                            spacing: 0
                            Repeater {
                                model: page.model.statRows
                                LeaderRow {
                                    required property var modelData
                                    label: modelData.label
                                    value: modelData.value
                                }
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
                        Item {
                            visible: page.model.recentJobs.length === 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: 240
                            StatEmpty {
                                anchors.centerIn: parent
                                icon: "FaRegListAlt"
                                text: qsTr("No Jobs recorded. Get carving!")
                            }
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
                                Panel {
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
                        Layout.bottomMargin: 8
                    }
                    ColumnLayout {   // gap-1
                        Layout.fillWidth: true
                        spacing: 4
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
        }

        // Get Help: below the machine at max-xl, the help and the alarms
        // side by side.
        GridLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            columns: page.compact ? 2 : 1
            columnSpacing: 16
            rowSpacing: 16
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
                    color: Theme.surfaceRaised
                    border.color: Theme.border
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
                        Icon { name: "GoArrowUpRight"; color: Theme.primaryText; width: 20; height: 20 }
                    }
                    TapHandler { onTapped: Qt.openUrlExternally(modelData.link) }
                }
            }
        }
            StatCard {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.fillHeight: page.compact
                Layout.alignment: Qt.AlignTop
                CardHeader {
                    Layout.fillWidth: true
                    title: qsTr("Alarms & Errors")
                    linkLabel: qsTr("View all")
                    onLinkActivated: page.showPage("alarms")
                }
                Label {
                    visible: page.model.alarmPreview.length === 0
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillHeight: true
                    Layout.preferredHeight: 160
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("No Alarms or Errors recorded. Hooray!")
                    color: Theme.contentBody
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
