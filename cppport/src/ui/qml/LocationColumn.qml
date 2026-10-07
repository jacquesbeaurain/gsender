import QtQuick
import QtQuick.Layouts
import GSender

// The Carve page's column (workspace/Column, features/Location): the DRO
// over the jog controls in one widget card. The parts are sized as upstream
// sizes them at the 1280 px window (the DRO rows at 95%, the wheel at 90%),
// so they fill the card as its page does.
Card {
    id: column
    objectName: "locationColumn"
    padding: 0

    Item {
        id: area
        anchors.fill: parent

        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 0
            DroPanel {
                Layout.fillWidth: true
            }
            JogPanel {
                Layout.fillWidth: true
                Layout.topMargin: 30
            }
        }
    }
}
