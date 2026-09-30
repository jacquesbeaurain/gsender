import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

Item {
    id: root
    // The bridge PluginHost hands the plugin: capability-checked requests, topics, storage.
    property var gsender: null
    implicitWidth: 600
    implicitHeight: 400

    Card {
        anchors.fill: parent
        anchors.margins: 16

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            Label {
                text: qsTr("Hello World Plugin")
                font.pixelSize: Theme.font2xl
                font.bold: true
                color: Theme.contentPrimary
            }

            Label {
                text: qsTr("A plugin with a QML UI and WebAssembly logic, talking to gSender through its bridge.")
                color: Theme.contentSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            GButton {
                text: qsTr("Send Test Command")
                onClicked: {
                    if (root.gsender) {
                        root.gsender.send("machine:command", { command: "$G" })
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
