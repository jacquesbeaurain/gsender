import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import GSender

// The machine state (MachineStatus): a trapezoid hanging from the top bar
// (clip-path 0 0, 100% 0, 85% 100%, 15% 100%) in the state's colour, the
// state's name in light 30px type, "Alarm (n)" with the alarm's code and
// its "?". Beside it Machine Information (MachineInfo) and the lock
// (SmallUnlockButton); under it, in an alarm, the button that unlocks - or
// runs homing (UnlockButton).
Item {
    id: pill
    objectName: "statusPill"

    property StatusModel model: StatusModel {}
    readonly property string text: Backend.stateText + (Backend.alarmCode ? " (" + Backend.alarmCode + ")" : "")

    implicitWidth: 288
    implicitHeight: 60

    function act(needsChoice) {
        if (needsChoice)
            homingFailure.open()
    }

    Shape {
        anchors.fill: parent
        ShapePath {
            strokeWidth: -1
            fillColor: Theme.stateColor(Backend.activeState)
            startX: 0; startY: 0
            PathLine { x: pill.width; y: 0 }
            PathLine { x: pill.width * 0.85; y: pill.height }
            PathLine { x: pill.width * 0.15; y: pill.height }
            PathLine { x: 0; y: 0 }
        }
    }
    Label {
        id: statusText
        objectName: "statusText"
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -2
        anchors.horizontalCenterOffset: pill.model.alarm ? -20 : 0
        text: pill.text
        color: "white"
        font.pixelSize: Theme.font3xl
        font.weight: Font.Light
    }
    // The alarm's explanation (AlarmDescriptionIcon).
    Rectangle {
        objectName: "alarmHelp"
        visible: pill.model.alarm
        anchors.left: statusText.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 32
        height: 32
        radius: 16
        color: "white"
        opacity: 0.9
        Label { anchors.centerIn: parent; text: "?"; font.pixelSize: 20; color: Theme.gray[600] }
        TapHandler { onTapped: pill.model.showAlarmHelp() }
    }

    // Machine Information, left of the status.
    Rectangle {
        objectName: "machineInfoButton"
        anchors.right: parent.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -6
        width: Theme.touchTarget
        height: Theme.touchTarget - 8
        radius: Theme.radiusSmall
        color: "transparent"
        Icon { anchors.centerIn: parent; name: "MdInfoOutline"; color: Theme.contentMuted; width: 26; height: 26 }
        TapHandler { onTapped: machineInfo.opened ? machineInfo.close() : machineInfo.open() }
        MachineInfoPopup {
            id: machineInfo
            model: pill.model
            x: -width / 2
            y: parent.height + 16
        }
    }
    // The lock, right of the status.
    Rectangle {
        objectName: "lockButton"
        anchors.left: parent.right
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -6
        width: Theme.touchTarget
        height: Theme.touchTarget - 8
        color: "transparent"
        enabled: pill.model.connected
        Icon {
            anchors.centerIn: parent
            name: pill.model.lockActive ? "FaUnlock" : "FaLock"
            color: pill.model.lockActive ? Theme.yellow600 : Theme.gray[400]
            width: 26
            height: 26
        }
        TapHandler { onTapped: pill.act(pill.model.clickLock()) }
    }

    // Under the status in an alarm (UnlockButton).
    AbstractButton {
        id: unlock
        objectName: "unlockButton"
        visible: pill.model.alarm
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.bottom
        anchors.topMargin: 16
        implicitWidth: unlockRow.implicitWidth + 32
        implicitHeight: Theme.touchTarget + 4
        background: Rectangle {
            radius: height / 2
            color: unlock.pressed ? Theme.red[700] : Theme.red[500]
            border.color: Theme.red[800]
        }
        contentItem: Item {
            RowLayout {
                id: unlockRow
                anchors.centerIn: parent
                spacing: 8
                Icon {
                    name: pill.model.alarmButtonHomes ? "FaHome" : "FaUnlock"
                    color: "white"
                    width: 22
                    height: 22
                }
                Label {
                    text: pill.model.alarmButtonHomes ? qsTr("Click to Run Homing") : qsTr("Click to Unlock Machine")
                    color: "white"
                    font.bold: true
                }
            }
        }
        onClicked: pill.act(pill.model.clickAlarmButton())
    }

    // confirmUnlockAfterHomingFailure.
    Popup {
        id: homingFailure
        objectName: "homingFailure"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 24
        width: Math.min(520, parent ? parent.width - 32 : 520)
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
        background: Rectangle { radius: Theme.radius; color: Theme.dark ? Theme.surfaceElevated : "white" }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: qsTr("Homing Not Complete")
                font.pixelSize: Theme.fontLg
                font.bold: true
                color: Theme.contentPrimary
            }
            Label {
                text: pill.model.homingFailureText()
                wrapMode: Text.Wrap
                color: Theme.contentSecondary
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                GButton {
                    text: qsTr("Cancel")
                    variant: "outline"
                    onClicked: homingFailure.close()
                }
                GButton {
                    objectName: "unlockAnyway"
                    text: qsTr("Unlock Anyway")
                    variant: "error"
                    onClicked: { homingFailure.close(); pill.model.resolveHomingFailure("unlock") }
                }
                GButton {
                    objectName: "rehome"
                    text: qsTr("Rehome")
                    variant: "primary"
                    onClicked: { homingFailure.close(); pill.model.resolveHomingFailure("rehome") }
                }
            }
        }
    }
}
