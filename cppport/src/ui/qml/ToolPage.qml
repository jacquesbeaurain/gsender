import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A tool's page (components/Page): the title and description over a rule,
// Go Back to the Tools page, and the tool beneath.
Item {
    id: page

    property string title
    property string description
    default property alias content: area.data
    signal back()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.leftMargin: 32
        anchors.rightMargin: 32
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumHeight: 56
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { text: page.title; font.pixelSize: 30; font.bold: true; color: Theme.contentPrimary; Layout.fillWidth: true }
                Label { visible: page.description !== ""; text: page.description; color: Theme.gray[500] }
            }
            GButton {
                objectName: "toolGoBack"
                variant: "outline"
                iconName: "LuArrowLeft"
                iconSize: 22
                text: qsTr("Go Back")
                onClicked: page.back()
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.dark ? Theme.outline : Theme.gray[200] }
        Item {
            id: area
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
