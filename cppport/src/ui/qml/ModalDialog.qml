import QtQuick
import QtQuick.Controls.Basic

// A modal dialog (shadcn's Dialog): centred over the window, which it dims,
// on a popover panel. `preferredWidth` narrows to fit a small window.
Popup {
    property real preferredWidth: 460

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 24
    width: Math.min(preferredWidth, parent ? parent.width - 32 : preferredWidth)

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
    background: Panel {
        color: Theme.surfaceElevated
        border.color: Theme.outlineSubtle
    }
}
