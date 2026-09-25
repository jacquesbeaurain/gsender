import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// AutoSpin: run the spindle at a chosen speed ($31 - $30); a running
// spindle takes a new speed 300 ms after the last change.
WizardStepPage {
    id: page
    WizardText { text: qsTr("Test your AutoSpin setup by running the spindle at a set speed.") }
    WizardText { text: qsTr("<ol><li>Turn the AutoSpin dial to \"S\"</li><li>Turn on the spindle using the power toggle</li><li>Choose a speed with the slider</li><li>Press <b>\"Start\"</b> to run the spindle (M3)</li></ol>") }
    Slider {
        id: speed
        objectName: "autospin-test-speed"
        Layout.fillWidth: true
        from: page.model.spindleMin
        to: page.model.spindleMax
        stepSize: 100
        value: page.model.spindleMin
        enabled: page.model.connected
        onMoved: if (page.model.spindleRunning) change.restart()
    }
    Label {
        objectName: "autospin-range"
        text: qsTr("%1 RPM - Range %2 - %3 RPM ($31 - $30)").arg(Math.round(speed.value)).arg(page.model.spindleMin).arg(page.model.spindleMax)
              + (page.model.spindleRunning && page.model.connected ? qsTr(" · reporting %1 RPM").arg(page.model.spindleReported) : "")
        color: Theme.contentSecondary
    }
    RowLayout {
        spacing: 12
        GButton {
            objectName: "autospin-start"
            variant: "success"
            iconName: "LuPlay"
            text: qsTr("Start")
            enabled: page.model.connected
            onClicked: {
                page.model.startSpindle(Math.round(speed.value))
                page.complete = true
            }
        }
        GButton {
            objectName: "autospin-stop"
            variant: "error"
            text: qsTr("Stop")
            enabled: page.model.connected
            onClicked: {
                change.stop()
                page.model.stopSpindle()
            }
        }
    }
    Timer {
        id: change
        interval: 300
        onTriggered: page.model.changeSpindleSpeed(Math.round(speed.value))
    }
}
