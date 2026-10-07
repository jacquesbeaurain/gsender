import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import GSender

// The scrollbar the Electron app shows (Chromium's, 15 px wide): a pale track,
// a rounded thumb and a small arrow at each end, always visible while there is
// something to scroll. `inset` leaves room above it where the page's scroll
// area starts below its own top.
ScrollBar {
    id: bar
    property real inset: 0
    readonly property color ink: "#8b8b8b"   // Chromium keeps its light scrollbar in dark mode too

    orientation: Qt.Vertical
    policy: size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    implicitWidth: 15
    topPadding: inset + 14
    bottomPadding: 14
    minimumSize: 0.05

    background: Item {
        Rectangle {
            y: bar.inset
            width: parent.width
            height: parent.height - bar.inset
            color: "#fcfcfc"
        }
        Shape {
            x: (parent.width - 8) / 2
            y: bar.inset + 4
            width: 8
            height: 6
            ShapePath {
                fillColor: bar.ink
                strokeWidth: -1
                startX: 4; startY: 0
                PathLine { x: 8; y: 6 }
                PathLine { x: 0; y: 6 }
                PathLine { x: 4; y: 0 }
            }
        }
        Shape {
            x: (parent.width - 8) / 2
            y: parent.height - 10
            width: 8
            height: 6
            ShapePath {
                fillColor: bar.ink
                strokeWidth: -1
                startX: 0; startY: 0
                PathLine { x: 8; y: 0 }
                PathLine { x: 4; y: 6 }
                PathLine { x: 0; y: 0 }
            }
        }
    }
    contentItem: Item {
        implicitWidth: 15
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 9
            height: parent.height
            radius: 4.5
            color: bar.pressed ? Qt.darker(bar.ink, 1.4) : bar.hovered ? Qt.darker(bar.ink, 1.15) : bar.ink
        }
    }
}
