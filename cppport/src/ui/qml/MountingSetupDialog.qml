import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Rotary Mounting Setup (features/Rotary/MountingSetup): whether the track
// lines up, the end mill, the holes and the extension - the illustration
// following them - then the program that bores the holes as the job.
Popup {
    id: dialog
    objectName: "mountingSetup"

    property RotaryModel model
    property bool linesUp: false
    property bool quarterInch: true
    property int holes: 6
    property bool longExtension: false

    // Upstream's defaults each time it opens.
    function openFresh() {
        linesUp = false
        quarterInch = true
        holes = 6
        longExtension = false
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 20
    width: Math.min(820, parent ? parent.width - 32 : 820)
    height: Math.min(implicitHeight, parent ? parent.height - 32 : implicitHeight)
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
    background: Rectangle {
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceElevated : "white"
        border.color: Theme.outlineSubtle
    }

    // A question and its two answers.
    component Choice: RowLayout {
        id: choice
        property string question
        property var answers: []
        property int chosen: 0
        signal picked(int index)
        Layout.fillWidth: true
        spacing: 8
        Label {
            text: choice.question
            color: Theme.contentPrimary
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Repeater {
            model: choice.answers
            GButton {
                required property string modelData
                required property int index
                objectName: "mounting_" + modelData
                text: modelData
                variant: index === choice.chosen ? "primary" : "outline"
                Layout.preferredWidth: 150
                onClicked: choice.picked(index)
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 10
        Label {
            text: qsTr("Rotary Mounting Setup")
            font.pixelSize: Theme.fontLg
            font.bold: true
            color: Theme.contentPrimary
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.contentSecondary
            text: qsTr("Make sure your router is mounted as far down as possible with the bit inserted not too far into the collet to prevent bottoming out.")
        }
        Choice {
            question: qsTr("Does the mounting track line up without interference?")
            answers: [qsTr("Lines up"), qsTr("Does not line up")]
            chosen: dialog.linesUp ? 0 : 1
            onPicked: (index) => dialog.linesUp = index === 0
        }
        Choice {
            question: qsTr("End Mill Diameter")
            answers: ["¼\"", "⅛\""]
            chosen: dialog.quarterInch ? 0 : 1
            onPicked: (index) => dialog.quarterInch = index === 0
        }
        Choice {
            question: qsTr("Number of Holes")
            answers: ["6", "10"]
            chosen: dialog.holes === 10 ? 1 : 0
            onPicked: (index) => dialog.holes = index === 1 ? 10 : 6
        }
        Choice {
            question: qsTr("Extension Track Length")
            answers: ["400mm", "460mm"]
            chosen: dialog.longExtension ? 1 : 0
            onPicked: (index) => dialog.longExtension = index === 1
        }
        Image {
            objectName: "mountingIllustration"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 280
            fillMode: Image.PreserveAspectFit
            source: dialog.model ? dialog.model.mountingImage(dialog.linesUp, dialog.holes) : ""
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            GButton {
                text: qsTr("Cancel")
                variant: "outline"
                onClicked: dialog.close()
            }
            GButton {
                objectName: "mountingLoad"
                variant: "primary"
                text: qsTr("Load G-Code to Visualizer")
                onClicked: {
                    if (dialog.model.loadMounting(dialog.linesUp, dialog.quarterInch, dialog.holes,
                                                  dialog.longExtension))
                        dialog.close()
                }
            }
        }
    }
}
