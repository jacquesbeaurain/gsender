import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

Item {
    id: root
    implicitWidth: 320
    implicitHeight: 200
    property var gsender: null

    property string selectedCorner: "Bottom-Left"

    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        width: 240
        height: 140
        radius: Theme.radius
        color: Qt.rgba(0.1, 0.1, 0.1, 0.85)
        border.color: Theme.blue[500]
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Label {
                text: qsTr("🎯 Corner Finder Overlay")
                font.bold: true
                color: "white"
                font.pixelSize: Theme.fontSm
            }

            Grid {
                columns: 2
                spacing: 6
                Layout.alignment: Qt.AlignHCenter

                Repeater {
                    model: ["Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right"]
                    Rectangle {
                        width: 100
                        height: 36
                        radius: 4
                        color: root.selectedCorner === modelData ? Theme.blue[500] : Qt.rgba(1, 1, 1, 0.15)
                        Label {
                            anchors.centerIn: parent
                            text: modelData
                            color: "white"
                            font.pixelSize: Theme.fontXs
                        }
                        TapHandler {
                            onTapped: {
                                root.selectedCorner = modelData
                                if (root.gsender) {
                                    root.gsender.send("viewer:camera:set", { view: "top" })
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
