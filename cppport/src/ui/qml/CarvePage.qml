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

    GridLayout {
        anchors.fill: parent
        anchors.margins: 4
        columns: page.portrait ? 1 : 2
        rowSpacing: 4
        columnSpacing: 4

        Visualizer {
            objectName: "carveVisualizer"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: page.portrait ? page.height * 0.45 : page.height * 0.75
        }
        Pending {
            objectName: "locationColumn"
            visible: !page.portrait
            title: qsTr("Location & Jogging")
            note: qsTr("DRO, jog pad and presets - Phase 1")
            Layout.preferredWidth: Math.min(page.width * 0.33, 448)
            Layout.fillHeight: true
        }
        RowLayout {
            Layout.columnSpan: page.portrait ? 1 : 2
            Layout.fillWidth: true
            Layout.preferredHeight: page.portrait ? page.height * 0.55 : Math.max(192, page.height * 0.25)
            spacing: 4
            GridLayout {
                columns: page.portrait ? 1 : 3
                Layout.fillWidth: true
                Layout.fillHeight: true
                rowSpacing: 4
                columnSpacing: 4
                Pending {
                    objectName: "fileWidget"
                    title: qsTr("File")
                    note: Backend.hasProgram ? Backend.programName : qsTr("Load File - Phase 1")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                Pending {
                    objectName: "jobWidget"
                    title: qsTr("Job")
                    note: qsTr("Start, pause, stop, overrides - Phase 1")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
                Pending {
                    objectName: "toolsWidget"
                    title: qsTr("Tools")
                    note: qsTr("Probe, macros, spindle, coolant, console - Phase 2")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
            Pending {
                visible: page.portrait
                title: qsTr("Location & Jogging")
                note: qsTr("Phase 1")
                Layout.preferredWidth: Math.max(page.width / 3, 400)
                Layout.fillHeight: true
            }
        }
    }
}
