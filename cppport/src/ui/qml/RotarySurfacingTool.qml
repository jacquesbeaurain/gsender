import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Rotary Surfacing (features/Rotary/RotarySurfacing): what to check first,
// the stock's length, start and final diameters, stepdown, bit and tool,
// stepover, feed, spindle and rehoming; beside, the preview (the toolpath
// wrapped around the rotary) and the G-code.
ToolPage {
    id: tool
    objectName: "rotarySurfacingTool"
    title: qsTr("Rotary Surfacing")

    property RotarySurfacingModel model: RotarySurfacingModel { objectName: "rotarySurfacing" }
    readonly property string units: model.units

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                contentHeight: form.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ColumnLayout {
                    id: form
                    width: parent.width
                    spacing: 10
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.contentSecondary
                        text: qsTr("Make sure that your tool clears the surface of your material without running into the limits of your Z-axis. You should also use the probing feature to zero your Z-axis to the centerline before surfacing.")
                    }
                    ToolFormRow {
                        label: qsTr("Length")
                        ToolField { model: tool.model; key: "stockLength" }
                        Label { text: tool.units; color: Theme.contentMuted }
                    }
                    ToolFormRow {
                        label: qsTr("Start & Final Diameter")
                        ToolField { model: tool.model; key: "startHeight" }
                        Label { text: "&"; color: Theme.contentMuted }
                        ToolField { model: tool.model; key: "finalHeight" }
                        Label { text: tool.units; color: Theme.contentMuted }
                    }
                    ToolFormRow {
                        label: qsTr("Stepdown")
                        ToolField { model: tool.model; key: "stepdown" }
                        Label { text: tool.units; color: Theme.contentMuted }
                    }
                    ToolFormRow {
                        label: qsTr("Bit Diameter & Tool Number (optional)")
                        ToolField { model: tool.model; key: "bitDiameter" }
                        Label { text: "&"; color: Theme.contentMuted }
                        ToolField { model: tool.model; key: "toolNumber"; decimals: 0 }
                    }
                    ToolFormRow {
                        label: qsTr("Stepover")
                        ToolField { model: tool.model; key: "stepover"; decimals: 0; suffix: "%" }
                    }
                    ToolFormRow {
                        label: qsTr("Feed Rate")
                        ToolField { model: tool.model; key: "feedrate"; suffix: tool.units + "/min" }
                    }
                    ToolFormRow {
                        label: qsTr("Spindle RPM")
                        ToolField { model: tool.model; key: "spindleRPM"; decimals: 0 }
                        ToolToggle { model: tool.model; key: "shouldDwell"; label: qsTr("Delay") }
                    }
                    ToolFormRow {
                        label: qsTr("Enable Rehoming")
                        ToolToggle { model: tool.model; key: "enableRehoming"; label: "" }
                        Item { Layout.fillWidth: true }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSm
                        color: Theme.contentMuted
                        text: qsTr("Cut faster and cleaner by only rotating one direction, but you will need to rehome your A-axis at the end.")
                    }
                }
            }
            ProgramPreviewPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                name: "rotarySurfacing"
                program: tool.model.program
                lines: tool.model.lines
                emptyText: qsTr("No g-code generated yet.\nPlease generate g-code first.")
            }
        }
        RowLayout {
            spacing: 16
            GButton {
                objectName: "rotarySurfacingGenerate"
                text: qsTr("Generate G-Code")
                enabled: tool.model.free
                onClicked: tool.model.generate()
            }
            GButton {
                objectName: "rotarySurfacingLoad"
                text: qsTr("Load to Main Visualizer")
                enabled: tool.model.free && tool.model.lines > 0
                onClicked: {
                    if (tool.model.load())
                        Backend.openPage("carve")
                }
            }
        }
    }
}
