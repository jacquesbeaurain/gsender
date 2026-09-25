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

    // One point: orbit (Shift: pan).
    DragHandler {
        id: orbitDrag
        target: null
        acceptedButtons: Qt.LeftButton
        maximumPointCount: 1
        property point last
        onActiveChanged: last = centroid.position
        onCentroidChanged: {
            if (!active)
                return
            const p = centroid.position
            if (centroid.modifiers & Qt.ShiftModifier)
                toolpath.pan(p.x - last.x, p.y - last.y)
            else
                toolpath.orbit((p.x - last.x) * 0.5, -(p.y - last.y) * 0.5)
            last = p
        }
    }
    // The other buttons: pan.
    DragHandler {
        target: null
        acceptedButtons: Qt.RightButton | Qt.MiddleButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        property point last
        onActiveChanged: last = centroid.position
        onCentroidChanged: {
            if (!active)
                return
            const p = centroid.position
            toolpath.pan(p.x - last.x, p.y - last.y)
            last = p
        }
    }
    // Two fingers: pinch to zoom about them, move to pan.
    PinchHandler {
        target: null
        minimumPointCount: 2
        maximumPointCount: 2
        property real lastScale: 1
        property point last
        onActiveChanged: {
            lastScale = 1
            last = centroid.position
        }
        onActiveScaleChanged: {
            if (!active || lastScale <= 0)
                return
            toolpath.zoomAt(centroid.position.x, centroid.position.y, activeScale / lastScale)
            lastScale = activeScale
        }
        onCentroidChanged: {
            if (!active)
                return
            const p = centroid.position
            toolpath.pan(p.x - last.x, p.y - last.y)
            last = p
        }
    }
    WheelHandler {
        target: null
        onWheel: (event) => toolpath.zoomAt(point.position.x, point.position.y, Math.pow(1.0015, event.angleDelta.y))
    }
    TapHandler {
        onDoubleTapped: toolpath.fit()
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
