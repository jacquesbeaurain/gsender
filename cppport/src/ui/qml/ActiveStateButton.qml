import QtQuick
import QtQuick.Shapes
import GSender

// Upstream's ActiveStateButton: a button that, while what it started is on
// (the coolant flowing, the spindle turning), has a robin arc sweeping round
// its edge and its text in robin.
Item {
    id: root

    property alias text: button.text
    property alias iconName: button.iconName
    property alias iconSize: button.iconSize
    property alias fontSize: button.fontSize
    property bool active: false
    signal clicked()

    implicitWidth: button.implicitWidth + 4
    implicitHeight: button.implicitHeight + 4

    // The sweep: a wedge turning behind the button, seen at its edge.
    Item {
        anchors.fill: parent
        clip: true
        visible: root.active
        Shape {
            id: wedge
            readonly property real reach: Math.hypot(root.width, root.height)
            x: root.width / 2
            y: root.height / 2
            ShapePath {
                strokeWidth: -1
                fillColor: Theme.robin[500]
                startX: 0; startY: 0
                PathAngleArc {
                    centerX: 0; centerY: 0
                    radiusX: wedge.reach; radiusY: wedge.reach
                    startAngle: 0; sweepAngle: 140
                    moveToStart: false
                }
                PathLine { x: 0; y: 0 }
            }
            RotationAnimation on rotation {
                running: root.active && root.visible
                from: 0; to: 360
                duration: 4000
                loops: Animation.Infinite
            }
        }
    }

    GButton {
        id: button
        anchors.fill: parent
        anchors.margins: 2
        active: root.active
        iconColor: root.active ? Theme.robin[500] : foreground
        textColor: root.active ? Theme.robin[500] : foreground
        onClicked: root.clicked()
    }
}
