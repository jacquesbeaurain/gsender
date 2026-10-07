import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The touch UI's window (workspace/index): the top bar over the navigation
// rail and the page it selects.
ApplicationWindow {
    id: window
    objectName: "mainWindow"

    width: 1280
    height: 800
    visible: true
    title: qsTr("gSender")
    color: Theme.dark ? Theme.surfaceBase : "white"

    font.pixelSize: Theme.fontBase

    // The Basic style's controls take their colours from the palette, which
    // would otherwise follow the operating system's light or dark setting:
    // set them from the app's own theme.
    palette {
        window: Theme.dark ? Theme.surfaceBase : "white"
        windowText: Theme.contentPrimary
        base: Theme.dark ? Theme.surfaceSunken : "white"
        alternateBase: Theme.surfaceRaised
        text: Theme.contentPrimary
        placeholderText: Theme.contentMuted
        button: Theme.dark ? Theme.surfaceRaised : "white"
        buttonText: Theme.contentPrimary
        highlight: Theme.blue[500]
        highlightedText: "white"
        mid: Theme.outline
        midlight: Theme.outlineSubtle
        light: Theme.dark ? Theme.surfaceHover : "white"
        dark: Theme.outlineStrong
        shadow: Theme.dark ? "#000000" : Theme.gray[400]
        toolTipBase: Theme.dark ? Theme.surfaceElevated : Theme.gray[900]
        toolTipText: Theme.dark ? Theme.contentPrimary : "white"
    }

    // Basics' "Prompt on exit": closing asks first.
    property bool exitConfirmed: false
    onClosing: (close) => {
        if (Backend.promptExit && !exitConfirmed) {
            close.accepted = false
            exitPrompt.open()
        }
    }
    ConfirmDialog {
        id: exitPrompt
        objectName: "exitPrompt"
        title: qsTr("Exit gSender")
        message: qsTr("Are you sure you want to exit?")
        actionText: qsTr("Exit")
        onAccepted: {
            window.exitConfirmed = true
            window.close()
        }
    }

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
                Layout.preferredWidth: 61   // upstream's w-[60px] and its edge
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
