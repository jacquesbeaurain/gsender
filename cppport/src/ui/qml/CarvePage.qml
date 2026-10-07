import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The Carve page (workspace/Carve): the visualizer beside the location
// column (DRO and jogging) over three quarters of the height; the file, job
// and tool widgets below. Portrait stacks them: the visualizer over the tool
// area, the column beside it.
Item {
    id: page
    objectName: "carvePage"

    readonly property bool portrait: height > width
    // Upstream's max-xl: below 1280 px the upper part takes 76% of the height
    // (75% above), the widgets scale down and the top bar is lower.
    readonly property bool compact: Window.window && Window.window.width <= 1280

    StepThrough { id: stepThrough }

    // The keyboard shortcuts for this page's screens.
    Connections {
        target: Backend
        function onShortcutTriggered(id) {
            switch (id) {
            case "LOAD_FILE":
                if (fileControl.model.canLoad)
                    fileControl.load()
                break
            case "OPEN_PROBE": {
                const probe = tools.tabItem("probe")
                tools.select("probe", null)
                if (probe)
                    probe.openRun()
                break
            }
            case "PROBE_ROUTINE_SCROLL_RIGHT":
            case "PROBE_ROUTINE_SCROLL_LEFT": {
                const probe = tools.tabItem("probe")
                if (probe)
                    probe.model.stepCommand(id.endsWith("RIGHT") ? 1 : -1)
                break
            }
            case "SWITCH_WORKSPACE_MODE": {
                const rotary = tools.tabItem("rotary")
                if (rotary)
                    rotary.toggleMode()
                break
            }
            case "TOGGLE_MOUNTING_SETUP": {
                const rotary = tools.tabItem("rotary")
                if (rotary)
                    rotary.openMounting()
                break
            }
            case "TOGGLE_ROTARY_SURFACING":
                Backend.openTool("rotarySurfacing")
                break
            }
        }
    }

    GridLayout {
        id: grid
        anchors.fill: parent
        anchors.margins: 2
        columns: page.portrait ? 1 : 2
        // Upstream's sections are inset by 2 px (p-0.5); the file and job
        // buttons (24 px) stand on the cards' top edge, in the bottom row.
        rowSpacing: 4
        columnSpacing: 4

        Visualizer {
            objectName: "carveVisualizer"
            // The editor takes the visualizer's place while open.
            GcodeEditor {
                id: gcodeEditor
                anchors.fill: parent
                z: 5
            }
            ProgressArea {
                model: jobControl.model
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 72
            }
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: page.portrait ? page.height * 0.45
                                                  : page.height * (page.compact ? 0.76 : 0.75) - 28
        }
        LocationColumn {
            visible: !page.portrait
            Layout.preferredWidth: Math.min(page.width * 0.33, 448) - 4
            Layout.minimumWidth: 360
            Layout.fillHeight: true
        }
        RowLayout {
            id: bottomRow
            Layout.columnSpan: page.portrait ? 1 : 2
            Layout.fillWidth: true
            // The button groups stand 34 px above the cards, over the visualizer's
            // lower edge (upstream's -mt).
            Layout.topMargin: page.portrait ? 0 : -10
            Layout.preferredHeight: page.portrait ? page.height * 0.55
                                                  : page.height * (page.compact ? 0.24 : 0.25) + 30
            spacing: 4
            GridLayout {
                id: widgets
                columns: page.portrait ? 1 : 3
                Layout.fillWidth: true
                Layout.fillHeight: true
                rowSpacing: 4
                columnSpacing: 4
                FileControl {
                    id: fileControl
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    onOpenStepThrough: stepThrough.openFile()
                    onOpenEditor: gcodeEditor.visible ? gcodeEditor.close() : gcodeEditor.openFile()
                }
                JobControl {
                    id: jobControl
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                ToolsWidget {
                    id: tools
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    tabs: [
                        ToolTab { key: "probe"; label: qsTr("Probe"); component: ProbeTab {} },
                        ToolTab { key: "macros"; label: qsTr("Macros"); component: MacrosTab {} },
                        ToolTab {
                            key: "spindle"; label: qsTr("Spindle/Laser"); shown: Backend.spindleFunctions
                            component: SpindleTab {}
                        },
                        ToolTab {
                            key: "coolant"; label: qsTr("Coolant"); shown: Backend.coolantFunctions
                            component: CoolantTab {}
                        },
                        ToolTab { key: "rotary"; label: qsTr("Rotary"); shown: Backend.rotaryTab; component: RotaryTab {} },
                        ToolTab { key: "console"; label: qsTr("Console"); component: ConsoleTab {} }
                    ]
                }
            }
            LocationColumn {
                objectName: "locationColumnPortrait"
                visible: page.portrait
                Layout.preferredWidth: Math.max(page.width / 3, 400)
                Layout.fillHeight: true
            }
        }
    }

    // Outline and Start From over the job card's middle (upstream's
    // top-[-80px]), hidden while they cannot run.
    RowLayout {
        id: jobActions
        z: 2
        visible: jobControl.model.canPrepare
        spacing: 8
        x: grid.x + bottomRow.x + widgets.x + jobControl.x + (jobControl.width - width) / 2
        y: grid.y + bottomRow.y + widgets.y + jobControl.y - height - 8
        GButton {
            objectName: "outlineJob"
            iconName: "TbVector"
            text: qsTr("Outline")
            onClicked: jobControl.runOutline()
        }
        GButton {
            objectName: "startFromLine"
            iconName: "MdFormatListNumbered"
            text: qsTr("Start From")
            onClicked: jobControl.openStartFromLine()
        }
    }
}
