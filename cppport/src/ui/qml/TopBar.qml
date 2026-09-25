import QtQuick
import QtQuick.Layouts
import GSender

// The top bar (workspace/TopBar): the connection at the left, the machine
// state hanging from the middle, the status icons at the right.
Rectangle {
    id: bar
    objectName: "topBar"

    implicitHeight: 56
    color: Theme.topBar
    border.color: Theme.dark ? Theme.outline : Theme.gray[200]

    ConnectionButton {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        height: 44
    }

    StatusPill {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
    }

    RowLayout {
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 16
        // StatusIcons and the notifications' bell; their panels come later.
        NotificationBell {}
        Icon { name: "FaRegKeyboard"; color: Theme.green[500]; width: 28; height: 28 }
        Icon { name: "LuGamepad2"; color: Theme.contentMuted; width: 28; height: 28 }
    }
}
