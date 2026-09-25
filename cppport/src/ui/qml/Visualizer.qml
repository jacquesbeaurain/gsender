import QtQuick
import QtQuick.Controls.Basic
import GSender

// The main visualizer: the toolpath drawn by ToolpathItem, driven by touch
// or mouse - one finger (or the left button) orbits, two fingers (or the
// right/middle button, or Shift+drag) pan, a pinch or the wheel zooms, a
// double tap fits.
Rectangle {
    id: frame
    objectName: "visualizer"

    property alias view: toolpath

    color: "transparent"
    radius: Theme.radius
    clip: true

    ToolpathItem {
        id: toolpath
        objectName: "toolpath"
        anchors.fill: parent
    }

    ToolpathGestures {
        anchors.fill: parent
        view: toolpath
    }

    WorkspaceSelector {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
    }

    // The view presets.
    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        spacing: 4
        Repeater {
            model: [
                { label: qsTr("3D"), view: "3d" },
                { label: qsTr("Top"), view: "top" },
                { label: qsTr("Fit"), view: "" }
            ]
            Rectangle {
                required property var modelData
                objectName: "view" + modelData.label
                width: 52
                height: Theme.touchTarget
                radius: Theme.radiusSmall
                color: modelData.view !== "" && toolpath.view === modelData.view ? Theme.primary : Qt.rgba(1, 1, 1, 0.08)
                border.color: Qt.rgba(1, 1, 1, 0.2)
                Label {
                    anchors.centerIn: parent
                    text: parent.modelData.label
                    color: "white"
                    font.pixelSize: Theme.fontSm
                }
                TapHandler {
                    onTapped: parent.modelData.view !== "" ? toolpath.setView(parent.modelData.view) : toolpath.fit()
                }
            }
        }
    }
}
