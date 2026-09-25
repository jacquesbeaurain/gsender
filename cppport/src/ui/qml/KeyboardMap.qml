import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The keyboard map (Accessibility's "Show keyboard shortcut map"): the
// shortcuts that work now, by category, over the bottom of the window; its
// close button turns the setting off.
Rectangle {
    id: map
    objectName: "keyboardMap"

    property var groups: []
    function refresh() { groups = Backend.activeShortcuts() }

    visible: Backend.keyboardMap
    onVisibleChanged: if (visible) refresh()
    Component.onCompleted: refresh()
    Connections {
        target: Backend
        // After the shortcuts have been rebuilt from the same change.
        function onAppSettingsChanged() { Qt.callLater(map.refresh) }
        function onConnectionChanged() { Qt.callLater(map.refresh) }
    }

    radius: 12
    color: Qt.rgba(0, 0, 0, 0.9)
    border.color: Qt.rgba(1, 1, 1, 0.2)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label { Layout.fillWidth: true; text: qsTr("Active Keyboard Shortcuts"); font.pixelSize: Theme.fontXl; font.bold: true; color: "white" }
                Label { text: qsTr("Dynamic map of currently available shortcuts"); color: Qt.rgba(1, 1, 1, 0.7) }
            }
            Item {
                objectName: "closeKeyboardMap"
                Layout.alignment: Qt.AlignTop
                implicitWidth: Theme.touchTarget
                implicitHeight: Theme.touchTarget
                Icon { anchors.centerIn: parent; name: "MdClose"; color: Qt.rgba(1, 1, 1, 0.7); width: 22; height: 22 }
                TapHandler { onTapped: Backend.keyboardMap = false }
            }
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: grid.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            GridLayout {
                id: grid
                width: parent.width
                columns: 3
                columnSpacing: 32
                rowSpacing: 18
                Repeater {
                    model: map.groups
                    ColumnLayout {
                        required property var modelData
                        objectName: "keyboardMapGroup_" + modelData.category
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: 4
                        Label {
                            text: modelData.category.toUpperCase()
                            font.pixelSize: Theme.fontXs
                            font.bold: true
                            color: "#60a5fa"
                        }
                        Repeater {
                            model: modelData.shortcuts
                            RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 12
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    elide: Text.ElideRight
                                    color: Qt.rgba(1, 1, 1, 0.75)
                                    font.pixelSize: Theme.fontSm
                                }
                                Rectangle {
                                    implicitWidth: keysLabel.implicitWidth + 12
                                    implicitHeight: 22
                                    radius: 4
                                    color: Qt.rgba(1, 1, 1, 0.1)
                                    border.color: Qt.rgba(1, 1, 1, 0.2)
                                    Label {
                                        id: keysLabel
                                        anchors.centerIn: parent
                                        text: modelData.keys
                                        font.family: "monospace"
                                        font.pixelSize: Theme.fontSm
                                        color: "#bfdbfe"
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
