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
        { code: "G54", label: "G54 (P1)", color: Theme.tw.blue[600] },
        { code: "G55", label: "G55 (P2)", color: Theme.tw.emerald[600] },
        { code: "G56", label: "G56 (P3)", color: Theme.tw.amber[600] },
        { code: "G57", label: "G57 (P4)", color: Theme.tw.violet[600] },
        { code: "G58", label: "G58 (P5)", color: Theme.tw.rose[600] },
        { code: "G59", label: "G59 (P6)", color: Theme.tw.cyan[600] }
    ]
    readonly property int current: Math.max(0, workspaces.findIndex(w => w.code === model.workspace))

    spacing: 8

    Label {
        text: qsTr("Workspace:")
        color: Theme.gray[300]
        font.pixelSize: Theme.fontBase
    }
    ComboBox {
        id: combo
        objectName: "workspaceCombo"
        property string tooltip: qsTr("Select a workspace")
        property string tooltipSide: "left"
        readonly property bool clickable: true
        enabled: selector.model.workspaceEnabled
        model: selector.workspaces
        textRole: "label"
        currentIndex: selector.current
        implicitHeight: 30
        implicitWidth: 85
        // Upstream's select: a small chevron at the right.
        indicator: Icon {
            x: combo.width - width - 4
            y: (combo.height - height) / 2
            name: "MdKeyboardArrowDown"
            size: 18
            color: Theme.dark ? Theme.gray[300] : Theme.gray[600]
        }
        onActivated: (index) => selector.model.selectWorkspace(selector.workspaces[index].code)
        contentItem: Label {
            leftPadding: 8
            text: selector.workspaces[selector.current].label
            color: Theme.dark ? Qt.lighter(selector.workspaces[selector.current].color, 1.4)
                              : selector.workspaces[selector.current].color
            font.bold: true
            font.pixelSize: Theme.fontSm
            verticalAlignment: Text.AlignVCenter
        }
        background: Panel {
            radius: 4
            color: !combo.enabled ? (Theme.dark ? Theme.surfaceDisabled : Theme.gray[300])
                 : Theme.surfaceRaised
            border.color: Theme.dark ? selector.workspaces[selector.current].color : Theme.gray[300]
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
