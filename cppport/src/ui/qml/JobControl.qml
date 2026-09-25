import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Job control (features/JobControl): Outline and Start From above the
// card, Start / Pause / Stop on its top edge, the overrides inside.
Item {
    id: control
    objectName: "jobControl"

    property JobModel model: JobModel {}

    Connections {
        target: control.model
        function onNotice(text) { Backend.notify(text, "info") }
    }
    StartFromLinePopup {
        id: startFromLine
        model: control.model
    }

    Card {
        anchors.fill: parent
        anchors.topMargin: 24
        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 30
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8
            OverrideSlider {
                objectName: "feedOverride"
                Layout.fillWidth: true
                title: qsTr("Feed")
                valueText: control.model.feedText
                percent: control.model.feedOverride
                enabled: control.model.connected
                onChangeRequested: (percent) => control.model.setFeedOverride(percent)
            }
            OverrideSlider {
                objectName: "spindleOverride"
                visible: control.model.showSpindleOverride
                Layout.fillWidth: true
                title: control.model.spindleLabel
                valueText: control.model.spindleText
                percent: control.model.spindleOverride
                fill: Theme.red[500]
                enabled: control.model.connected
                onChangeRequested: (percent) => control.model.setSpindleOverride(percent)
            }
            Item { Layout.fillHeight: true }
        }
    }

    // Start / Pause / Stop (ControlButton).
    component ControlButton: AbstractButton {
        id: button
        property string iconName
        property color fill
        implicitWidth: 96
        implicitHeight: 48
        background: Rectangle {
            radius: 4
            color: button.enabled ? button.fill : (Theme.dark ? Theme.surfaceRaised : Theme.gray[300])
            border.color: Theme.gray[600]
            opacity: button.pressed ? 0.85 : 1
        }
        contentItem: RowLayout {
            spacing: 4
            Icon {
                name: button.iconName
                color: button.enabled ? "white" : Theme.gray[600]
                width: 28
                height: 28
                Layout.alignment: Qt.AlignVCenter
            }
            Label {
                text: button.text
                color: button.enabled ? "white" : Theme.gray[600]
                font.pixelSize: Theme.fontBase
                Layout.fillWidth: true
            }
        }
    }
    RowLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        spacing: 8
        ControlButton {
            objectName: "startJob"
            text: qsTr("Start")
            iconName: "IoPlayOutline"
            fill: Theme.dark ? Theme.green[700] : Theme.green[600]
            enabled: control.model.canStart
            onClicked: control.model.start()
        }
        ControlButton {
            objectName: "pauseJob"
            text: qsTr("Pause")
            iconName: "PiPause"
            fill: Theme.dark ? Theme.orange[700] : "#c27924"
            enabled: control.model.canPause
            onClicked: control.model.pause()
        }
        ControlButton {
            objectName: "stopJob"
            text: qsTr("Stop")
            iconName: "FiOctagon"
            fill: Theme.dark ? Theme.red[700] : Theme.red[500]
            enabled: control.model.canStop
            onClicked: control.model.stop()
        }
    }

    // Outline and Start From: CarvePage places their buttons above the card
    // (within its own bounds, where they receive taps).
    function runOutline() {
        const error = control.model.outline()
        if (error)
            Backend.notify(error, "error")
    }
    function openStartFromLine() {
        startFromLine.open()
    }
}
