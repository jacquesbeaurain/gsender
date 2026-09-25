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

    // The visualizer's keyboard shortcuts.
    Connections {
        target: Backend
        function onShortcutTriggered(id) {
            const views = { VISUALIZER_VIEW_3D: "3d", VISUALIZER_VIEW_TOP: "top", VISUALIZER_VIEW_FRONT: "front",
                            VISUALIZER_VIEW_RIGHT: "right", VISUALIZER_VIEW_LEFT: "left", VISUALIZER_VIEW_RESET: "3d" }
            if (views[id])
                toolpath.setView(views[id])
            else if (id === "VISUALIZER_VIEW_CYCLE")
                toolpath.cycleView()
            else if (id === "VISUALIZER_ZOOM_IN")
                toolpath.zoomAt(toolpath.width / 2, toolpath.height / 2, 1.25)
            else if (id === "VISUALIZER_ZOOM_OUT")
                toolpath.zoomAt(toolpath.width / 2, toolpath.height / 2, 0.8)
            else if (id === "VISUALIZER_ZOOM_FIT")
                toolpath.fit()
        }
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
