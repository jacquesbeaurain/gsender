import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The pop-ups (sonner): stacked at the bottom right, the newest lowest, at
// most three; each for workspace.toastDuration (0 the default 5 s, -1 until
// closed, -2 none - the message is still kept in the bell's list).
Item {
    id: area
    objectName: "toastArea"

    property var toasts: []   // {id, text, type}
    property int nextId: 1
    readonly property int count: toasts.length

    function show(text, type, duration) {
        if (duration === -2)
            return
        const toast = { id: nextId++, text: text, type: type }
        let list = toasts.concat([toast])
        if (list.length > 3)
            list = list.slice(list.length - 3)
        toasts = list
        if (duration !== -1)
            Qt.callLater(() => timer.createObject(area, { toastId: toast.id, interval: duration > 0 ? duration : 5000 }))
    }
    function dismiss(id) {
        toasts = toasts.filter(t => t.id !== id)
    }

    Component {
        id: timer
        Timer {
            property int toastId
            running: true
            onTriggered: { area.dismiss(toastId); destroy() }
        }
    }
    Connections {
        target: Backend
        function onToast(text, type, duration) { area.show(text, type, duration) }
    }

    ColumnLayout {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 8
        Repeater {
            model: area.toasts
            Rectangle {
                required property var modelData
                objectName: "toast"
                implicitWidth: 360
                implicitHeight: row.implicitHeight + 24
                radius: Theme.radius
                color: Theme.dark ? Theme.surfaceElevated : "white"
                border.color: Theme.outline
                // The type's rail.
                Rectangle {
                    width: 4
                    height: parent.height - 16
                    anchors.left: parent.left
                    anchors.leftMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 2
                    color: modelData.type === "error" ? Theme.red[500]
                         : modelData.type === "success" ? Theme.green[500]
                         : modelData.type === "warning" ? Theme.yellow600 : Theme.blue[500]
                }
                RowLayout {
                    id: row
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 4
                    Label {
                        objectName: "toastText"
                        text: modelData.text
                        wrapMode: Text.Wrap
                        color: Theme.contentPrimary
                        Layout.fillWidth: true
                    }
                    GButton {
                        variant: "ghost"
                        iconName: "MdClose"
                        onClicked: area.dismiss(modelData.id)
                    }
                }
            }
        }
    }
}
