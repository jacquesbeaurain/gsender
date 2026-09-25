import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// PositionSetter: X, Y and Z (workspace units) following the machine until
// edited; Set Position keeps them, undone when the machine moves away. The
// TLS's location starts at the machine's position; the manual tool change
// location at the recommended one, with Go To, until the first real jog.
WizardStepPage {
    id: page

    property bool manual: false
    property var atStart: []
    property var setAt: null
    property bool editing: false

    function same(a, b) { return JSON.stringify(a) === JSON.stringify(b) }
    function show(mm) {
        for (let axis = 0; axis < 3; ++axis)
            fields.itemAt(axis).text = page.model.positionText(mm.length === 3 ? mm[axis] : 0)
    }
    function position() {
        return [0, 1, 2].map(axis => page.model.positionMm(fields.itemAt(axis).text))
    }
    function follow() {
        const mpos = page.model.machinePosition
        if (editing || mpos.length !== 3)
            return
        if (manual && (atStart.length !== 3 || same(atStart, mpos)))
            return  // no real jog since the step opened: keep the recommendation
        if (complete && setAt && !same(setAt, mpos)) {
            action.reset()
            complete = false
        }
        show(mpos)
    }

    Component.onCompleted: {
        atStart = page.model.machinePosition
        show(manual ? page.model.recommendedManualPosition() : page.model.machinePosition)
    }
    Connections {
        target: page.model
        function onChanged() { page.follow() }
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
                objectName: "position" + modelData
                Layout.fillWidth: true
                implicitHeight: 40
                leftPadding: 28
                horizontalAlignment: TextInput.AlignRight
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                onTextEdited: {
                    page.editing = true
                    if (page.complete) {
                        action.reset()
                        page.complete = false
                    }
                }
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
                const at = page.position()
                if (page.manual)
                    page.model.setManualPosition(at[0], at[1], at[2])
                else
                    page.model.setTlsLocation(at[0], at[1], at[2])
                page.setAt = page.model.machinePosition
                finish(page.manual ? qsTr("Tool change location set.") : qsTr("TLS location set."))
                page.complete = true
            }
        }
        GButton {
            visible: page.manual
            objectName: "goToPosition"
            Layout.alignment: Qt.AlignTop
            text: qsTr("Go To")
            onClicked: {
                const at = page.position()
                page.model.goToPosition(at[0], at[1], at[2])
            }
        }
    }
}
