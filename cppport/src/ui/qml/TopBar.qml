import QtQuick
import QtQuick.Controls.Basic
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
        // The notifications' bell and StatusIcons: Keyboard Shortcuts (green
        // while shortcuts are on) and Gamepad Shortcuts, each opening its tool.
        NotificationBell {}
        Repeater {
            model: [
                { name: "statusKeyboard", icon: "FaRegKeyboard", tool: "shortcuts", tip: qsTr("Keyboard Shortcuts") },
                { name: "statusGamepad", icon: "LuGamepad2", tool: "gamepad", tip: qsTr("Gamepad Shortcuts") }
            ]
            Item {
                required property var modelData
                objectName: modelData.name
                implicitWidth: 36
                implicitHeight: 36
                Icon {
                    anchors.centerIn: parent
                    name: parent.modelData.icon
                    color: parent.modelData.tool === "shortcuts" && Backend.shortcutsEnabled ? Theme.green[500] : Theme.contentMuted
                    width: 28; height: 28
                }
                HoverHandler { id: hover }
                ToolTip.visible: hover.hovered
                ToolTip.text: modelData.tip
                TapHandler { onTapped: Backend.openTool(parent.modelData.tool) }
            }
        }
    }
}
