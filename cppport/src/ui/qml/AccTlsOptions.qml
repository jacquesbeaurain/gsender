import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Sienci TLS: the first tool's behaviour and the manual tool change
// location; Apply sets the Fixed Tool Sensor strategy.
WizardStepPage {
    id: page
    WizardText { text: qsTr("Configure how gSender should handle tool changes with your Tool Length Sensor (TLS).") }
    WizardText { text: "<b>" + qsTr("First tool behaviour") + "</b>" }
    ComboBox {
        id: first
        objectName: "firstToolBehaviour"
        Layout.preferredWidth: 300
        model: page.model.firstToolBehaviours
        currentIndex: 1  // "Prompt for first tool"
    }
    WizardText {
        color: Theme.contentSecondary
        text: [
            qsTr("Runs the complete tool change process every time, including for the first tool."),
            qsTr("Asks whether to run the full wizard or just probe the current tool length for the first tool change."),
            qsTr("Skips the tool change prompt and only measures the current tool for the first tool change.")
        ][Math.max(0, Math.min(2, first.currentIndex))]
    }
    RowLayout {
        spacing: 12
        GSwitch {
            id: custom
            objectName: "customLocation"
            checked: true
        }
        Label { text: qsTr("Set manual tool change location"); color: Theme.contentPrimary }
    }
    WizardText {
        color: Theme.contentSecondary
        text: qsTr("Move the CNC to a more convenient location for manual tool changes instead of prompting to change over the sensor.")
    }
    WizardText { text: qsTr("Select <b>\"Apply\"</b> to set your tool change strategy to Fixed Tool Sensor and save these options.") }
    WizardAction {
        buttonName: "applyOptions"
        text: qsTr("Apply")
        runningText: qsTr("Applying...")
        onTriggered: {
            page.model.applyTlsOptions(first.currentText, custom.checked)
            finish(qsTr("Tool change options configured."))
            page.complete = true
        }
    }
}
