import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The Helper's info panel (features/Helper): a card over the top left - a
// third of the width, its bottom a third of the way down, orange-bordered -
// that blocks nothing and stays until closed or replaced.
Rectangle {
    id: panel
    objectName: "helperPanel"

    visible: Backend.helperVisible
    radius: Theme.radius
    color: Theme.dark ? Theme.surfaceElevated : "white"
    border.color: "#c27924"
    border.width: 2

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Label {
                objectName: "helperTitle"
                text: Backend.helperTitle
                font.pixelSize: Theme.fontLg
                font.bold: true
                color: Theme.contentPrimary
                Layout.fillWidth: true
            }
            GButton {
                objectName: "helperClose"
                variant: "ghost"
                iconName: "MdClose"
                onClicked: Backend.closeHelper()
            }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            Label {
                objectName: "helperText"
                width: panel.width - 24
                text: Backend.helperText
                textFormat: Text.RichText
                wrapMode: Text.Wrap
                color: Theme.contentSecondary
            }
        }
        Label {
            visible: Backend.helperLink !== ""
            textFormat: Text.StyledText
            text: qsTr("More in the <a href=\"%1\">resources</a>.").arg(Backend.helperLink)
            color: Theme.contentMuted
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
    }
}
