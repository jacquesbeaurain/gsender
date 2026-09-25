import QtQuick
import GSender

// Vacuum Table: zero X and Y at the table's front-left corner.
WizardStepPage {
    id: page
    WizardText { text: qsTr("Jog to the front-left corner of your vacuum table.") }
    WizardText { text: qsTr("Once you're in position, zero the X and Y axes so the mounting and grid files line up with the table.") }
    WizardAction {
        buttonName: "zeroXY"
        text: qsTr("Zero X/Y")
        runningText: qsTr("Zeroing...")
        onTriggered: {
            page.model.zeroXY()
            finish()
            page.complete = true
        }
    }
}
