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
            spacing: 12

            Label {
                text: qsTr("Storage Test Plugin")
                font.pixelSize: Theme.font2xl
                font.bold: true
                color: Theme.contentPrimary
            }

            Label {
                text: qsTr("Exercises per-plugin persistent storage isolation.")
                color: Theme.contentSecondary
            }

            RowLayout {
                spacing: 8
                NumberField {
                    id: keyInput
                    placeholderText: qsTr("Key")
                    Layout.preferredWidth: 150
                }
                NumberField {
                    id: valInput
                    placeholderText: qsTr("Value")
                    Layout.fillWidth: true
                }
            }

            RowLayout {
                spacing: 8
                GButton {
                    text: qsTr("Save Key")
                    onClicked: {
                        if (typeof PluginBridge !== "undefined" && keyInput.text.length > 0) {
                            PluginBridge.send("storage:set", { key: keyInput.text, value: valInput.text })
                        }
                    }
                }
                GButton {
                    text: qsTr("Clear All")
                    onClicked: {
                        if (typeof PluginBridge !== "undefined") {
                            PluginBridge.send("storage:clear", {})
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
