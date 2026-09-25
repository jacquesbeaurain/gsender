import QtQuick
import QtQuick.Controls.Basic
import GSender

// A page the touch UI does not have yet.
Item {
    id: placeholder
    property string title
    property string phase

    Column {
        anchors.centerIn: parent
        spacing: 8
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: placeholder.title
            font.pixelSize: Theme.font3xl
            font.weight: Font.Light
            color: Theme.contentPrimary
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Not ported to the touch UI yet (%1). The widget application has it.").arg(placeholder.phase)
            font.pixelSize: Theme.fontBase
            color: Theme.contentMuted
        }
    }
}
