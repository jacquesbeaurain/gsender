import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import GSender

// Flash Firmware (features/Config/components/FlashDialog and
// FlashingProgress): the port, the controller type and the firmware file; a
// warning that asks to continue; then the progress and the flash log. The
// built-in simulators are the ports it can flash.
ModalDialog {
    id: dialog
    objectName: "flashDialog"

    property FlashModel model: FlashModel { objectName: "flashModel" }
    readonly property bool idle: model.state === FlashModel.Idle
    property string port
    property string controllerType: "grbl"
    property url firmware

    preferredWidth: 650
    padding: 24
    onAboutToShow: {
        model.reset()
        port = model.ports.length ? model.ports[0] : ""
        controllerType = port === "Simulator grblHAL" ? "grblHAL" : "grbl"
        firmware = ""
    }

    FileDialog {
        id: firmwareFile
        title: qsTr("Firmware File")
        nameFilters: [qsTr("Firmware (*.hex *.uf2)"), qsTr("All files (*)")]
        onAccepted: dialog.firmware = selectedFile
    }

    contentItem: ColumnLayout {
        spacing: 16
        Label {
            text: qsTr("Flash Firmware")
            font.pixelSize: Theme.fontLg
            font.bold: true
        }
        Label {
            text: qsTr("This feature exists to flash firmware onto a compatible SLB or Arduino-based device.")
            font.pixelSize: Theme.fontSm
            color: Theme.contentBody
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // The form.
        ColumnLayout {
            visible: dialog.idle
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                spacing: 2
                Label { text: qsTr("Port"); font.pixelSize: Theme.fontSm; color: Theme.contentMuted }
                GSelect {
                    objectName: "flashPort"
                    Layout.fillWidth: true
                    model: dialog.model.ports
                    currentIndex: Math.max(0, dialog.model.ports.indexOf(dialog.port))
                    onActivated: (index) => {
                        dialog.port = dialog.model.ports[index]
                        if (dialog.port === "Simulator grblHAL")
                            dialog.controllerType = "grblHAL"
                    }
                }
            }
            ColumnLayout {
                spacing: 2
                Label { text: qsTr("Controller Type"); font.pixelSize: Theme.fontSm; color: Theme.contentMuted }
                GSelect {
                    objectName: "flashController"
                    Layout.fillWidth: true
                    model: ["grbl", "grblHAL"]
                    currentIndex: dialog.controllerType === "grblHAL" ? 1 : 0
                    onActivated: (index) => dialog.controllerType = model[index]
                }
            }
            ColumnLayout {
                spacing: 2
                opacity: dialog.controllerType === "grbl" ? 0 : 1
                Label { text: qsTr("Firmware File"); font.pixelSize: Theme.fontSm; color: Theme.contentMuted }
                RowLayout {
                    spacing: 8
                    GButton {
                        objectName: "flashChooseFile"
                        text: qsTr("Choose File")
                        enabled: dialog.controllerType !== "grbl"
                        fontSize: Theme.fontSm
                        implicitHeight: 32
                        onClicked: firmwareFile.open()
                    }
                    Label {
                        text: dialog.firmware.toString() ? decodeURIComponent(dialog.firmware.toString().split("/").pop()) : qsTr("No file chosen")
                        font.pixelSize: Theme.fontSm
                        color: Theme.contentBody
                    }
                }
            }
        }

        // The warning and its question.
        Rectangle {
            visible: dialog.idle
            Layout.fillWidth: true
            Layout.topMargin: 16
            implicitHeight: warning.implicitHeight + 32
            color: Qt.rgba(0xfe / 255, 0xf9 / 255, 0xc3 / 255, 0.6)
            border.color: Theme.tw.yellow[500]
            border.width: Theme.hairline
            ColumnLayout {
                id: warning
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSm
                    color: Theme.gray[600]
                    text: qsTr("This process will disconnect your machine, and may take a couple of minutes to complete.<br><b>Continue?</b>")
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 16
                    GButton {
                        objectName: "flashNo"
                        text: qsTr("No")
                        onClicked: dialog.close()
                    }
                    GButton {
                        objectName: "flashYes"
                        text: qsTr("Yes")
                        variant: "primary"
                        enabled: dialog.model.canStart(dialog.port, dialog.controllerType, dialog.firmware)
                        onClicked: {
                            const failure = dialog.model.start(dialog.port, dialog.controllerType, dialog.firmware)
                            if (failure)
                                Backend.notify(failure, "error")
                        }
                    }
                }
            }
        }

        // The progress and the log.
        ColumnLayout {
            visible: !dialog.idle
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                objectName: "flashProgress"
                visible: dialog.controllerType !== "grbl"
                Layout.fillWidth: true
                implicitHeight: 12
                radius: 6
                color: Theme.secondary
                border.color: Theme.outline
                border.width: Theme.hairline
                Rectangle {
                    width: parent.width * dialog.model.progress / dialog.model.total
                    height: parent.height
                    radius: 6
                    color: Theme.blue[500]
                }
            }
            Panel {
                Layout.fillWidth: true
                implicitHeight: 40 + 160
                radius: 4
                color: Theme.surfaceRaised
                border.color: Theme.outline
                clip: true
                Label {
                    x: 12; y: 8
                    text: qsTr("Flash log")
                    font.pixelSize: Theme.fontSm
                    color: Theme.contentMuted
                }
                Rectangle { y: 32; width: parent.width; height: 1; color: Theme.border }
                ListView {
                    objectName: "flashLog"
                    y: 33
                    width: parent.width
                    height: 160
                    leftMargin: 12
                    topMargin: 8
                    clip: true
                    model: dialog.model.log
                    ScrollBar.vertical: GScrollBar {}
                    delegate: Label {
                        required property var modelData
                        width: ListView.view.width - 24
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSm
                        textFormat: Text.StyledText
                        text: "<font color=\"" + Theme.contentMuted + "\">[" + modelData.time + "]</font> <font color=\""
                              + ({ Info: Theme.tw.blue[700], Success: Theme.tw.emerald[700], Warning: Theme.tw.amber[700],
                                   Error: Theme.tw.red[700] })[modelData.type] + "\">" + modelData.type + ":</font> "
                              + modelData.content
                    }
                    Label {
                        visible: dialog.model.log.length === 0
                        x: 12
                        text: qsTr("Waiting for logs...")
                        color: Theme.contentMuted
                    }
                }
            }
            GButton {
                objectName: "flashClose"
                visible: dialog.model.state === FlashModel.Complete || dialog.model.state === FlashModel.Error
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Close")
                variant: "primary"
                onClicked: dialog.close()
            }
        }
    }
}
