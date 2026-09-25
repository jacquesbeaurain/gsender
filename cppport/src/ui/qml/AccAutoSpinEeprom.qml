import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// AutoSpin: its EEPROM settings for the board's firmware, then the
// controller is to be restarted.
WizardStepPage {
    id: page
    WizardText { text: qsTr("Your AutoSpin EEPROM settings are applied in this step based on your connected firmware type.") }
    WizardText { text: qsTr("<ol><li>Press <b>\"Apply Settings\"</b></li><li>Reboot your controller using the power switch and reconnect</li><li>Click <b>\"Next\"</b></li></ol>") }
    WizardAction {
        id: action
        buttonName: "autospin-apply-settings"
        buttonEnabled: page.model.connected
        text: qsTr("Apply Settings")
        runningText: qsTr("Configuring...")
        onTriggered: {
            page.model.applyAutoSpin()
            applied.start()
        }
    }
    Timer {
        id: applied
        interval: 500
        onTriggered: {
            restartNotice.open()
            action.finish()
            page.complete = true
        }
    }
    ConfirmDialog {
        id: restartNotice
        objectName: "restartController"
        title: qsTr("Restart your Controller")
        message: qsTr("Please manually restart your CNC controller (power cycle) and reconnect to gSender for these settings to take effect.")
        actionText: qsTr("OK")
        cancelText: ""
    }
}
