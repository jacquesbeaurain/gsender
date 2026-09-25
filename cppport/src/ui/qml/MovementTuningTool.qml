import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Movement Tuning (features/MovementTuning): which axis and why; mark where
// it is, move it, measure how far it went; then the verdict and, when it is
// off, Update step/mm (asked first).
ToolPage {
    id: tool
    objectName: "movementTuningTool"
    title: qsTr("Movement Tuning")

    property MovementTuningModel model: MovementTuningModel { objectName: "movementTuning" }
    readonly property var steps: ["mark", "move", "measure"]
    function stepIndex(name) { return name === "intro" ? -1 : name === "result" ? 3 : steps.indexOf(name) }

    Flickable {
        anchors.fill: parent
        contentHeight: body.implicitHeight + 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: body
            width: Math.min(parent.width, 760)
            spacing: 16

            // The introduction and the axis.
            ColumnLayout {
                visible: tool.model.step === "intro"
                Layout.fillWidth: true
                spacing: 12
                Repeater {
                    model: [
                        qsTr("If you're looking to use your CNC for more accurate work and notice a specific axis is always off by a small amount - say 102mm instead of 100 - then use this tool."),
                        qsTr("Since CNC firmware needs to understand its hardware to make exact movements, small manufacturing variations in the motors, lead screws, pulleys, or incorrect firmware will create inaccuracies over longer distances."),
                        qsTr("By testing for this difference using a marker or tape and a measuring tape, this tool will better tune the firmware to your machine.")
                    ]
                    Label {
                        required property string modelData
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: modelData
                        color: Theme.contentSecondary
                    }
                }
                RowLayout {
                    spacing: 12
                    Label { text: qsTr("Axis to Tune"); font.bold: true; color: Theme.contentPrimary }
                    Repeater {
                        model: ["X", "Y", "Z"]
                        GButton {
                            required property string modelData
                            objectName: "tuningAxis_" + modelData
                            text: qsTr("%1-Axis").arg(modelData)
                            variant: tool.model.axis === modelData ? "primary" : "secondary"
                            onClicked: tool.model.axis = modelData
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.bold: true
                    color: Theme.contentPrimary
                    text: qsTr("Whichever axis you'll be tuning, please place it in an initial location so that it'll have space to move to the right (for X), backwards (for Y), and downwards (for Z).")
                }
                Rectangle {
                    visible: !tool.model.connected
                    Layout.fillWidth: true
                    implicitHeight: note.implicitHeight + 16
                    radius: 6
                    color: "#fef9c3"
                    border.color: "#fde68a"
                    Label {
                        id: note
                        anchors.fill: parent
                        anchors.margins: 8
                        wrapMode: Text.Wrap
                        color: "#854d0e"
                        text: qsTr("Please connect to a device before starting the movement tuning wizard.")
                    }
                }
                GButton {
                    objectName: "tuningStart"
                    variant: "primary"
                    text: qsTr("Start Movement Tuning")
                    enabled: tool.model.canMove
                    onClicked: tool.model.start()
                }
            }

            // Mark, move, measure.
            ColumnLayout {
                visible: ["mark", "move", "measure"].includes(tool.model.step)
                Layout.fillWidth: true
                spacing: 12
                Label { text: qsTr("Instructions"); font.bold: true; font.pixelSize: Theme.fontLg; color: Theme.contentPrimary }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: instruction.implicitHeight + 24
                    radius: 6
                    color: Theme.dark ? Theme.surfaceRaised : "#eff6ff"
                    border.color: Theme.dark ? Theme.outline : "#bfdbfe"
                    Label {
                        id: instruction
                        objectName: "tuningInstruction"
                        anchors.fill: parent
                        anchors.margins: 12
                        wrapMode: Text.Wrap
                        text: tool.model.instruction
                        color: Theme.contentPrimary
                    }
                }
                GridLayout {
                    columns: 3
                    columnSpacing: 12
                    rowSpacing: 10
                    readonly property int at: tool.stepIndex(tool.model.step)

                    StepMark { done: parent.at > 0; current: parent.at === 0 }
                    GButton {
                        objectName: "tuningMark"
                        Layout.preferredWidth: 220
                        text: qsTr("Mark First Location")
                        enabled: tool.model.step === "mark"
                        onClicked: tool.model.markLocation()
                    }
                    Item { width: 1 }

                    StepMark { done: parent.at > 1; current: parent.at === 1 }
                    GButton {
                        objectName: "tuningMove"
                        Layout.preferredWidth: 220
                        text: qsTr("Move %1-axis").arg(tool.model.axis)
                        enabled: tool.model.step === "move" && tool.model.canMove
                        onClicked: tool.model.moveAxis()
                    }
                    NumberField {
                        objectName: "tuningMoveDistance"
                        Layout.preferredWidth: 140
                        enabled: tool.model.step === "move"
                        value: tool.model.moveDistance
                        suffix: tool.model.units
                        onCommitted: (text) => { if (text !== "" && !isNaN(Number(text))) tool.model.moveDistance = Number(text) }
                    }

                    StepMark { done: parent.at > 2; current: parent.at === 2 }
                    GButton {
                        objectName: "tuningTravelled"
                        Layout.preferredWidth: 220
                        text: qsTr("Set Distance Travelled")
                        enabled: tool.model.step === "measure"
                        onClicked: tool.model.confirmTravelled()
                    }
                    NumberField {
                        objectName: "tuningMeasured"
                        Layout.preferredWidth: 140
                        enabled: tool.model.step === "measure"
                        value: tool.model.travelled
                        suffix: tool.model.units
                        onCommitted: (text) => { if (text !== "" && !isNaN(Number(text))) tool.model.travelled = Number(text) }
                    }
                }
            }

            // The verdict.
            ColumnLayout {
                visible: tool.model.step === "result"
                Layout.fillWidth: true
                spacing: 16
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: result.implicitHeight + 32
                    radius: 8
                    color: tool.model.accurate ? "#dcfce7" : "#fef9c3"
                    Label {
                        id: result
                        objectName: "tuningResult"
                        anchors.fill: parent
                        anchors.margins: 16
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        textFormat: Text.StyledText
                        text: tool.model.resultText
                        color: tool.model.accurate ? "#166534" : "#854d0e"
                        font.pixelSize: Theme.fontLg
                    }
                }
                GButton {
                    objectName: "tuningUpdate"
                    visible: !tool.model.accurate
                    Layout.alignment: Qt.AlignHCenter
                    variant: "primary"
                    text: qsTr("Update step/mm")
                    enabled: tool.model.connected
                    onClicked: tuningConfirm.open()
                }
            }

            GButton {
                objectName: "tuningRestart"
                visible: tool.model.step !== "intro"
                text: qsTr("Restart Wizard")
                onClicked: tool.model.restart()
            }
        }
    }

    ConfirmDialog {
        id: tuningConfirm
        objectName: "tuningConfirm"
        title: qsTr("Update Firmware")
        message: tool.model.updateText
        actionText: qsTr("Update Firmware")
        onAccepted: tool.model.updateFirmware()
    }
}
