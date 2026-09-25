import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The touch UI's window (workspace/index): the top bar over the navigation
// rail and the page it selects.
ApplicationWindow {
    id: window
    objectName: "mainWindow"

    width: 1400
    height: 900
    visible: true
    title: qsTr("gSender")
    color: Theme.dark ? Theme.surfaceBase : "white"

    font.pixelSize: Theme.fontBase

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar {
            Layout.fillWidth: true
            z: 1   // the status pill hangs over the page
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            NavRail {
                id: rail
                Layout.fillHeight: true
                Layout.preferredWidth: Math.max(56, Math.min(70, window.width * 0.05))
            }
            StackLayout {
                objectName: "pages"
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: rail.currentIndex

                CarvePage {}
                StatsPage {}
                ToolsPage {}
                ConfigPage {}
            }
        }
    }

    // The Helper's panel over the top left; the pop-ups at the bottom right.
    HelperPanel {
        x: 8
        y: 60
        width: window.width / 3
        height: window.height / 3 - y
        z: 10
    }
    ToastArea {
        anchors.fill: parent
        z: 11
    }
    KeyboardMap {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        width: Math.min(1000, window.width - 64)
        height: window.height * 0.7
        z: 8
    }
    JobAlerts {}
    ToolChange {
        anchors.fill: parent
        z: 9
    }
    // A tool asked for elsewhere (Rotary Surfacing from the Rotary tab).
    Connections {
        target: Backend
        function onToolRequested(name) { rail.currentIndex = 2 }
        function onPageRequested(name) {
            const index = ["carve", "stats", "tools", "config"].indexOf(name)
            if (index >= 0)
                rail.currentIndex = index
        }
    }
}
