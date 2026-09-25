import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// ContinuityIndicator: press the TLS to prove its wiring. A pin already on
// when checking is a short; a press is success, done a moment later.
WizardStepPage {
    id: page

    property string phase: "checking"  // waiting, success, stuckOn

    function setPhase(next) {
        phase = next
        if (next === "success") {
            succeeded.restart()
        } else {
            succeeded.stop()
            complete = false
        }
        if (next === "checking")
            update()
    }
    function update() {
        if (phase === "checking")
            setPhase(page.model.probeActive ? "stuckOn" : "waiting")
        else if (phase === "waiting" && page.model.probeActive)
            setPhase("success")
    }

    Component.onCompleted: setPhase("checking")
    Connections {
        target: page.model
        function onChanged() { page.update() }
    }
    Timer {
        id: succeeded
        interval: 1500  // CONTINUITY_CHECK_SUCCESS_DELAY_MS
        onTriggered: page.complete = true
    }

    WizardText { text: qsTr("Let's confirm your Tool Length Sensor is wired correctly. Press the TLS down when prompted below.") }
    ColumnLayout {
        objectName: "continuity"
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 16
        Layout.bottomMargin: 16
        spacing: 8
        readonly property color tone: page.phase === "success" ? Theme.green[500]
                                      : page.phase === "stuckOn" ? Theme.red[500]
                                      : page.phase === "waiting" ? Theme.blue[500] : Theme.gray[500]
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: page.phase === "success" ? "✔" : page.phase === "stuckOn" ? "⚠" : "◎"
            font.pixelSize: 56
            color: parent.tone
        }
        Label {
            objectName: "continuityText"
            Layout.alignment: Qt.AlignHCenter
            text: ({ checking: qsTr("Checking continuity…"), waiting: qsTr("Waiting for probe contact…"),
                     success: qsTr("Continuity confirmed"), stuckOn: qsTr("Sensor triggered immediately") })[page.phase]
            color: Theme.contentPrimary
            font.pixelSize: Theme.fontLg
        }
    }
    WizardText {
        visible: page.phase === "waiting"
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("Firmly press the TLS sensor to verify the connection.")
    }
    Rectangle {
        visible: page.phase === "success" || page.phase === "stuckOn"
        Layout.fillWidth: true
        implicitHeight: result.implicitHeight + 16
        radius: 8
        color: page.phase === "success" ? "#dcfce7" : "#fee2e2"
        WizardText {
            id: result
            anchors.fill: parent
            anchors.margins: 8
            color: page.phase === "success" ? "#166534" : "#991b1b"
            text: page.phase === "success"
                  ? "<b>" + qsTr("Success") + "</b><br>" + qsTr("Continuity check passed. Your TLS is working correctly.")
                  : "<b>" + qsTr("Error") + "</b><br>" + qsTr("Probe pin immediately asserted. Check your wiring or probe for a short and confirm $6 (Invert Probe Pin) is set correctly.")
        }
    }
    GButton {
        visible: page.phase === "stuckOn"
        objectName: "retryContinuity"
        text: qsTr("Try Again")
        onClicked: page.setPhase("checking")
    }
}
