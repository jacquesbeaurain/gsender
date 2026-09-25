import QtQuick
import QtQuick.Layouts
import GSender

// The Carve page's column (workspace/Column, features/Location): the DRO
// over the jog controls in one widget card. Where the height is short the
// content scales down to fit, as upstream scales it (max-xl:scale-90).
Card {
    id: column
    objectName: "locationColumn"
    padding: 12

    Item {
        id: area
        anchors.fill: parent

        ColumnLayout {
            id: content
            readonly property real fit: Math.min(1, area.height / implicitHeight)
            width: area.width / fit
            height: Math.max(implicitHeight, area.height / fit)
            scale: fit
            transformOrigin: Item.TopLeft
            spacing: 12
            DroPanel {
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
            JogPanel {
                Layout.fillWidth: true
            }
        }
    }
}
