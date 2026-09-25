import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The top bar's connection button (features/Connection): the connection's
// icon and state colour - with a green check once connected - and "Connect
// to CNC", or the port and firmware. A tap opens the choices.
Rectangle {
    id: button
    objectName: "connectionButton"

    readonly property string state: Backend.connected ? "connected" : Backend.connecting ? "connecting" : "disconnected"

    implicitWidth: Math.max(250, row.implicitWidth + 32)
    implicitHeight: 48
    radius: Theme.radius
    color: Theme.dark ? Theme.surfaceRaised : Theme.gray[100]
    border.color: Theme.dark ? Theme.outline : Theme.gray[400]

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 16

        Item {
            implicitWidth: 32
            implicitHeight: 32
            Icon {
                anchors.fill: parent
                name: !Backend.connected ? "PiPlugLight"
                    : Backend.connectionKind === "ethernet" ? "BsEthernet" : "BsUsbPlug"
                // ConnectionStateIndicator's colours.
                color: button.state === "connected" ? Theme.green[700]
                     : button.state === "connecting" ? Theme.yellow600 : Theme.blue[700]
            }
            Icon {
                visible: Backend.connected
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: -2
                width: 16
                height: 16
                name: "BsCheckCircleFill"
                color: "#22c55e"   // green-500 (Tailwind's: the check is not overridden)
            }
        }
        Label {
            visible: !Backend.connected
            text: Backend.connecting ? qsTr("Connecting...") : qsTr("Connect to CNC")
            font.bold: true
            font.pixelSize: Theme.fontBase
            color: Theme.contentPrimary
            Layout.fillWidth: true
        }
        ColumnLayout {
            visible: Backend.connected
            spacing: 4
            Layout.fillWidth: true
            Label {
                objectName: "connectionPort"
                text: Backend.portLabel
                font.bold: true
                font.pixelSize: Theme.fontBase
                color: Theme.contentPrimary
                Layout.alignment: Qt.AlignRight
            }
            Label {
                objectName: "connectionFirmware"
                text: Backend.firmwareLabel
                font.pixelSize: Theme.fontSm
                color: Theme.dark ? Theme.contentMuted : Theme.gray[600]
                Layout.alignment: Qt.AlignRight
            }
        }
    }

    TapHandler {
        onTapped: menu.open()
    }

    // Phase 0: the simulated boards. The port list comes with Phase 1.
    Menu {
        id: menu
        objectName: "connectionMenu"
        y: button.height + 4
        MenuItem {
            text: qsTr("Simulator (Grbl)")
            enabled: !Backend.connected
            onTriggered: Backend.connectSimulator(false)
        }
        MenuItem {
            text: qsTr("Simulator (grblHAL)")
            enabled: !Backend.connected
            onTriggered: Backend.connectSimulator(true)
        }
        MenuItem {
            text: qsTr("Disconnect")
            enabled: Backend.connected
            onTriggered: Backend.disconnectMachine()
        }
    }
}
