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

    // A widget not ported to the touch UI yet.
    component Pending: Card {
        id: pending
        property string title
        property string note
        Column {
            anchors.centerIn: parent
            spacing: 4
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: pending.title
                font.pixelSize: Theme.fontLg
                font.bold: true
                color: Theme.contentSecondary
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: pending.note
                font.pixelSize: Theme.fontSm
                color: Theme.contentMuted
            }
        }
    }

    StepThrough { id: stepThrough }

    GridLayout {
        id: grid
        anchors.fill: parent
        anchors.margins: 4
        columns: page.portrait ? 1 : 2
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
            Layout.preferredHeight: page.portrait ? page.height * 0.45 : page.height * 0.75
        }
        LocationColumn {
            visible: !page.portrait
            Layout.preferredWidth: Math.min(page.width * 0.33, 448)
            Layout.minimumWidth: 400
            Layout.fillHeight: true
        }
        RowLayout {
            id: bottomRow
            Layout.columnSpan: page.portrait ? 1 : 2
            Layout.fillWidth: true
            Layout.preferredHeight: page.portrait ? page.height * 0.55 : Math.max(192, page.height * 0.25)
            spacing: 4
            GridLayout {
                id: widgets
                columns: page.portrait ? 1 : 3
                Layout.fillWidth: true
                Layout.fillHeight: true
                rowSpacing: 4
                columnSpacing: 4
                FileControl {
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
