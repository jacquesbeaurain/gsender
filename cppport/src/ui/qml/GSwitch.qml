import QtQuick
import QtQuick.Controls.Basic
import GSender

// Upstream's Switch (components/Switch/Toggle): a 44 x 24 track, grey off
// and blue on, its white knob sliding across; half-faded when disabled. The
// tap area is the touch target's height.
AbstractButton {
    id: toggle

    checkable: true
    implicitWidth: 44
    implicitHeight: Theme.touchTarget
    opacity: enabled ? 1 : 0.5

    contentItem: Item {}
    background: Item {
        Rectangle {
            id: track
            anchors.verticalCenter: parent.verticalCenter
            width: 44
            height: 24
            radius: 12
            color: toggle.checked ? Theme.blue[600] : (Theme.dark ? Theme.surfaceElevated : Theme.gray[200])
            border.color: Theme.dark && !toggle.checked ? Theme.outline : "transparent"
            Rectangle {
                x: toggle.checked ? parent.width - width - 2 : 2
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 20
                radius: 10
                color: "white"
                border.color: toggle.checked ? "white" : Theme.gray[300]
                Behavior on x { NumberAnimation { duration: 150 } }
            }
        }
    }
}
