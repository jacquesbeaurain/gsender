import QtQuick
import GSender

// Sienci Spindle: its settings (and the Spindle/Laser tab), then Modbus -
// each done a moment after it is sent, as upstream waits.
WizardStepPage {
    id: page
    property bool modbus: false

    WizardText {
        visible: page.modbus
        text: "<b>" + qsTr("You are able to complete this step while the controller is still alarmed") + "</b>"
    }
    WizardText {
        text: page.modbus ? qsTr("Additional spindle settings are applied in this step.")
                          : qsTr("Your spindle settings are applied in this step and the controller will restart automatically.")
    }
    WizardText {
        text: page.modbus
              ? qsTr("<ol><li>Reconnect to your controller. Please ignore any alarms that pop-up.</li><li>Press <b>\"Apply and Restart\"</b></li></ol>")
              : "<ol><li>" + qsTr("Press <b>\"Apply And Restart\"</b>") + "</li>"
                + (page.model.atciFirmware ? "" : "<li>" + qsTr("Reboot your controller using the power switch and reconnect") + "</li>")
                + "<li>" + qsTr("Click <b>\"Next\"</b>") + "</li></ol>"
    }
    WizardAction {
        id: action
        buttonName: page.modbus ? "ss-configure-modbus" : "ss-setup-spindle-reboot"
        buttonEnabled: page.model.connected
        text: page.modbus ? qsTr("Configure Modbus") : qsTr("Setup Spindle")
        runningText: qsTr("Configuring...")
        onTriggered: {
            if (page.modbus)
                page.model.applyModbus()
            else
                page.model.applySpindle()
            applied.start()
        }
    }
    Timer {
        id: applied
        interval: page.modbus ? 1500 : 500
        onTriggered: {
            action.finish()
            page.complete = true
        }
    }
}
