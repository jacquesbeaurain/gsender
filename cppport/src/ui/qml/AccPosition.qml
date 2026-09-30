import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// PositionSetter: X, Y and Z (workspace units) following the machine until
// edited; Set Position keeps them, undone when the machine moves away. The
// TLS's location starts at the machine's position; the manual tool change
// location at the recommended one, with Go To, until the first real jog.
// AccessoryModel does the following (gs::toolchange::PositionFollower).
WizardStepPage {
    id: page

    property bool manual: false
    complete: page.model.positionSet

    Component.onCompleted: page.model.startPositionStep(manual)
    Connections {
        target: page.model
        function onPositionUnset() { action.reset() }
    }

    WizardText {
        text: page.manual
              ? qsTr("Jog to the location you'd like the machine to move to for manual tool changes, then set the position using the <b>\"Set Position\"</b> button.")
              : qsTr("Install the tallest bit you own in your spindle or router. Jog until it's positioned just above (10-20mm) the Tool Length Sensor, then set the position using the <b>\"Set Position\"</b> button.")
    }
    WizardText {
        color: Theme.contentSecondary
        text: page.manual
              ? qsTr("The fields below are already filled with a recommended position. Hit <b>\"Go To\"</b> to send the machine there, then jog to fine-tune it from that starting point before setting the position.")
              : qsTr("Using your tallest tool gives the most Z-axis clearance above the sensor, so its measured position ends up negative - this is what lets gSender accurately probe tools of any length during a tool change without running out of travel.")
    }
    WizardText { text: "<b>" + qsTr("Position (%1)").arg(page.model.units) + "</b>" }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Repeater {
            id: fields
            model: ["X", "Y", "Z"]
            TextField {
                required property string modelData
                required property int index
                objectName: "position" + modelData
                Layout.fillWidth: true
                implicitHeight: 40
                leftPadding: 28
                horizontalAlignment: TextInput.AlignRight
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                // Following the machine until typed into.
                text: page.model.positionFields[index] ?? ""
                onTextEdited: page.model.positionEdited()
                Label {
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.modelData
                    font.bold: true
                    color: Theme.contentSecondary
                }
            }
        }
    }
    RowLayout {
        spacing: 12
        WizardAction {
            id: action
            Layout.alignment: Qt.AlignTop
            buttonName: "setPosition"
            text: qsTr("Set Position")
            runningText: qsTr("Setting...")
            onTriggered: {
                page.model.setPosition(fields.itemAt(0).text, fields.itemAt(1).text, fields.itemAt(2).text)
                finish(page.manual ? qsTr("Tool change location set.") : qsTr("TLS location set."))
            }
        }
        GButton {
            visible: page.manual
            objectName: "goToPosition"
            Layout.alignment: Qt.AlignTop
            text: qsTr("Go To")
            onClicked: page.model.goToPosition(page.model.positionMm(fields.itemAt(0).text),
                                               page.model.positionMm(fields.itemAt(1).text),
                                               page.model.positionMm(fields.itemAt(2).text))
        }
    }
}
