import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The top bar's connection button (features/Connection): the connection's
// icon in its state's colour - with a green check once connected - and
// "Connect to CNC", "Connecting...", "Unable to connect.", or the port and
// firmware. Disconnected, a tap lists the ports (PortListings); connected,
// it offers Disconnect (upstream shows that overlay on hover, which a touch
// screen does not have).
Rectangle {
    id: button
    objectName: "connectionButton"

    property ConnectionModel model: ConnectionModel {}
    readonly property string state: model.state

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
            opacity: button.state === "disconnected" ? 0.75 + 0.25 * Math.sin(pulse.phase) : 1
            Icon {
                anchors.fill: parent
                name: button.state !== "connected" && button.state !== "connecting" ? "PiPlugLight"
                    : Backend.connectionKind === "ethernet" ? "BsEthernet" : "BsUsbPlug"
                // ConnectionStateIndicator's colours.
                color: button.state === "connected" ? Theme.green[700]
                     : button.state === "connecting" ? Theme.yellow600
                     : button.state === "error" ? Theme.red[600] : Theme.blue[700]
            }
            Icon {
                visible: button.state === "connected"
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
            objectName: "connectionText"
            visible: button.state !== "connected"
            text: button.state === "connecting" ? qsTr("Connecting...")
                : button.state === "error" ? qsTr("Unable to connect.") : qsTr("Connect to CNC")
            font.bold: true
            font.pixelSize: Theme.fontBase
            color: Theme.contentPrimary
            Layout.fillWidth: true
        }
        ColumnLayout {
            visible: button.state === "connected"
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
    // The disconnected icon's pulse (animate-pulse).
    QtObject {
        id: pulse
        property real phase: 0
        property NumberAnimation run: NumberAnimation {
            target: pulse; property: "phase"; from: 0; to: 2 * Math.PI
            duration: 2000; loops: Animation.Infinite
            running: button.state === "disconnected" && button.visible
        }
    }

    TapHandler {
        onTapped: {
            if (button.state === "connected") {
                disconnectMenu.open()
            } else if (button.state !== "connecting") {
                button.model.refresh()
                ports.open()
            }
        }
    }

    Menu {
        id: disconnectMenu
        objectName: "disconnectMenu"
        y: button.height + 4
        MenuItem {
            objectName: "disconnectItem"
            text: qsTr("Disconnect")
            height: Theme.touchTarget
            onTriggered: button.model.disconnectMachine()
        }
    }

    // PortListings.
    Popup {
        id: ports
        objectName: "portListings"
        y: button.height + 4
        width: Math.max(button.width, 300)
        padding: 0

        // A port's entry: an icon, the name and what it is.
        component PortEntry: Rectangle {
            id: entry
            property string icon
            property string name
            property string detail
            signal chosen()
            implicitHeight: 64
            Layout.fillWidth: true
            color: tap.pressed ? (Theme.dark ? Theme.surfaceHover : Theme.gray[100]) : "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                Icon { name: entry.icon; color: Theme.contentPrimary; width: 36; height: 36 }
                Item { Layout.fillWidth: true }
                ColumnLayout {
                    spacing: 2
                    Label {
                        text: entry.name
                        font.bold: true
                        color: Theme.contentPrimary
                        Layout.alignment: Qt.AlignRight
                    }
                    Label {
                        text: entry.detail
                        font.pixelSize: Theme.fontSm
                        color: Theme.dark ? Theme.contentMuted : Theme.gray[600]
                        Layout.alignment: Qt.AlignRight
                    }
                }
            }
            // The dotted blue dividers (divide-dotted divide-blue-300).
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.blue[300]
                opacity: 0.6
            }
            TapHandler {
                id: tap
                onTapped: {
                    ports.close()
                    entry.chosen()
                }
            }
        }

        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.dark ? Theme.surfaceRaised : "white"
            border.color: Theme.outline
        }

        contentItem: ColumnLayout {
            spacing: 0
            Label {
                visible: button.model.ports.length === 0
                text: qsTr("No USB devices found")
                color: Theme.contentPrimary
                Layout.alignment: Qt.AlignHCenter
                Layout.margins: 12
            }
            Repeater {
                model: button.model.ports
                PortEntry {
                    required property var modelData
                    objectName: "port_" + modelData.name
                    icon: "BsUsbPlug"
                    name: modelData.name
                    detail: qsTr("USB (%1)").arg(button.model.baudRate)
                    onChosen: button.model.connectTo(modelData.path)
                }
            }
            PortEntry {
                objectName: "portEthernet"
                icon: "BsEthernet"
                name: button.model.ethernetAddress
                detail: qsTr("Ethernet (port %1)").arg(button.model.ethernetPort)
                onChosen: button.model.connectEthernet()
            }
            // The built-in simulated boards (the port's own).
            PortEntry {
                objectName: "portSimulator"
                icon: "GrSatellite"
                name: qsTr("Simulator")
                detail: qsTr("Grbl, no hardware")
                onChosen: Backend.connectSimulator(false)
            }
            PortEntry {
                objectName: "portSimulatorHal"
                icon: "GrSatellite"
                name: qsTr("Simulator")
                detail: qsTr("grblHAL with an SD card, no hardware")
                onChosen: Backend.connectSimulator(true)
            }
            ColumnLayout {
                visible: button.model.unrecognizedPorts.length > 0
                spacing: 0
                Layout.fillWidth: true
                RowLayout {
                    objectName: "unrecognizedToggle"
                    Layout.fillWidth: true
                    Layout.margins: 8
                    Label {
                        text: qsTr("Unrecognized Ports")
                        color: Theme.contentSecondary
                        Layout.fillWidth: true
                    }
                    Icon {
                        name: "FaArrowAltCircleRight"
                        color: Theme.contentSecondary
                        rotation: unrecognized.visible ? 90 : 0
                        width: 20
                        height: 20
                    }
                    TapHandler { onTapped: unrecognized.visible = !unrecognized.visible }
                }
                ColumnLayout {
                    id: unrecognized
                    visible: false
                    spacing: 0
                    Layout.fillWidth: true
                    Repeater {
                        model: button.model.unrecognizedPorts
                        PortEntry {
                            required property var modelData
                            icon: "BsUsbPlug"
                            name: modelData.name
                            detail: qsTr("USB (%1)").arg(button.model.baudRate)
                            onChosen: button.model.connectTo(modelData.path)
                        }
                    }
                }
            }
        }
    }
}
