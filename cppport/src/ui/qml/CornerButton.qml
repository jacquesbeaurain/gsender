import QtQuick
import QtQuick.Shapes
import GSender

// A corner button (RapidPositionButtons): a thick L pointing into the
// corner - 0 back left, 1 back right, 2 front left, 3 front right - in
// robin-500, grey when disabled.
//
// The four sit in a 64 x 56 grid (w-16 h-14), drawn as upstream draws it:
// scale(1.4) translateX(-25px) perspective(160px) rotateX(55deg) about the
// grid's centre. The shapes' corners are projected here (a 3D transform would
// not render on the software renderer, and the hit area is the projected
// cell), so the button is placed at - and sized to - its projected cell,
// relative to the grid's top left.
Item {
    id: button

    property int corner: 0
    signal clicked()

    readonly property bool atLeft: corner === 0 || corner === 2
    readonly property bool atBack: corner === 0 || corner === 1
    readonly property color ink: enabled ? Theme.robin[500] : Theme.gray[400]

    // The grid and a cell (32 x 28), the stroke's thickness and the gap the
    // strokes leave between the two columns.
    readonly property real cellW: 32
    readonly property real cellH: 28
    readonly property real stroke: 8.2
    readonly property real gap: 5.6
    readonly property real ox: atLeft ? 0 : cellW
    readonly property real oy: atBack ? 0 : cellH

    // The CSS transform of the grid, for a point of the grid (origin top left).
    function project(px, py) {
        const cx = cellW, cy = cellH
        const x0 = px - cx, y0 = py - cy
        const angle = 55 * Math.PI / 180
        const y1 = y0 * Math.cos(angle)
        const z1 = y0 * Math.sin(angle)
        const w = 1 - z1 / 160
        return Qt.point(((x0 / w) - 25) * 1.4 + cx, (y1 / w) * 1.4 + cy)
    }

    // The cell's projected outline: this item's place and size.
    readonly property var cellPoints: [project(ox, oy), project(ox + cellW, oy),
                                       project(ox + cellW, oy + cellH), project(ox, oy + cellH)]
    readonly property real minX: Math.min(cellPoints[0].x, cellPoints[3].x, cellPoints[1].x, cellPoints[2].x)
    readonly property real minY: Math.min(cellPoints[0].y, cellPoints[1].y)
    readonly property real maxX: Math.max(cellPoints[0].x, cellPoints[3].x, cellPoints[1].x, cellPoints[2].x)
    readonly property real maxY: Math.max(cellPoints[2].y, cellPoints[3].y)

    x: minX
    y: minY
    width: maxX - minX
    height: maxY - minY

    // The L: a bar along the back or front edge and one up the outer edge,
    // as rectangles of the cell, projected.
    function rectPoints(x, y, w, h) {
        return [project(ox + x, oy + y), project(ox + x + w, oy + y),
                project(ox + x + w, oy + y + h), project(ox + x, oy + y + h)]
            .map(p => Qt.point(p.x - button.minX, p.y - button.minY))
    }
    readonly property var edgeBar: rectPoints(atLeft ? 0 : gap, atBack ? 0 : cellH - stroke, cellW - gap, stroke)
    readonly property var sideBar: rectPoints(atLeft ? 0 : cellW - stroke, 0, stroke, cellH)

    Repeater {
        model: [button.edgeBar, button.sideBar]
        Shape {
            required property var modelData
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: -1
                fillColor: button.ink
                PathPolyline { path: [...modelData, modelData[0]] }
            }
        }
    }
    TapHandler {
        enabled: button.enabled
        onTapped: button.clicked()
    }
}
