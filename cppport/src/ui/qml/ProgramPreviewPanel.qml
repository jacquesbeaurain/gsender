import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A tool's generated program: the Visualizer Preview and G-Code tabs (the
// second once there is a program, with its line count).
Rectangle {
    id: panel

    property string program
    property int lines: 0
    property string name  // the objectNames' prefix
    property string emptyText: ""
    property int tab: 0

    radius: Theme.radiusSmall
    color: "transparent"
    border.color: Theme.dark ? Theme.outline : Theme.gray[200]

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: [qsTr("Visualizer Preview"), panel.lines > 0 ? qsTr("G-Code (%1 lines)").arg(panel.lines) : qsTr("G-Code")]
                Rectangle {
                    required property string modelData
                    required property int index
                    objectName: panel.name + "Tab_" + index
                    Layout.fillWidth: true
                    height: Theme.touchTarget
                    enabled: index === 0 || panel.lines > 0
                    color: panel.tab === index ? (Theme.dark ? Theme.surfaceRaised : "white") : (Theme.dark ? Theme.surfaceBase : Theme.gray[100])
                    Label {
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: Theme.fontSm
                        color: parent.enabled ? Theme.contentPrimary : Theme.contentDisabled
                    }
                    TapHandler { onTapped: panel.tab = index }
                }
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: panel.tab
            Item {
                clip: true
                ProgramPreviewItem {
                    id: preview
                    objectName: panel.name + "Preview"
                    anchors.fill: parent
                    program: panel.program
                }
                ToolpathGestures { anchors.fill: parent; view: preview }
                Label {
                    anchors.centerIn: parent
                    visible: preview.empty && panel.emptyText !== ""
                    horizontalAlignment: Text.AlignHCenter
                    text: panel.emptyText
                    color: Theme.contentMuted
                }
            }
            ScrollView {
                TextArea {
                    objectName: panel.name + "Gcode"
                    readOnly: true
                    text: panel.program
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.fontXs
                    color: Theme.contentPrimary
                }
            }
        }
    }
}
