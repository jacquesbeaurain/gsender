import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import GSender

// The machine state (MachineStatus): a trapezoid hanging from the top bar
// (clip-path 0 0, 100% 0, 85% 100%, 15% 100%) in the state's colour, the
// state's name in light 30px type, "Alarm (n)" with the alarm's code.
Item {
    id: pill
    objectName: "statusPill"

    readonly property string text: Backend.stateText + (Backend.alarmCode ? " (" + Backend.alarmCode + ")" : "")

    implicitWidth: 288
    implicitHeight: 60

    Shape {
        anchors.fill: parent
        ShapePath {
            strokeWidth: -1
            fillColor: Theme.stateColor(Backend.activeState)
            startX: 0; startY: 0
            PathLine { x: pill.width; y: 0 }
            PathLine { x: pill.width * 0.85; y: pill.height }
            PathLine { x: pill.width * 0.15; y: pill.height }
            PathLine { x: 0; y: 0 }
        }
    }
    Label {
        objectName: "statusText"
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -2
        text: pill.text
        color: "white"
        font.pixelSize: Theme.font3xl
        font.weight: Font.Light
    }
}
