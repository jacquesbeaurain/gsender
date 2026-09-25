import QtQuick
import QtQuick.Shapes
import GSender

// The XY jog wheel (Jogging/components/JogWheel): upstream's eight sectors -
// the axes in blue, the diagonals in robin, a little shorter - with its
// arrow labels, and the stop button in the middle. Tap a sector to step,
// hold it to jog until released.
Item {
    id: wheel
    objectName: "jogWheel"

    property JogModel model

    // Each sector: its SVG path (viewBox 200 x 200) and X/Y directions.
    readonly property var sectors: [
        { name: "xPlus", x: 1, y: 0, axis: true, path: "M192.388 61.7317C197.413 73.8642 200 86.8678 200 100C200 113.132 197.413 126.136 192.388 138.268L100 100L192.388 61.7317Z" },
        { name: "xPlusYPlus", x: 1, y: 1, axis: false, path: "M140.859 19.8094C157.794 28.438 171.562 42.2063 180.191 59.1409L100 100L140.859 19.8094Z" },
        { name: "yPlus", x: 0, y: 1, axis: true, path: "M61.7316 7.61205C73.8642 2.58658 86.8678 -1.566e-07 100 0C113.132 1.566e-07 126.136 2.58658 138.268 7.61205L100 100L61.7316 7.61205Z" },
        { name: "xMinusYPlus", x: -1, y: 1, axis: false, path: "M19.8094 59.1409C28.438 42.2063 42.2063 28.438 59.1408 19.8094L100 100L19.8094 59.1409Z" },
        { name: "xMinus", x: -1, y: 0, axis: true, path: "M7.61205 138.268C2.58658 126.136 -1.14805e-06 113.132 0 100C1.14805e-06 86.8678 2.58658 73.8642 7.61206 61.7316L100 100L7.61205 138.268Z" },
        { name: "xMinusYMinus", x: -1, y: -1, axis: false, path: "M59.1408 180.191C42.2063 171.562 28.438 157.794 19.8094 140.859L100 100L59.1408 180.191Z" },
        { name: "yMinus", x: 0, y: -1, axis: true, path: "M138.268 192.388C126.136 197.413 113.132 200 100 200C86.8678 200 73.8642 197.413 61.7316 192.388L100 100L138.268 192.388Z" },
        { name: "xPlusYMinus", x: 1, y: -1, axis: false, path: "M180.191 140.859C171.562 157.794 157.794 171.562 140.859 180.191L100 100L180.191 140.859Z" }
    ]
    property int pressedSector: -1

    // The sector under a point of the wheel (-1: none, or the stop button).
    function sectorAt(px, py) {
        const dx = px / width * 200 - 100
        const dy = 100 - py / height * 200
        const r = Math.sqrt(dx * dx + dy * dy)
        if (r < 40 || r > 100)
            return -1
        const angle = (Math.atan2(dy, dx) * 180 / Math.PI + 360 + 22.5) % 360
        const index = Math.floor(angle / 45)
        if (!sectors[index].axis && r > 90)
            return -1
        return index
    }
    function sectorEnabled(index) {
        // The rotary is Y in rotary mode: only X jogs.
        return model.canJog && (!model.rotaryMode || sectors[index].y === 0)
    }

    implicitWidth: 180
    implicitHeight: 180

    Repeater {
        model: wheel.sectors
        Shape {
            required property var modelData
            required property int index
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: -1
                fillColor: !wheel.sectorEnabled(index) ? Theme.gray[400]
                         : wheel.pressedSector === index ? (modelData.axis ? Theme.blue[700] : "#3c74a9")
                         : modelData.axis ? Theme.blue[500] : "#689AC9"
                scale: Qt.size(wheel.width / 200, wheel.height / 200)
                PathSvg { path: modelData.path }
            }
        }
    }
    Icon {
        anchors.fill: parent
        name: "JogWheelLabels"
        color: "white"
    }

    TapHandler {
        gesturePolicy: TapHandler.WithinBounds
        onPressedChanged: {
            if (pressed) {
                const sector = wheel.sectorAt(point.pressPosition.x, point.pressPosition.y)
                if (sector >= 0 && wheel.sectorEnabled(sector)) {
                    wheel.pressedSector = sector
                    wheel.model.press(wheel.sectors[sector].x, wheel.sectors[sector].y, 0)
                }
            } else if (wheel.pressedSector >= 0) {
                wheel.pressedSector = -1
                wheel.model.release()
            }
        }
        onCanceled: {
            if (wheel.pressedSector >= 0) {
                wheel.pressedSector = -1
                wheel.model.release()
            }
        }
    }

    // The stop button (StopButton): cancels a jog.
    Item {
        id: stopButton
        objectName: "jogStop"
        anchors.centerIn: parent
        width: parent.width * 71 / 180
        height: width
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: "black"
                strokeWidth: 1
                fillColor: !wheel.model.connected ? Theme.gray[500] : stopTap.pressed ? Theme.red[700] : Theme.red[500]
                scale: Qt.size(stopButton.width / 79, stopButton.height / 79)
                PathSvg { path: "m28.766 11.998 22.438.328 15.634 16.098-.328 22.439-16.098 15.634-22.438-.328L12.34 50.071l.328-22.438 16.098-15.635Z" }
            }
        }
        Text {
            anchors.centerIn: parent
            text: "STOP"
            color: "white"
            font.pixelSize: stopButton.width * 0.2
            font.bold: true
        }
        TapHandler {
            id: stopTap
            enabled: wheel.model.connected
            onTapped: wheel.model.stop()
        }
    }
}
