import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Stats: Alarms (features/Stats/Alarms): the alarms and errors, newest
// first; the diagnostic file; clearing them.
Item {
    id: page
    objectName: "statsAlarms"

    property StatsModel model
    signal clearRequested()
    signal downloadDiagnostics()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.bottomMargin: 72
        spacing: 24
        StatCard {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            CardHeader { Layout.fillWidth: true; title: qsTr("Alarms & Errors") }
            ColumnLayout {
                visible: page.model.alarms.length === 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Item { Layout.fillHeight: true }
                Icon { Layout.alignment: Qt.AlignHCenter; name: "PiMaskHappyBold"; color: Theme.contentMuted; width: 64; height: 64 }
                Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("No Alarms or Errors recorded. Hooray!"); color: Theme.contentMuted; font.pixelSize: Theme.fontLg }
                Item { Layout.fillHeight: true }
            }
            ListView {
                objectName: "alarmList"
                visible: page.model.alarms.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: page.model.alarms
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    required property var modelData
                    readonly property color tone: modelData.alarm ? Theme.red[500] : "#f97316"
                    width: ListView.view.width
                    spacing: 12
                    Icon {
                        Layout.alignment: Qt.AlignTop
                        name: modelData.alarm ? "LuCircleX" : "IoIosWarning"
                        color: parent.tone
                        width: 32; height: 32
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: modelData.title; font.pixelSize: Theme.fontLg; font.weight: Font.DemiBold; color: parent.parent.tone }
                        Label { text: modelData.time; color: Theme.gray[500]; font.pixelSize: Theme.fontSm }
                        Label { text: modelData.message; wrapMode: Text.Wrap; color: Theme.contentPrimary; Layout.fillWidth: true }
                        Label {
                            textFormat: Text.StyledText
                            text: qsTr("Line:") + " <b>" + modelData.line + "</b>"
                            color: Theme.contentPrimary
                        }
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: 16
            DiagnosticCard {
                Layout.fillWidth: true
                titled: true
                onDownload: page.downloadDiagnostics()
            }
            StatCard {
                Layout.fillWidth: true
                CardHeader { Layout.fillWidth: true; title: qsTr("Clear Alarms & Errors") }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.contentMuted
                    text: qsTr("Clear all prior alarms and errors. This action cannot be undone.")
                }
                GButton {
                    objectName: "clearAlarms"
                    Layout.fillWidth: true
                    text: qsTr("Clear Alarms & Errors")
                    iconName: "FaTrash"
                    iconSize: 14
                    onClicked: page.clearRequested()
                }
            }
        }
    }
}
