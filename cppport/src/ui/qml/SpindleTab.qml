import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Spindle/Laser (features/Spindle): the Spindle/Laser switch - and grblHAL's
// spindles - then either the spindle's Forward (M3), Reverse (M4), Stop and
// speed, or the laser's On (focus), Test, Off, power and test duration. The
// buttons light while the board has the spindle on.
Item {
    id: tab
    objectName: "spindleTab"

    property SpindleModel model: SpindleModel {}

    // Scaled down to fit a short card, as the location column is.
    ColumnLayout {
        id: content
        anchors.centerIn: parent
        width: Math.min(parent.width - 16, 480)
        spacing: 8
        scale: Math.min(1, tab.height / Math.max(1, implicitHeight))

        // The mode, and grblHAL's spindles.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            Label { text: qsTr("Spindle") }
            GSwitch {
                objectName: "laserModeSwitch"
                property string tooltip: qsTr("Enable Spindle or Laser mode")
                checked: tab.model.laserMode
                enabled: tab.model.canClick
                onToggled: {
                    tab.model.toggleMode()
                    checked = Qt.binding(() => tab.model.laserMode)
                }
            }
            Label { text: qsTr("Laser") }
            GSelect {
                id: spindleSelect
                objectName: "spindleSelect"
                tooltip: qsTr("Select a spindle")
                visible: tab.model.grblHal && tab.model.spindles.length > 0
                enabled: tab.model.canClick
                model: tab.model.spindles
                textRole: "label"
                valueRole: "id"
                Layout.preferredWidth: 200
                Component.onCompleted: currentIndex = indexOfValue(tab.model.spindleId)
                Connections {
                    target: tab.model
                    function onChanged() { spindleSelect.currentIndex = spindleSelect.indexOfValue(tab.model.spindleId) }
                }
                onActivated: tab.model.selectSpindle(currentValue)
            }
        }

        // The spindle.
        RowLayout {
            visible: !tab.model.laserMode
            Layout.fillWidth: true
            spacing: 8
            ActiveStateButton {
                objectName: "spindleForward"
                tooltip: qsTr("Run spindle clockwise")
                Layout.fillWidth: true
                text: qsTr("Forward")
                iconName: "FaRedoAlt"
                iconSize: 16
                active: tab.model.connected && tab.model.forward
                enabled: tab.model.canClick
                onClicked: tab.model.startClockwise()
            }
            ActiveStateButton {
                objectName: "spindleReverse"
                tooltip: qsTr("Run spindle counterclockwise")
                Layout.fillWidth: true
                text: qsTr("Reverse")
                iconName: "FaUndoAlt"
                iconSize: 16
                active: tab.model.connected && tab.model.reverse
                enabled: tab.model.canClick
                onClicked: tab.model.startCounterClockwise()
            }
            ActiveStateButton {
                objectName: "spindleStop"
                tooltip: qsTr("Stop spindle")
                Layout.fillWidth: true
                text: qsTr("Stop")
                iconName: "FaBan"
                iconSize: 16
                enabled: tab.model.canClick
                onClicked: tab.model.stop()
            }
        }
        GridLayout {
            visible: !tab.model.laserMode
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            Label {
                text: qsTr("Speed")
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 1
                Layout.fillWidth: true
            }
            Slider {
                objectName: "spindleSpeed"
                property string tooltip: qsTr("Adjust spindle speed")
                property string tooltipSide: "bottom"
                visible: tab.model.speedSlider
                from: tab.model.speedMin
                to: tab.model.speedMax
                stepSize: 10
                value: tab.model.speed
                enabled: tab.model.canClick
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                onMoved: tab.model.setSpeed(value)
            }
            NumberField {
                objectName: "spindleSpeedInput"
                tooltip: qsTr("Adjust spindle speed")
                visible: !tab.model.speedSlider
                value: tab.model.speed
                decimals: 0
                enabled: tab.model.canClick
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                onCommitted: (text) => tab.model.setSpeed(Number(text))
            }
            Label {
                objectName: "spindleSpeedText"
                text: tab.model.speedSlider ? qsTr("%1 RPM").arg(Math.round(tab.model.speed)) : qsTr("RPM")
                Layout.preferredWidth: 1
                Layout.fillWidth: true
            }
        }

        // The laser.
        RowLayout {
            visible: tab.model.laserMode
            Layout.fillWidth: true
            spacing: 8
            ActiveStateButton {
                objectName: "laserOn"
                tooltip: qsTr("Turn on laser")
                Layout.fillWidth: true
                text: qsTr("Laser On")
                iconName: "FaLightbulb"
                iconSize: 16
                active: tab.model.connected && tab.model.laserOn
                enabled: tab.model.canClick
                onClicked: tab.model.startClockwise()
            }
            ActiveStateButton {
                objectName: "laserTest"
                tooltip: qsTr("Turn on laser for Test Duration")
                Layout.fillWidth: true
                text: qsTr("Laser Test")
                iconName: "FaSatelliteDish"
                iconSize: 16
                enabled: tab.model.canClick
                onClicked: tab.model.startCounterClockwise()
            }
            ActiveStateButton {
                objectName: "laserOff"
                tooltip: qsTr("Turn off laser")
                Layout.fillWidth: true
                text: qsTr("Off")
                iconName: "FaRegLightbulb"
                iconSize: 16
                enabled: tab.model.canClick
                onClicked: tab.model.stop()
            }
        }
        GridLayout {
            visible: tab.model.laserMode
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            Label {
                text: qsTr("Power")
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 1
                Layout.fillWidth: true
            }
            Slider {
                objectName: "laserPower"
                property string tooltip: qsTr("Adjust laser power")
                from: 0
                to: 100
                stepSize: 1
                value: tab.model.power
                enabled: tab.model.canClick
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                onMoved: tab.model.setPower(value)
            }
            Label {
                text: Math.round(tab.model.power) + "%"
                Layout.preferredWidth: 1
                Layout.fillWidth: true
            }
        }
        RowLayout {
            visible: tab.model.laserMode
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            Label { text: qsTr("Test Duration:") }
            NumberField {
                objectName: "laserDuration"
                tooltip: qsTr("Laser test duration")
                value: tab.model.duration
                decimals: 1
                horizontalAlignment: TextInput.AlignHCenter
                color: Theme.primaryText
                font.pixelSize: Theme.fontXl
                Layout.preferredWidth: 80
                onCommitted: (text) => tab.model.setDuration(Number(text))
            }
            Label { text: qsTr("sec"); color: Theme.contentMuted }
        }
    }
}
