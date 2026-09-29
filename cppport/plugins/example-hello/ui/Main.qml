import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

Item {
    id: root
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
                text: qsTr("This is a sandboxed plugin running with QML UI and WebAssembly host bindings.")
                color: Theme.contentSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            GButton {
                text: qsTr("Send Test Command")
                onClicked: {
                    if (typeof PluginBridge !== "undefined") {
                        PluginBridge.send("machine:command", { command: "$G" })
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
