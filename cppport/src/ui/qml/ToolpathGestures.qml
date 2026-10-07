import QtQuick
import QtQuick.Shapes
import GSender

// A toolpath view's touch and mouse: one finger (or the left button)
// orbits, two fingers (or the right/middle button, or Shift+drag) pan, a
// pinch or the wheel zooms about the fingers or cursor, a double tap fits.
Item {
    id: gestures

    property ToolpathItem view

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
                gestures.view.pan(p.x - last.x, p.y - last.y)
            else
                gestures.view.orbit((p.x - last.x) * 0.5, -(p.y - last.y) * 0.5)
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
            gestures.view.pan(p.x - last.x, p.y - last.y)
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
            gestures.view.zoomAt(centroid.position.x, centroid.position.y, activeScale / lastScale)
            lastScale = activeScale
        }
        onCentroidChanged: {
            if (!active)
                return
            const p = centroid.position
            gestures.view.pan(p.x - last.x, p.y - last.y)
            last = p
        }
    }
    WheelHandler {
        target: null
        onWheel: (event) => gestures.view.zoomAt(point.position.x, point.position.y, Math.pow(1.0015, event.angleDelta.y))
    }
    TapHandler {
        onDoubleTapped: gestures.view.fit()
        // A plugin's click pick: a clean tap (a drag orbits or pans instead).
        onSingleTapped: (eventPoint) => {
            if (gestures.view.pickMode === "click")
                gestures.view.pickAt(eventPoint.position.x, eventPoint.position.y)
        }
    }
    // A plugin's hold pick: press still for half a second; moving cancels it.
    TapHandler {
        id: holdPick
        enabled: gestures.view.pickMode === "hold"
        acceptedButtons: Qt.LeftButton
        longPressThreshold: 0.5
        onTimeHeldChanged: {
            if (pressed)
                gestures.view.pickHoldProgress(Math.min(1, timeHeld / longPressThreshold))
        }
        onPressedChanged: {
            if (!pressed)
                gestures.view.pickHoldProgress(0)
        }
        onLongPressed: gestures.view.pickAt(point.position.x, point.position.y)
    }
    // The crosshair cursor while a pick is armed.
    HoverHandler {
        enabled: gestures.view.pickMode !== ""
        cursorShape: Qt.CrossCursor
    }

    // The hold's ring under the pointer (GcodeViewer's pick indicator): a
    // 48 px dark disc with a green arc that fills over the half second.
    Item {
        id: ring
        objectName: "holdRing"
        readonly property real progress: holdPick.pressed ? Math.min(1, holdPick.timeHeld / holdPick.longPressThreshold) : 0
        visible: holdPick.enabled && holdPick.pressed
        x: holdPick.point.position.x - 24
        y: holdPick.point.position.y - 24
        width: 48
        height: 48
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2.5
            radius: width / 2
            color: Qt.rgba(0, 0, 0, 0.35)
            border.color: Qt.rgba(1, 1, 1, 0.4)
            border.width: 3
        }
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Theme.tw.green[400]
                strokeWidth: 3
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc {
                    centerX: 24
                    centerY: 24
                    radiusX: 20
                    radiusY: 20
                    startAngle: -90
                    sweepAngle: 360 * ring.progress
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 4
            height: 4
            radius: 2
            color: Theme.tw.green[400]
        }
    }
}
