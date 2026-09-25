import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The bell (NotificationsArea): the unread errors' count; its list has
// tabs - All, Errors, Info, Success - the newest first with how long ago,
// and Clear all. Opening or closing it reads everything.
Item {
    id: bell
    objectName: "notificationBell"

    implicitWidth: Theme.touchTarget
    implicitHeight: Theme.touchTarget

    Icon {
        anchors.centerIn: parent
        name: "LuBell"
        color: Theme.contentMuted
        width: 26
        height: 26
    }
    Rectangle {
        objectName: "unreadErrors"
        visible: Backend.unreadErrors > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 2
        width: Math.max(18, badge.implicitWidth + 8)
        height: 18
        radius: 9
        color: Theme.red[500]
        Label { id: badge; anchors.centerIn: parent; text: Backend.unreadErrors; color: "white"; font.pixelSize: 11 }
    }
    TapHandler { onTapped: panel.opened ? panel.close() : panel.open() }
    Connections {
        target: Backend
        function onShortcutTriggered(id) {
            if (id === "DISPLAY_NOTIFICATIONS")
                panel.opened ? panel.close() : panel.open()
        }
    }

    Popup {
        id: panel
        objectName: "notificationPanel"
        property string tab: "all"
        x: bell.width - width
        y: bell.height + 8
        width: 380
        height: 460
        padding: 12
        onOpened: Backend.readAllNotifications()
        onClosed: Backend.readAllNotifications()
        background: Rectangle {
            radius: Theme.radius
            color: Theme.dark ? Theme.surfaceElevated : "white"
            border.color: Theme.outline
        }
        contentItem: ColumnLayout {
            spacing: 8
            RowLayout {
                Label { text: qsTr("Notifications"); font.bold: true; color: Theme.contentPrimary; Layout.fillWidth: true }
                GButton {
                    objectName: "clearNotifications"
                    variant: "ghost"
                    text: qsTr("Clear all")
                    onClicked: Backend.clearNotifications()
                }
            }
            RowLayout {
                spacing: 4
                Repeater {
                    model: [
                        { key: "all", label: qsTr("All") },
                        { key: "error", label: qsTr("Errors") },
                        { key: "info", label: qsTr("Info") },
                        { key: "success", label: qsTr("Success") }
                    ]
                    GButton {
                        required property var modelData
                        objectName: "notificationTab_" + modelData.key
                        text: modelData.label
                        variant: panel.tab === modelData.key ? "primary" : "ghost"
                        Layout.fillWidth: true
                        onClicked: panel.tab = modelData.key
                    }
                }
            }
            ListView {
                id: list
                objectName: "notificationList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: Backend.notifications.filter(n => panel.tab === "all" || n.type === panel.tab)
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    implicitHeight: column.implicitHeight + 12
                    radius: Theme.radiusSmall
                    color: Theme.dark ? Theme.surfaceRaised : Theme.gray[50]
                    border.color: Theme.outlineSubtle
                    ColumnLayout {
                        id: column
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        Label {
                            text: parent.parent.modelData.message
                            wrapMode: Text.Wrap
                            color: parent.parent.modelData.type === "error" ? Theme.red[500] : Theme.contentPrimary
                            Layout.fillWidth: true
                        }
                        Label {
                            text: parent.parent.modelData.ago
                            font.pixelSize: Theme.fontXs
                            color: Theme.contentMuted
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: qsTr("No notifications")
                    color: Theme.contentMuted
                }
            }
        }
    }
}
