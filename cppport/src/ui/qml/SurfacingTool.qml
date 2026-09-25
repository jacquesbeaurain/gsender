import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Wasteboard Surfacing (features/Surfacing): the stock's X & Y, cut depth
// and max, bit and tool, stepover, feed, spindle, coolant, where it starts
// and its pattern; beside, the preview and the G-code; Generate G-code and
// Load to Main Visualizer.
ToolPage {
    id: tool
    objectName: "surfacingTool"
    title: qsTr("Wasteboard Surfacing")

    property SurfacingModel model: SurfacingModel { objectName: "surfacing" }
    readonly property var o: model.options
    readonly property var d: model.defaults
    readonly property string units: model.units

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // The form.
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

                    // Where it starts, and the pattern.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 32
                        Item {
                            objectName: "surfacingStart"
                            Layout.preferredWidth: 104
                            Layout.preferredHeight: 104
                            Rectangle {
                                anchors.centerIn: parent
                                width: 64; height: 64
                                color: "transparent"
                                border.width: 4
                                border.color: Theme.contentPrimary
                            }
                            Repeater {
                                model: [
                                    { key: "backLeft", x: 0, y: 0, tip: qsTr("Start at the Back Left") },
                                    { key: "backRight", x: 72, y: 0, tip: qsTr("Start at the Back Right") },
                                    { key: "frontLeft", x: 0, y: 72, tip: qsTr("Start at the Front Left") },
                                    { key: "frontRight", x: 72, y: 72, tip: qsTr("Start at the Front Right") },
                                    { key: "center", x: 36, y: 36, tip: qsTr("Start at the Center") }
                                ]
                                Rectangle {
                                    required property var modelData
                                    objectName: "surfacingStart_" + modelData.key
                                    x: modelData.x; y: modelData.y
                                    width: 32; height: 32; radius: 16
                                    color: Theme.dark ? Theme.surfaceRaised : "white"
                                    border.width: 2
                                    border.color: Theme.blue[500]
                                    Rectangle {
                                        anchors.centerIn: parent
                                        visible: tool.o.startPosition === modelData.key
                                        width: 16; height: 16; radius: 8
                                        color: Theme.blue[500]
                                    }
                                    TapHandler { onTapped: tool.model.setOption("startPosition", modelData.key) }
                                }
                            }
                        }
                        ColumnLayout {
                            spacing: 8
                            RowLayout {
                                spacing: 16
                                Repeater {
                                    model: [
                                        { key: "spiral", label: qsTr("Spiral") },
                                        { key: "zigzag", label: qsTr("Zig-Zag") }
                                    ]
                                    Rectangle {
                                        required property var modelData
                                        readonly property bool chosen: tool.o.pattern === modelData.key
                                        objectName: "surfacingPattern_" + modelData.key
                                        width: 64; height: 64; radius: 8
                                        color: chosen ? "#eff6ff" : "transparent"
                                        border.width: 2
                                        border.color: chosen ? Theme.blue[500] : Theme.contentPrimary
                                        Icon {
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            name: modelData.key === "zigzag" ? "SurfacingZigZag" : "SurfacingSpiral"
                                            color: Theme.dark && !parent.chosen ? Theme.contentPrimary : "black"
                                        }
                                        TapHandler { onTapped: tool.model.setOption("pattern", modelData.key) }
                                    }
                                }
                            }
                            ToolToggle { model: tool.model; key: "cutDirectionFlipped"; label: qsTr("Flip the cut direction") }
                        }
                    }

                    ToolFormRow {
                        label: qsTr("X & Y")
                        ToolField { model: tool.model; key: "width" }
                        Label { text: "&"; color: Theme.contentMuted }
                        ToolField { model: tool.model; key: "length" }
                        Label { text: tool.units; color: Theme.contentMuted }
                    }
                    ToolFormRow {
                        label: qsTr("Cut Depth & Max")
                        ToolField { model: tool.model; key: "skimDepth" }
                        Label { text: "&"; color: Theme.contentMuted }
                        ToolField { model: tool.model; key: "maxDepth" }
                        Label { text: tool.units; color: Theme.contentMuted }
                    }
                    Label {
                        objectName: "surfacingDepthWarning"
                        visible: tool.model.depthWarning !== ""
                        Layout.fillWidth: true
                        text: tool.model.depthWarning
                        color: Theme.red[500]
                        font.pixelSize: Theme.fontSm
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
                        ComboBox {
                            objectName: "surfacing_spindle"
                            model: ["M3", "M4"]
                            currentIndex: tool.o.spindle === "M4" ? 1 : 0
                            onActivated: (index) => tool.model.setOption("spindle", index === 1 ? "M4" : "M3")
                            Layout.preferredWidth: 90
                        }
                        ToolToggle { model: tool.model; key: "shouldDwell"; label: qsTr("Delay") }
                    }
                    ToolFormRow {
                        label: qsTr("Coolant Control")
                        ToolToggle { model: tool.model; key: "mist"; label: qsTr("Mist") }
                        ToolToggle { model: tool.model; key: "flood"; label: qsTr("Flood") }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // The preview and the G-code.
            ProgramPreviewPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                name: "surfacing"
                program: tool.model.program
                lines: tool.model.lines
            }
        }
        RowLayout {
            spacing: 16
            GButton {
                objectName: "surfacingGenerate"
                text: qsTr("Generate G-code")
                enabled: tool.model.free
                onClicked: tool.model.generate()
            }
            GButton {
                objectName: "surfacingLoad"
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
