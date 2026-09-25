import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import GSender

// XY Squaring's triangle: point 1 bottom left, 2 bottom right (the square
// corner), 3 top right; the sides once both ends are marked (the diagonal
// dashed until measured), the move being made as an arrow, and the lengths
// measured.
Item {
    id: diagram

    property SquaringModel model
    readonly property real side: Math.max(40, Math.min(width, height) - 60)
    readonly property var points: [
        Qt.point((width - side) / 2, (height + side) / 2),
        Qt.point((width + side) / 2, (height + side) / 2),
        Qt.point((width + side) / 2, (height - side) / 2)
    ]
    readonly property var sideEnds: [[0, 1], [1, 2], [0, 2]]
    readonly property color active: Theme.blue[500]
    readonly property color idle: Theme.gray[400]

    implicitWidth: 240
    implicitHeight: 240

    // The sides.
    Repeater {
        model: 3
        Shape {
            id: line
            required property int index
            readonly property var from: diagram.points[diagram.sideEnds[index][0]]
            readonly property var to: diagram.points[diagram.sideEnds[index][1]]
            readonly property bool current: diagram.model.activeSide === index
            anchors.fill: parent
            visible: diagram.sideEnds[index][1] < diagram.model.markedPoints
            ShapePath {
                strokeColor: line.current ? diagram.active : diagram.idle
                strokeWidth: line.current ? 3 : 2
                strokeStyle: line.index === 2 && diagram.model.mainStep < 2 ? ShapePath.DashLine : ShapePath.SolidLine
                fillColor: "transparent"
                startX: line.from.x; startY: line.from.y
                PathLine { x: line.to.x; y: line.to.y }
            }
        }
    }
    Repeater {
        model: 3
        Label {
            required property int index
            readonly property var from: diagram.points[diagram.sideEnds[index][0]]
            readonly property var to: diagram.points[diagram.sideEnds[index][1]]
            readonly property var offset: index === 0 ? Qt.point(0, 18) : index === 1 ? Qt.point(-40, 0) : Qt.point(-40, -10)
            objectName: "squaringSide_" + index
            visible: diagram.model.mainStep >= 2 && diagram.model.sides[index] > 0
            text: diagram.model.sides[index] + " " + diagram.model.units
            x: (from.x + to.x) / 2 + offset.x - width / 2
            y: (from.y + to.y) / 2 + offset.y - height / 2
            color: Theme.contentPrimary
            font.pixelSize: Theme.fontSm
        }
    }
    // The square corner.
    Shape {
        anchors.fill: parent
        visible: diagram.model.markedPoints >= 3
        ShapePath {
            strokeColor: diagram.idle
            strokeWidth: 1
            fillColor: "transparent"
            startX: diagram.points[1].x; startY: diagram.points[1].y - 16
            PathLine { x: diagram.points[1].x - 16; y: diagram.points[1].y - 16 }
            PathLine { x: diagram.points[1].x - 16; y: diagram.points[1].y }
        }
    }
    // The move being made: X from 1 towards 2, Y from 2 towards 3.
    Shape {
        id: arrow
        readonly property var from: diagram.points[diagram.model.moving === "X" ? 0 : 1]
        readonly property var to: diagram.points[diagram.model.moving === "X" ? 1 : 2]
        readonly property real length: Math.max(1, Math.hypot(to.x - from.x, to.y - from.y))
        readonly property real dx: (to.x - from.x) / length
        readonly property real dy: (to.y - from.y) / length
        objectName: "squaringArrow"
        anchors.fill: parent
        visible: diagram.model.moving !== ""
        ShapePath {
            strokeColor: diagram.active
            strokeWidth: 3
            strokeStyle: ShapePath.DashLine
            fillColor: "transparent"
            startX: arrow.from.x; startY: arrow.from.y
            PathLine { x: arrow.to.x - arrow.dx * 12; y: arrow.to.y - arrow.dy * 12 }
        }
        ShapePath {
            strokeColor: "transparent"
            fillColor: diagram.active
            startX: arrow.to.x; startY: arrow.to.y
            PathLine { x: arrow.to.x - arrow.dx * 14 - arrow.dy * 7; y: arrow.to.y - arrow.dy * 14 + arrow.dx * 7 }
            PathLine { x: arrow.to.x - arrow.dx * 14 + arrow.dy * 7; y: arrow.to.y - arrow.dy * 14 - arrow.dx * 7 }
            PathLine { x: arrow.to.x; y: arrow.to.y }
        }
    }
    // The points.
    Repeater {
        model: 3
        Rectangle {
            required property int index
            objectName: "squaringPoint_" + (index + 1)
            visible: index < diagram.model.markedPoints
            x: diagram.points[index].x - 13
            y: diagram.points[index].y - 13
            width: 26; height: 26; radius: 13
            color: diagram.model.activePoint === index ? diagram.active : Theme.green[600]
            Label {
                anchors.centerIn: parent
                text: parent.index + 1
                color: "white"
                font.bold: true
            }
        }
    }
}
