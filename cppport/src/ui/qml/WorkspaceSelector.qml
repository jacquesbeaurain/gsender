import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// "Workspace:" and the work coordinate systems (features/WorkspaceSelector)
// over the visualizer's top right, each in its accent colour (upstream's
// -600 shades, shared with its pendant); off while a job runs.
RowLayout {
    id: selector
    objectName: "workspaceSelector"

    property DroModel model: DroModel {}
    readonly property var workspaces: [
        { code: "G54", label: "G54 (P1)", color: "#2563eb" },
        { code: "G55", label: "G55 (P2)", color: "#059669" },
        { code: "G56", label: "G56 (P3)", color: "#d97706" },
        { code: "G57", label: "G57 (P4)", color: "#7c3aed" },
        { code: "G58", label: "G58 (P5)", color: "#e11d48" },
        { code: "G59", label: "G59 (P6)", color: "#0891b2" }
    ]
    readonly property int current: Math.max(0, workspaces.findIndex(w => w.code === model.workspace))

    spacing: 8

    Label {
        text: qsTr("Workspace:")
        color: "#d1d5db"
        font.pixelSize: Theme.fontSm
    }
    ComboBox {
        id: combo
        objectName: "workspaceCombo"
        enabled: selector.model.workspaceEnabled
        model: selector.workspaces
        textRole: "label"
        currentIndex: selector.current
        implicitHeight: Theme.touchTarget - 4
        implicitWidth: 130
        onActivated: (index) => selector.model.selectWorkspace(selector.workspaces[index].code)
        contentItem: Label {
            leftPadding: 10
            text: selector.workspaces[selector.current].label
            color: selector.workspaces[selector.current].color
            font.bold: true
            font.pixelSize: Theme.fontSm
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 4
            color: combo.enabled ? "white" : Theme.gray[300]
            border.color: Theme.gray[300]
        }
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: combo.width
            height: Theme.touchTarget
            contentItem: Label {
                text: parent.modelData.label
                color: parent.modelData.color
                font.bold: true
                font.pixelSize: Theme.fontSm
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
