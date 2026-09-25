import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Tools (routes/tools): the cards, each opening its tool on this page with
// Go Back to return. A tool asked for elsewhere (Rotary Surfacing from the
// Rotary tab) opens directly.
Item {
    id: tools
    objectName: "toolsPage"

    property string current: ""

    // The tools there are: {key, title, description, icon, component}.
    readonly property var cards: [
        { key: "surfacing", title: qsTr("Surfacing"), description: qsTr("Flatten your wasteboard or other non-flat stock"), icon: "GiFlatPlatform" },
        { key: "rotarySurfacing", title: qsTr("Rotary Surfacing"), description: qsTr("Turn square material into round stock for rotary cutting"), icon: "BiSolidCylinder" },
        { key: "movementTuning", title: qsTr("Movement Tuning"), description: qsTr("Ensure that each axis of your machine is moving accurately"), icon: "TbRulerMeasure" },
        { key: "squaring", title: qsTr("XY Squaring"), description: qsTr("Get your CNC accurately aligned to make square cuts"), icon: "MdSquareFoot" },
        { key: "shortcuts", title: qsTr("Keyboard Shortcuts"), description: qsTr("Set up keyboard shortcuts for easy navigation and control"), icon: "FaKeyboard" },
        { key: "gamepad", title: qsTr("Gamepad"), description: qsTr("Easy hand-held CNC control using pre-made or custom profiles"), icon: "FaGamepad" },
        { key: "sd", title: qsTr("SD Card Manager"), description: qsTr("Manage and view files on your SD card"), icon: "FaSdCard" },
        { key: "accessoryInstall", title: qsTr("Accessory Installation"), description: qsTr("Install various CNC Accessories"), icon: "LuDrill" },
        { key: "plugins", title: qsTr("Plugins"), description: qsTr("Manage installed UI plugins"), icon: "PiPuzzlePiece" }
    ]
    // The tools this version has (the rest say so).
    property var available: ({ surfacing: surfacingTool, rotarySurfacing: rotarySurfacingTool })

    function open(key) {
        if (available[key])
            current = key
        else
            Backend.notify(qsTr("%1 is not available in this version yet").arg(cards.find(c => c.key === key).title), "info")
    }

    Connections {
        target: Backend
        function onToolRequested(name) { tools.open(name) }
    }

    Rectangle { anchors.fill: parent; color: Theme.dark ? Theme.surfaceBase : "white" }

    // The cards.
    Flickable {
        visible: tools.current === ""
        anchors.fill: parent
        contentHeight: grid.implicitHeight + header.implicitHeight + 64
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: header
            x: 64
            y: 16
            width: parent.width - 128
            spacing: 8
            Label { text: qsTr("Tools"); font.pixelSize: 30; font.bold: true; color: Theme.contentPrimary }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Tools are plugins that can be installed and used to extend the functionality of gSender. Some are built in to gSender, some are third party plugins.")
                font.pixelSize: Theme.fontSm
                color: Theme.gray[500]
            }
            GridLayout {
                id: grid
                Layout.fillWidth: true
                Layout.topMargin: 8
                columns: tools.width > 1024 ? 3 : 2
                rowSpacing: 16
                columnSpacing: 16
                Repeater {
                    model: tools.cards
                    Rectangle {
                        required property var modelData
                        objectName: "toolCard_" + modelData.key
                        Layout.fillWidth: true
                        Layout.preferredHeight: 224
                        radius: Theme.radius
                        color: cardTap.pressed ? Theme.gray[300] : (Theme.dark ? Theme.surfaceRaised : Theme.gray[100])
                        border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: modelData.title
                                font.pixelSize: Theme.fontXl
                                font.weight: Font.DemiBold
                                color: Theme.contentPrimary
                            }
                            Label {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.Wrap
                                text: modelData.description
                                font.pixelSize: Theme.fontSm
                                color: Theme.gray[500]
                            }
                            Item { Layout.fillHeight: true }
                            Icon {
                                Layout.alignment: Qt.AlignHCenter
                                name: modelData.icon
                                color: Theme.contentPrimary
                                width: 56; height: 56
                            }
                        }
                        TapHandler { id: cardTap; onTapped: tools.open(modelData.key) }
                    }
                }
            }
        }
    }

    // The tool open.
    Loader {
        id: toolLoader
        objectName: "toolLoader"
        anchors.fill: parent
        active: tools.current !== ""
        sourceComponent: tools.available[tools.current] || null
    }
    Connections {
        target: toolLoader.item
        ignoreUnknownSignals: true
        function onBack() { tools.current = "" }
    }

    Component {
        id: surfacingTool
        SurfacingTool {}
    }
    Component {
        id: rotarySurfacingTool
        RotarySurfacingTool {}
    }
}
