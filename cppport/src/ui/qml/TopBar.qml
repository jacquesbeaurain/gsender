import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The top bar (workspace/TopBar): the connection at the left, the machine
// state hanging from the middle, the status icons at the right.
Rectangle {
    id: bar
    objectName: "topBar"

    // Upstream's h-14, and h-12 below the xl breakpoint.
    implicitHeight: Window.window && Window.window.width <= 1280 ? 48 : 56
    color: Theme.topBar
    border.color: Theme.dark ? Theme.outline : Theme.gray[200]
    border.width: Theme.hairline

    // The gSender logo (40 px) and the connection button after it.
    Image {
        objectName: "topBarLogo"
        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        width: 40
        height: 40
        source: "qrc:/about/icon-round.png"
        sourceSize.width: 80
        sourceSize.height: 80
        smooth: true
    }
    ConnectionButton {
        anchors.left: parent.left
        anchors.leftMargin: 65
        anchors.verticalCenter: parent.verticalCenter
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
        // StatusIcons - Wireless Control (green while the pendant is served),
        // Keyboard Shortcuts (green while shortcuts are on) and Gamepad
        // Shortcuts, each opening its tool - then the notifications' bell.
        Item {
            objectName: "statusRemote"
            implicitWidth: 24
            implicitHeight: 28
            RemoteModel { id: remoteState }
            Icon {
                anchors.centerIn: parent
                name: "RemoteIndicator"
                color: remoteState.running ? Theme.green[500] : Theme.contentDisabled
                width: 24; height: 28
            }
            HoverHandler { id: remoteHover }
            ToolTip.visible: remoteHover.hovered
            ToolTip.text: qsTr("Wireless Control")
            TapHandler { onTapped: remoteDialog.open() }
            RemoteDialog { id: remoteDialog; model: remoteState }
        }
        Repeater {
            model: [
                { name: "statusKeyboard", icon: "FaRegKeyboard", tool: "shortcuts", tip: qsTr("Keyboard Shortcuts") },
                { name: "statusGamepad", icon: "LuGamepad2", tool: "gamepad", tip: qsTr("Gamepad Shortcuts") }
            ]
            Item {
                required property var modelData
                objectName: modelData.name
                implicitWidth: 28
                implicitHeight: 28
                Icon {
                    anchors.centerIn: parent
                    name: parent.modelData.icon
                    color: (parent.modelData.tool === "shortcuts" ? Backend.shortcutsEnabled : Backend.gamepadConnected) ? Theme.green[500] : Theme.contentMuted
                    width: 28; height: 28
                }
                HoverHandler { id: hover }
                ToolTip.visible: hover.hovered
                ToolTip.text: modelData.tip
                TapHandler { onTapped: Backend.openTool(parent.modelData.tool) }
            }
        }
        NotificationBell {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 28
        }
    }
}
