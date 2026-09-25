import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Vacuum Table: the table's size (the installer remembers it).
WizardStepPage {
    id: page
    complete: true  // a size is always chosen
    WizardText { text: qsTr("Choose the size of your vacuum table.") }
    ComboBox {
        objectName: "tableSize"
        Layout.preferredWidth: 280
        model: page.model.vacuumSizes
        textRole: "label"
        valueRole: "value"
        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(page.installer.vacuumSize))
        onActivated: page.installer.vacuumSize = currentValue
    }
}
