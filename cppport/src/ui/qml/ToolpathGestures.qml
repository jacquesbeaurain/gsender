import QtQuick
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
    }
}
