import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// XY Squaring (features/Squaring): set up, mark three points in a triangle
// (moving X then Y between them), measure its sides, and see how square the
// machine is; beside, the triangle. Restart, Back and Next beneath.
ToolPage {
    id: tool
    objectName: "squaringTool"
    title: qsTr("XY Squaring")

    property SquaringModel model: SquaringModel { objectName: "squaring" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 24

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                contentHeight: steps.implicitHeight + 8
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ColumnLayout {
                    id: steps
                    width: parent.width
                    spacing: 12
                    Label {
                        objectName: "squaringTitle"
                        text: tool.model.title
                        font.pixelSize: Theme.fontXl
                        font.bold: true
                        color: Theme.contentPrimary
                    }
                    Label {
                        visible: text !== ""
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: tool.model.description
                        color: Theme.contentSecondary
                    }
                    Rectangle {
                        visible: tool.model.mainStep < 3
                        Layout.fillWidth: true
                        implicitHeight: instruction.implicitHeight + 24
                        radius: 6
                        color: Theme.dark ? Theme.surfaceRaised : "#eff6ff"
                        border.color: Theme.dark ? Theme.outline : "#bfdbfe"
                        Label {
                            id: instruction
                            objectName: "squaringInstruction"
                            anchors.fill: parent
                            anchors.margins: 12
                            wrapMode: Text.Wrap
                            text: tool.model.instruction
                            color: Theme.contentPrimary
                        }
                    }
                    Repeater {
                        // By count, so the rows (and a field being edited) stay as the
                        // values change.
                        model: tool.model.rows.length
                        RowLayout {
                            required property int index
                            readonly property var modelData: tool.model.rows[index] || ({})
                            spacing: 12
                            StepMark { done: modelData.completed; current: modelData.current }
                            GButton {
                                objectName: "squaringRow_" + index
                                Layout.preferredWidth: 220
                                text: modelData.button
                                enabled: modelData.enabled
                                onClicked: tool.model.completeRow(index)
                            }
                            NumberField {
                                objectName: "squaringValue_" + index
                                visible: modelData.hasValue
                                Layout.preferredWidth: 140
                                enabled: modelData.current
                                value: modelData.value
                                suffix: tool.model.units
                                onCommitted: (text) => { if (text !== "" && !isNaN(Number(text))) tool.model.setRowValue(index, Number(text)) }
                            }
                        }
                    }
                    Label {
                        objectName: "squaringResult"
                        visible: tool.model.mainStep === 3
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        textFormat: Text.StyledText
                        text: tool.model.resultText
                        color: Theme.contentPrimary
                    }
                    GButton {
                        objectName: "squaringUpdate"
                        visible: tool.model.updateNeeded
                        variant: "primary"
                        text: qsTr("Update step/mm")
                        onClicked: squaringConfirm.open()
                    }
                }
            }
            SquaringDiagram {
                objectName: "squaringDiagram"
                model: tool.model
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            GButton {
                objectName: "squaringRestart"
                text: qsTr("Restart")
                onClicked: tool.model.restart()
            }
            Item { Layout.fillWidth: true }
            GButton {
                objectName: "squaringBack"
                text: qsTr("Back")
                enabled: tool.model.mainStep > 0
                onClicked: tool.model.back()
            }
            GButton {
                objectName: "squaringNext"
                visible: tool.model.mainStep < 3
                variant: "primary"
                text: tool.model.mainStep === 2 ? qsTr("See Results") : qsTr("Next")
                enabled: tool.model.canGoNext
                onClicked: tool.model.next()
            }
        }
    }

    ConfirmDialog {
        id: squaringConfirm
        objectName: "squaringConfirm"
        title: qsTr("Update Firmware")
        message: tool.model.updateText
        actionText: qsTr("Update Firmware")
        onAccepted: tool.model.updateFirmware()
    }
}
