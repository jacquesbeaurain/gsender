import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

Item {
    id: root
    implicitWidth: 600
    implicitHeight: 400
    property var gsender: null

    property double posX: 0.0
    property double posY: 0.0
    property double posZ: 0.0
    property double posA: 0.0
    property string activeState: "Idle"
    property var eventHistory: []

    Component.onCompleted: {
        if (gsender) {
            gsender.subscribe("workspace", function(data) {
                if (data.x !== undefined) posX = data.x
                if (data.y !== undefined) posY = data.y
                if (data.z !== undefined) posZ = data.z
                if (data.a !== undefined) posA = data.a
            })
            gsender.subscribe("controller", function(data) {
                if (data.activeState) activeState = data.activeState
                const list = eventHistory.slice()
                list.unshift(new Date().toLocaleTimeString() + ": Controller state -> " + data.activeState)
                if (list.length > 20) list.pop()
                eventHistory = list
            })
        }
    }

    Card {
        anchors.fill: parent
        anchors.margins: 16

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: qsTr("Controller Events & Live Telemetry")
                    font.pixelSize: Theme.font2xl
                    font.bold: true
                    color: Theme.contentPrimary
                    Layout.fillWidth: true
                }
                Rectangle {
                    radius: 12
                    implicitWidth: 90
                    implicitHeight: 28
                    color: activeState === "Run" ? Theme.green[500] : (activeState === "Alarm" ? Theme.red[500] : Theme.blue[500])
                    Label {
                        anchors.centerIn: parent
                        text: root.activeState
                        color: "white"
                        font.bold: true
                        font.pixelSize: Theme.fontSm
                    }
                }
            }

            // DRO readout row
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Repeater {
                    model: [
                        { axis: "X", val: root.posX },
                        { axis: "Y", val: root.posY },
                        { axis: "Z", val: root.posZ },
                        { axis: "A", val: root.posA }
                    ]
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 70
                        radius: Theme.radius
                        color: Theme.dark ? Theme.surfaceRaised : Theme.gray[100]
                        border.color: Theme.dark ? Theme.outline : Theme.gray[300]
                        ColumnLayout {
                            anchors.centerIn: parent
                            Label {
                                text: modelData.axis
                                font.bold: true
                                color: Theme.contentSecondary
                                Layout.alignment: Qt.AlignHCenter
                            }
                            Label {
                                text: modelData.val.toFixed(3)
                                font.pixelSize: Theme.fontXl
                                font.bold: true
                                color: Theme.contentPrimary
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }
                }
            }

            Label {
                text: qsTr("Recent Controller Events:")
                font.bold: true
                color: Theme.contentPrimary
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.eventHistory
                delegate: Item {
                    width: ListView.view.width
                    height: 26
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        color: Theme.contentSecondary
                        font.pixelSize: Theme.fontSm
                    }
                }
            }
        }
    }
}
