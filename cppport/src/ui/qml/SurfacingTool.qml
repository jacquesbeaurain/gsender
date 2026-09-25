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

    property SurfacingModel model: SurfacingModel {}
    readonly property var o: model.options
    readonly property var d: model.defaults
    readonly property string units: model.units

    // InputArea: the label, then its inputs.
    component FormRow: RowLayout {
        property string label
        default property alias inputs: box.data
        Layout.fillWidth: true
        spacing: 12
        Label {
            text: parent.label
            font.pixelSize: Theme.fontSm
            color: Theme.contentPrimary
            wrapMode: Text.Wrap
            Layout.preferredWidth: 150
        }
        RowLayout { id: box; Layout.fillWidth: true; spacing: 8 }
    }
    component Field: NumberField {
        property string key
        Layout.fillWidth: true
        value: tool.o[key] !== undefined ? tool.o[key] : 0
        horizontalAlignment: TextInput.AlignHCenter
        color: Theme.blue[500]
        font.pixelSize: Theme.fontLg
        objectName: "surfacing_" + key
        onCommitted: (text) => { if (text !== "" && !isNaN(Number(text))) tool.model.setOption(key, Number(text)) }
    }
    component Toggle: RowLayout {
        property string key
        property string label
        spacing: 6
        Label { text: parent.label; font.pixelSize: Theme.fontSm; color: Theme.contentPrimary }
        GSwitch {
            objectName: "surfacing_" + parent.key
            checked: !!tool.o[parent.key]
            onToggled: tool.model.setOption(parent.key, checked)
        }
    }

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
                            Toggle { key: "cutDirectionFlipped"; label: qsTr("Flip the cut direction") }
                        }
                    }

                    FormRow {
                        label: qsTr("X & Y")
                        Field { key: "width" }
                        Label { text: "&"; color: Theme.contentMuted }
                        Field { key: "length" }
                        Label { text: tool.units; color: Theme.contentMuted }
                    }
                    FormRow {
                        label: qsTr("Cut Depth & Max")
                        Field { key: "skimDepth" }
                        Label { text: "&"; color: Theme.contentMuted }
                        Field { key: "maxDepth" }
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
                    FormRow {
                        label: qsTr("Bit Diameter & Tool Number (optional)")
                        Field { key: "bitDiameter" }
                        Label { text: "&"; color: Theme.contentMuted }
                        Field { key: "toolNumber"; decimals: 0 }
                    }
                    FormRow {
                        label: qsTr("Stepover")
                        Field { key: "stepover"; decimals: 0; suffix: "%" }
                    }
                    FormRow {
                        label: qsTr("Feed Rate")
                        Field { key: "feedrate"; suffix: tool.units + "/min" }
                    }
                    FormRow {
                        label: qsTr("Spindle RPM")
                        Field { key: "spindleRPM"; decimals: 0 }
                        ComboBox {
                            objectName: "surfacing_spindle"
                            model: ["M3", "M4"]
                            currentIndex: tool.o.spindle === "M4" ? 1 : 0
                            onActivated: (index) => tool.model.setOption("spindle", index === 1 ? "M4" : "M3")
                            Layout.preferredWidth: 90
                        }
                        Toggle { key: "shouldDwell"; label: qsTr("Delay") }
                    }
                    FormRow {
                        label: qsTr("Coolant Control")
                        Toggle { key: "mist"; label: qsTr("Mist") }
                        Toggle { key: "flood"; label: qsTr("Flood") }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // The preview and the G-code.
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                radius: Theme.radiusSmall
                color: "transparent"
                border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    property int tab: 0
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Repeater {
                            model: [qsTr("Visualizer Preview"), tool.model.lines > 0 ? qsTr("G-Code (%1 lines)").arg(tool.model.lines) : qsTr("G-Code")]
                            Rectangle {
                                required property string modelData
                                required property int index
                                objectName: "surfacingTab_" + index
                                Layout.fillWidth: true
                                height: Theme.touchTarget
                                enabled: index === 0 || tool.model.lines > 0
                                color: parent.parent.tab === index ? (Theme.dark ? Theme.surfaceRaised : "white") : (Theme.dark ? Theme.surfaceBase : Theme.gray[100])
                                Label {
                                    anchors.centerIn: parent
                                    text: modelData
                                    font.pixelSize: Theme.fontSm
                                    color: parent.enabled ? Theme.contentPrimary : Theme.contentDisabled
                                }
                                TapHandler { onTapped: parent.parent.parent.tab = index }
                            }
                        }
                    }
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: parent.tab
                        Item {
                            clip: true
                            ProgramPreviewItem {
                                id: preview
                                objectName: "surfacingPreview"
                                anchors.fill: parent
                                program: tool.model.program
                            }
                            ToolpathGestures { anchors.fill: parent; view: preview }
                        }
                        ScrollView {
                            TextArea {
                                objectName: "surfacingGcode"
                                readOnly: true
                                text: tool.model.program
                                font.family: Theme.monoFont
                                font.pixelSize: Theme.fontXs
                                color: Theme.contentPrimary
                            }
                        }
                    }
                }
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
