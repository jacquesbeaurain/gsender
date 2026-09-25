import QtQuick
import QtQuick.Shapes
import GSender

// A two-button jog tab (Jogging/components/TabJog: Z, and A): the upper half
// jogs +, the lower -, with upstream's arrow labels. Tap to step, hold to
// jog until released.
Item {
    id: tab

    property string labels: "JogZLabels"
    property bool canJog: true
    signal pressedDirection(int direction)
    signal released()

    property int pressedHalf: 0   // 1 upper, -1 lower

    implicitWidth: 45
    implicitHeight: 168

    Repeater {
        model: [
            { direction: 1, path: "M0.5 10C0.5 4.75329 4.75329 0.5 10 0.5H40C45.2467 0.5 49.5 4.7533 49.5 10V88.5H0.5V10Z" },
            { direction: -1, path: "M0.5 98.5H49.5V177C49.5 182.247 45.2467 186.5 40 186.5H10C4.75329 186.5 0.5 182.247 0.5 177V98.5Z" }
        ]
        Shape {
            required property var modelData
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: -1
                fillColor: !tab.canJog ? (Theme.dark ? Theme.gray[700] : Theme.gray[400])
                         : tab.pressedHalf === modelData.direction ? Theme.blue[700] : Theme.blue[500]
                scale: Qt.size(tab.width / 50, tab.height / 187)
                PathSvg { path: modelData.path }
            }
        }
    }
    Icon {
        anchors.fill: parent
        name: tab.labels
        color: "white"
    }
    TapHandler {
        enabled: tab.canJog
        gesturePolicy: TapHandler.WithinBounds
        onPressedChanged: {
            if (pressed) {
                tab.pressedHalf = point.pressPosition.y < tab.height / 2 ? 1 : -1
                tab.pressedDirection(tab.pressedHalf)
            } else if (tab.pressedHalf !== 0) {
                tab.pressedHalf = 0
                tab.released()
            }
        }
        onCanceled: {
            if (tab.pressedHalf !== 0) {
                tab.pressedHalf = 0
                tab.released()
            }
        }
    }
}
