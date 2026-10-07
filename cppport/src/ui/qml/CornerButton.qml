import QtQuick
import QtQuick.Controls.Basic
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
    // Upstream's tooltips, the front corners' below them.
    readonly property string tooltip: tips[corner]
    readonly property string tooltipSide: atBack ? "top" : "bottom"
    property bool available: true
    signal clicked()

    readonly property bool atLeft: corner === 0 || corner === 2
    readonly property bool atBack: corner === 0 || corner === 1
    readonly property color ink: available ? Theme.robin[500] : Theme.gray[400]

    // The grid's cell (32 x 28).
    readonly property real cellW: 32
    readonly property real cellH: 28
    readonly property var tips: [qsTr("Go to Back Left Corner"), qsTr("Go to Back Right Corner"),
                                 qsTr("Go to Front Left Corner"), qsTr("Go to Front Right Corner")]
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

    // The L as upstream's SVG draws it: a 20-unit stroke (butt ends, a mitre
    // corner) in the corner's own viewBox, fitted into the cell (the default
    // xMidYMid meet) and clipped to it. These rectangles are the stroke's
    // outline in viewBox units; the back corners' ends stop short of the
    // cell's bottom, which parts the back from the front.
    readonly property var svg: [
        { vw: 37, vh: 34, rects: [[-10, -10, 42, 20], [-10, -10, 20, 42]] },  // M 32 0 H 0 V 32
        { vw: 27, vh: 34, rects: [[0, -10, 42, 20], [22, -10, 20, 42]] },     // M 32 32 V 0 L 0 0
        { vw: 37, vh: 33, rects: [[-10, 0, 20, 42], [-10, 22, 42, 20]] },     // M 0 0 L 0 32 L 32 32
        { vw: 27, vh: 33, rects: [[0, 22, 42, 20], [22, 0, 20, 42]] }         // M 0 32 H 32 V 0
    ][corner]
    function bar(r) {
        const k = Math.min(cellW / svg.vw, cellH / svg.vh)
        const offX = (cellW - svg.vw * k) / 2, offY = (cellH - svg.vh * k) / 2
        const x0 = Math.max(0, offX + r[0] * k), x1 = Math.min(cellW, offX + (r[0] + r[2]) * k)
        const y0 = Math.max(0, offY + r[1] * k), y1 = Math.min(cellH, offY + (r[1] + r[3]) * k)
        return [project(ox + x0, oy + y0), project(ox + x1, oy + y0),
                project(ox + x1, oy + y1), project(ox + x0, oy + y1)]
            .map(p => Qt.point(p.x - button.minX, p.y - button.minY))
    }

    Repeater {
        model: button.svg.rects.map(r => button.bar(r))
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
        enabled: button.available
        onTapped: button.clicked()
    }
}
