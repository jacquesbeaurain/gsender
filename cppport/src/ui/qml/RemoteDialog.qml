import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Wireless CNC Control (features/RemoteMode/index.tsx): the switch, the
// address - the recommended one first, loopback and virtual adapters marked
// unreachable - and the port at the left; the phone's QR code and the
// address to type at the right. Saving applies at once (upstream saved and
// restarted the application).
ModalDialog {
    id: dialog
    objectName: "remoteDialog"

    property RemoteModel model: RemoteModel {}
    property string ip: ""
    property int port: 8000
    property bool on: false
    property bool dirty: false
    readonly property var selected: model.entry(ip)
    readonly property bool addressMissing: model.addresses.length > 0 && selected.address === undefined

    function load() {
        model.refreshAddresses()
        ip = model.initialIp()
        port = model.savedPort
        on = model.enabled
        dirty = false
    }

    preferredWidth: 760
    padding: 20
    onOpened: load()

    // The Basic style's dim, not the other dialogs' black.
    Overlay.modal: Rectangle { color: Qt.alpha(dialog.palette.shadow, 0.5) }
    background: Panel {
        color: Theme.surfaceElevated
        border.color: Theme.outline
    }

    // For "Copy": QML has no clipboard of its own.
    TextEdit {
        id: clipboard
        visible: false
    }

    contentItem: ColumnLayout {
        spacing: 16
        RowLayout {
            Label {
                Layout.fillWidth: true
                text: qsTr("Wireless CNC Control")
                font.pixelSize: Theme.fontLg
                font.bold: true
                color: Theme.contentPrimary
            }
            GButton {
                objectName: "remoteClose"
                variant: "ghost"
                iconName: "MdClose"
                onClicked: dialog.close()
            }
        }
        RowLayout {
            spacing: 24
            // ---- the settings ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                spacing: 16
                RowLayout {
                    spacing: 12
                    Label {
                        text: qsTr("Enable Wireless Control")
                        font.bold: true
                        color: Theme.contentPrimary
                    }
                    GSwitch {
                        objectName: "remoteSwitch"
                        checked: dialog.on
                        onToggled: {
                            dialog.on = checked
                            dialog.dirty = true
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("In most cases you'll want the recommended address and the default port:")
                    color: Theme.contentSecondary
                }
                RowLayout {
                    spacing: 12
                    Label { text: qsTr("Addr:"); color: Theme.contentPrimary; Layout.preferredWidth: 48 }
                    GSelect {
                        id: addressCombo
                        objectName: "remoteAddress"
                        Layout.fillWidth: true
                        implicitHeight: Theme.touchTarget
                        model: dialog.model.addresses
                        textRole: "address"
                        displayText: dialog.ip + (dialog.selected.label ? "  (" + dialog.selected.label + ")" : "")
                        currentIndex: {
                            for (let i = 0; i < dialog.model.addresses.length; ++i)
                                if (dialog.model.addresses[i].address === dialog.ip)
                                    return i
                            return -1
                        }
                        onActivated: (index) => {
                            dialog.ip = dialog.model.addresses[index].address
                            dialog.dirty = true
                        }
                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: addressCombo.width
                            contentItem: ColumnLayout {
                                spacing: 0
                                Label {
                                    text: modelData.address + (modelData.recommended ? "  " + qsTr("Recommended") : "")
                                    font.bold: modelData.recommended
                                    color: modelData.usable ? Theme.contentPrimary : Theme.contentMuted
                                }
                                Label {
                                    text: modelData.label + " - " + modelData.iface
                                    font.pixelSize: Theme.fontXs
                                    color: Theme.contentMuted
                                }
                            }
                        }
                    }
                }
                // The address's note: gone, unreachable, or what it is.
                Label {
                    objectName: "remoteAddressNote"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontXs
                    textFormat: Text.StyledText
                    linkColor: Theme.primaryText
                    color: dialog.addressMissing || dialog.selected.usable === false ? Theme.orange[600] : Theme.contentMuted
                    text: dialog.addressMissing
                          ? qsTr("This computer no longer has <b>%1</b>.").arg(dialog.ip)
                            + (dialog.model.recommended ? " <a href=\"use\">" + qsTr("Use %1 instead").arg(dialog.model.recommended) + "</a>" : "")
                          : dialog.selected.usable === false
                            ? qsTr("Other devices can't reach gSender at this address - pick the recommended one to use wireless control.")
                            : (dialog.selected.label ? dialog.selected.label + " - " : "") + qsTr("other devices on your network use this address to reach gSender.")
                    onLinkActivated: {
                        dialog.ip = dialog.model.recommended
                        dialog.dirty = true
                    }
                }
                RowLayout {
                    spacing: 12
                    Label { text: qsTr("Port:"); color: Theme.contentPrimary; Layout.preferredWidth: 48 }
                    TextField {
                        objectName: "remotePort"
                        Layout.fillWidth: true
                        implicitHeight: Theme.touchTarget
                        text: String(dialog.port)
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: IntValidator { bottom: 0; top: 99999 }
                        onTextEdited: {
                            dialog.port = Number(text)
                            dialog.dirty = true
                        }
                    }
                }
                Label {
                    objectName: "remoteStatus"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSm
                    color: dialog.model.running ? Theme.green[700] : Theme.contentMuted
                    text: dialog.model.running
                          ? (dialog.model.clients === 1 ? qsTr("Serving the pendant at %1 - 1 device connected.").arg(dialog.model.url)
                                                      : qsTr("Serving the pendant at %1 - %2 devices connected.").arg(dialog.model.url).arg(dialog.model.clients))
                          : dialog.model.savedError
                            ? qsTr("Wireless control was switched off: gSender could not use the saved address.")
                            : qsTr("Wireless control is off.")
                }
                GButton {
                    objectName: "remoteSave"
                    Layout.fillWidth: true
                    variant: "primary"
                    text: qsTr("Save")
                    enabled: dialog.dirty
                    onClicked: {
                        const error = dialog.model.save(dialog.ip, dialog.port, dialog.on)
                        if (error) {
                            Backend.notify(error, "error")
                            return
                        }
                        Backend.notify(qsTr("Updated Wireless Control Settings"), "success")
                        dialog.dirty = false
                    }
                }
            }
            // ---- the QR code (QRCodeDisplay) ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 12
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Scan QR Code")
                    font.pixelSize: 24
                    color: Theme.primaryText
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Scan with your phone camera to control your CNC")
                    color: Theme.contentSecondary
                    font.pixelSize: Theme.fontSm
                }
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: 232
                    implicitHeight: 232
                    radius: 6
                    color: Theme.dark ? "white" : Theme.gray[900]
                    QrCode {
                        objectName: "remoteQr"
                        anchors.centerIn: parent
                        text: dialog.model.pendantUrl(dialog.ip, dialog.port)
                        size: 216
                    }
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("Or type the text below into a web browser for any other device:")
                    color: Theme.contentSecondary
                    font.pixelSize: Theme.fontSm
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8
                    Label {
                        objectName: "remoteAddressText"
                        text: dialog.ip + ":" + dialog.port
                        font.bold: true
                        color: Theme.primaryText
                    }
                    GButton {
                        text: qsTr("Copy")
                        onClicked: {
                            clipboard.text = dialog.ip + ":" + dialog.port
                            clipboard.selectAll()
                            clipboard.copy()
                            Backend.notify(qsTr("Copied link to clipboard"), "success")
                        }
                    }
                }
            }
        }
    }
}
