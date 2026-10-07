import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Probe (features/Probe): the plate (when the touch plate switcher is on),
// the routines as a segmented row, the tool diameter for routines that need
// one, and Probe; beside them the routine's picture, with the plate's corner
// (tap to move it round). Probe opens the run step; with the 3D probe, the
// grid button beside it opens the rectangular grid capture.
Item {
    id: tab
    objectName: "probeTab"

    property ProbeModel model: ProbeModel {}

    function openRun() {
        if (!model.canClick)
            return
        model.beginRun()
        run.open()
    }

    RunProbeDialog {
        id: run
        model: tab.model
    }

    function openGrid() {
        if (model.canClick)
            grid.openCapture()
    }

    RectangularGridDialog {
        id: grid
        probe: tab.model
    }

    RowLayout {
        anchors.fill: parent
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 5
            spacing: 4

            Item { Layout.fillHeight: true }
            GButton {
                objectName: "probePlate"
                visible: tab.model.plateSwitcher
                Layout.alignment: Qt.AlignHCenter
                text: tab.model.plateType
                fontSize: Theme.fontSm
                onClicked: plateMenu.popup(this, 0, height)
                Menu {
                    id: plateMenu
                    Repeater {
                        model: tab.model.plateTypes
                        MenuItem {
                            required property string modelData
                            text: modelData
                            height: Theme.touchTarget
                            onTriggered: tab.model.setPlateType(modelData)
                        }
                    }
                }
            }
            // The routines.
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: routines.implicitWidth + 4
                implicitHeight: routines.implicitHeight + 4
                radius: Theme.radiusSmall
                color: Theme.dark ? Theme.surfaceRaised : "white"
                border.color: Theme.dark ? Theme.outline : Theme.gray[300]
                Row {
                    id: routines
                    anchors.centerIn: parent
                    Repeater {
                        model: tab.model.commands
                        Rectangle {
                            required property var modelData
                            required property int index
                            objectName: "probeRoutine_" + modelData.label
                            width: 40   // upstream's w-10 h-[35px]
                            height: 35
                            radius: Theme.radiusSmall
                            color: index === tab.model.selected ? Qt.rgba(0x52 / 255, 0x91 / 255, 0xcd / 255, 0.3)
                                                                : "transparent"
                            Label {
                                id: label
                                anchors.centerIn: parent
                                text: modelData.label
                                font.pixelSize: Theme.fontBase
                                font.weight: Font.Medium
                                color: Theme.contentPrimary
                            }
                            TapHandler { onTapped: tab.model.selectCommand(index) }
                        }
                    }
                }
            }
            // The tool diameter.
            GButton {
                id: toolButton
                objectName: "probeTool"
                visible: tab.model.needsTool
                Layout.fillWidth: true
                Layout.maximumWidth: 260
                Layout.alignment: Qt.AlignHCenter
                variant: "outline"
                fontSize: Theme.fontSm
                text: tab.model.tool
                      ? tab.model.tool + (Number(tab.model.tool) > 0 ? " " + tab.model.units : "")
                      : qsTr("Select diameter")
                onClicked: toolPopup.open()
                Popup {
                    id: toolPopup
                    objectName: "probeToolPopup"
                    y: toolButton.height + 4
                    width: Math.max(toolButton.width, 240)
                    padding: 4
                    margins: 8  // kept inside the window: above the button near the bottom
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        color: Theme.dark ? Theme.surfaceElevated : "white"
                        border.color: Theme.outline
                    }
                    contentItem: ColumnLayout {
                        spacing: 4
                        ListView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(contentHeight, 240)
                            clip: true
                            model: tab.model.tools
                            delegate: Rectangle {
                                required property var modelData
                                objectName: "probeToolOption_" + modelData.value
                                width: ListView.view.width
                                height: Theme.touchTarget
                                radius: 4
                                color: modelData.value === tab.model.tool
                                       ? (Theme.dark ? Theme.surfaceRaised : Theme.robin[200]) : "transparent"
                                Label {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.label
                                    color: Theme.contentPrimary
                                }
                                TapHandler {
                                    onTapped: {
                                        tab.model.selectTool(modelData.value)
                                        toolPopup.close()
                                    }
                                }
                                Item {
                                    visible: modelData.removable
                                    anchors.right: parent.right
                                    width: Theme.touchTarget
                                    height: parent.height
                                    Icon { anchors.centerIn: parent; name: "LuX"; color: Theme.contentMuted; width: 20; height: 20 }
                                    TapHandler { onTapped: tab.model.removeTool(modelData.value) }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            TextField {
                                id: custom
                                objectName: "probeCustomTool"
                                Layout.fillWidth: true
                                implicitHeight: 40
                                placeholderText: qsTr("Custom diameter (%1)").arg(tab.model.units)
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                color: Theme.contentPrimary
                                onAccepted: addButton.clicked()
                            }
                            GButton {
                                id: addButton
                                objectName: "probeAddTool"
                                text: qsTr("Add")
                                iconName: "LuPlus"
                                iconSize: 16
                                fontSize: Theme.fontSm
                                onClicked: {
                                    if (custom.text && tab.model.addTool(custom.text)) {
                                        custom.text = ""
                                        toolPopup.close()
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8
                GButton {
                    objectName: "probeButton"
                    text: qsTr("Probe")
                    implicitHeight: 40
                    enabled: tab.model.canClick
                    onClicked: tab.openRun()
                }
                // "Capture Rectangular Grid with 3D Probe": a 4x4 grid of points.
                GButton {
                    id: gridButton
                    objectName: "gridCaptureButton"
                    visible: tab.model.probe3D
                    implicitWidth: Theme.touchTarget
                    enabled: tab.model.canClick
                    onClicked: tab.openGrid()
                    Accessible.name: qsTr("Capture Rectangular Grid with 3D Probe")
                    Grid {
                        anchors.centerIn: parent
                        columns: 4
                        spacing: 3
                        Repeater {
                            model: 16
                            Rectangle { width: 4; height: 4; radius: 2; color: gridButton.foreground }
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }

        // The picture, and the corner.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            AnimatedImage {
                objectName: "probeImage"
                anchors.fill: parent
                anchors.bottomMargin: cornerRow.height
                fillMode: Image.PreserveAspectFit
                source: tab.model.image
                playing: tab.visible
            }
            Row {
                id: cornerRow
                objectName: "probeCorner"
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                height: 28
                spacing: 6
                visible: tab.model.plateType !== "Z Probe" && tab.model.commandId !== "Z Touch"
                // Four dots for the stock's corners, the plate's filled.
                Grid {
                    anchors.verticalCenter: parent.verticalCenter
                    columns: 2
                    spacing: 3
                    Repeater {
                        // Top left, top right, bottom left, bottom right.
                        model: [1, 2, 0, 3]
                        Rectangle {
                            required property int modelData
                            width: 8; height: 8; radius: 4
                            color: modelData === tab.model.corner ? Theme.blue[500] : "transparent"
                            border.color: Theme.contentMuted
                        }
                    }
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: tab.model.cornerName
                    font.pixelSize: Theme.fontXs
                    color: Theme.contentMuted
                }
                TapHandler { onTapped: tab.model.nextCorner() }
            }
        }
    }
}
