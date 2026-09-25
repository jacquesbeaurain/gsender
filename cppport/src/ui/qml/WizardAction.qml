import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// StepActionButton: a button that runs once - its running label, then
// "Complete" (or an error) - with a message below it.
ColumnLayout {
    id: action

    property string text
    property string runningText
    property bool running: false
    property bool done: false
    property string error
    property string success
    property alias buttonName: button.objectName
    property alias buttonEnabled: button.enabled
    signal triggered()

    function finish(message) { running = false; done = true; error = ""; success = message || "" }
    function fail(message) { running = false; error = message }
    function reset() { running = false; done = false; error = ""; success = "" }

    spacing: 8
    GButton {
        id: button
        Layout.preferredWidth: Math.max(180, implicitWidth)
        variant: action.done ? "success" : "primary"
        text: action.done ? "✔ " + qsTr("Complete") : action.error !== "" ? qsTr("Error")
              : action.running ? action.runningText : action.text
        onClicked: {
            if (action.done)
                return
            action.running = true
            action.triggered()
        }
    }
    Rectangle {
        readonly property string message: action.error !== "" ? action.error : action.done ? action.success : ""
        visible: message !== ""
        Layout.fillWidth: true
        implicitHeight: messageLabel.implicitHeight + 16
        radius: 8
        color: action.error !== "" ? "#fee2e2" : "#dcfce7"
        Label {
            id: messageLabel
            anchors.fill: parent
            anchors.margins: 8
            wrapMode: Text.Wrap
            text: parent.message
            color: action.error !== "" ? "#991b1b" : "#166534"
        }
    }
}
