import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

Item {
    id: root
    implicitWidth: 600
    implicitHeight: 400
    property var gsender: null

    property string testLine: "[PRB:12.345,45.678,-2.000:1]"
    property string parsedResult: "Matches: X=12.345, Y=45.678, Z=-2.000 (Success=1)"

    Card {
        anchors.fill: parent
        anchors.margins: 16

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            Label {
                text: qsTr("Parser Demo & Response Regex Interceptor")
                font.pixelSize: Theme.font2xl
                font.bold: true
                color: Theme.contentPrimary
            }

            Label {
                text: qsTr("Declared line parsers in manifest intercept raw GRBL responses before standard handling.")
                color: Theme.contentSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Label {
                text: qsTr("Active Patterns:")
                font.bold: true
                color: Theme.contentPrimary
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 60
                radius: Theme.radius
                color: Theme.dark ? Theme.surfaceRaised : Theme.gray[100]
                border.color: Theme.dark ? Theme.outline : Theme.gray[300]
                ColumnLayout {
                    anchors.centerIn: parent
                    Label { text: "1. ^\[PRB:([-\d.]+),([-\d.]+),([-\d.]+):([01])\]"; font.family: Theme.monoFont; color: Theme.contentPrimary }
                    Label { text: "2. ^;TOOL:(\d+)"; font.family: Theme.monoFont; color: Theme.contentPrimary }
                }
            }

            Label {
                text: qsTr("Test Line:")
                font.bold: true
                color: Theme.contentPrimary
            }

            TextField {
                Layout.fillWidth: true
                text: root.testLine
                onTextChanged: root.testLine = text
            }

            GButton {
                text: qsTr("Send Probe Test Command")
                onClicked: {
                    if (root.gsender) {
                        root.gsender.send("machine:command", { command: "$G" })
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
