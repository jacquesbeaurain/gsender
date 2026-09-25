import QtQuick
import GSender

// A corner button (RapidPositionButtons): a thick L pointing into the
// corner - 0 back left, 1 back right, 2 front left, 3 front right - in
// robin-500, grey when disabled.
Item {
    id: button

    property int corner: 0
    signal clicked()

    readonly property bool atLeft: corner === 0 || corner === 2
    readonly property bool atBack: corner === 0 || corner === 1
    readonly property color ink: enabled ? "#689AC9" : Theme.gray[400]

    implicitWidth: 34
    implicitHeight: 30

    Rectangle {  // the horizontal stroke, at the back or the front
        x: button.atLeft ? 3 : 0
        y: button.atBack ? 3 : button.height - 11
        width: button.width - 3
        height: 8
        color: button.ink
    }
    Rectangle {  // the vertical stroke, at the left or the right
        x: button.atLeft ? 3 : button.width - 11
        y: button.atBack ? 3 : 0
        width: 8
        height: button.height - 3
        color: button.ink
    }
    TapHandler {
        enabled: button.enabled
        onTapped: button.clicked()
    }
}
