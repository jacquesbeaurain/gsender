import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Rotary (features/Rotary): the Rotary switch (with 4-Axis beside it on
// grblHAL) - entering rotary mode says what it will do first - and Rotary
// Surfacing, Mounting Setup, Probe Rotary Z-Axis and Y-Axis Alignment.
Item {
    id: tab
    objectName: "rotaryTab"

    property RotaryModel model: RotaryModel {}

    // The switch's and the shortcuts' (SWITCH_WORKSPACE_MODE,
    // TOGGLE_MOUNTING_SETUP) way in.
    function toggleMode() {
        if (!model.canSwitch)
            return
        if (model.rotaryMode)
            model.setRotaryMode(false)
        else
            enable.open()
    }
    function openMounting() {
        if (model.mountingAvailable)
            mounting.openFresh()
    }

    ConfirmDialog {
        id: enable
        objectName: "confirmRotaryMode"
        title: qsTr("Enable Rotary Mode")
        message: tab.model.enableConfirmation()
        actionText: qsTr("OK")
        onAccepted: tab.model.setRotaryMode(true)
    }
    ConfirmDialog {
        id: probe
        objectName: "confirmRotaryProbe"
        property bool yAlignment: false
        readonly property string name: yAlignment ? qsTr("Y-Axis Alignment") : qsTr("Rotary Z-Axis")
        title: qsTr("%1 probing").arg(name)
        message: qsTr("Click 'Run' to start the %1 probing cycle").arg(name)
        actionText: qsTr("Run")
        onAccepted: tab.model.runProbe(yAlignment)
    }
    MountingSetupDialog {
        id: mounting
        model: tab.model
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 16

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            Label { visible: tab.model.grblHal; text: qsTr("4-Axis"); color: Theme.contentPrimary }
            GSwitch {
                objectName: "rotaryModeSwitch"
                checked: tab.model.rotaryMode
                enabled: tab.model.canSwitch
                onToggled: {
                    // The switch shows the mode: a declined change leaves it.
                    checked = Qt.binding(() => tab.model.rotaryMode)
                    tab.toggleMode()
                }
            }
            Label { text: qsTr("Rotary"); color: Theme.contentPrimary }
        }
        GridLayout {
            columns: 2
            rowSpacing: 12
            columnSpacing: 12
            GButton {
                objectName: "rotarySurfacing"
                Layout.fillWidth: true
                text: qsTr("Rotary Surfacing")
                fontSize: Theme.fontSm
                enabled: tab.model.surfacingAvailable
                onClicked: Backend.openTool("rotarySurfacing")
            }
            GButton {
                objectName: "mountingSetupButton"
                Layout.fillWidth: true
                text: qsTr("Mounting Setup")
                fontSize: Theme.fontSm
                enabled: tab.model.mountingAvailable
                onClicked: tab.openMounting()
            }
            GButton {
                objectName: "probeRotaryZ"
                Layout.fillWidth: true
                variant: "primary"
                text: qsTr("Probe Rotary Z-Axis")
                fontSize: Theme.fontSm
                enabled: tab.model.probeZAvailable
                onClicked: { probe.yAlignment = false; probe.open() }
            }
            GButton {
                objectName: "alignYAxis"
                Layout.fillWidth: true
                variant: "primary"
                text: qsTr("Y-Axis Alignment")
                fontSize: Theme.fontSm
                enabled: tab.model.alignYAvailable
                onClicked: { probe.yAlignment = true; probe.open() }
            }
        }
    }
}
