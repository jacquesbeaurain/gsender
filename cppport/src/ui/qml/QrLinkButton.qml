import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The QR code beside a help link (Wizard/SecondaryContentPanel, Helper):
// a small QR button that pops up "Scan QR Code" with the link's code, so
// the resource opens on a phone next to the machine.
Item {
    id: root

    property string url
    property string level: "H"

    implicitWidth: Theme.touchTarget
    implicitHeight: Theme.touchTarget
    visible: url !== ""

    Rectangle {
        anchors.centerIn: parent
        width: 32
        height: 32
        radius: Theme.radiusSmall
        color: tap.pressed ? Theme.surfaceHover : "transparent"
        Icon {
            anchors.centerIn: parent
            name: "LuQrCode"
            color: Theme.contentSecondary
            width: 20
            height: 20
        }
    }
    TapHandler {
        id: tap
        onTapped: popup.open()
    }

    Popup {
        id: popup
        objectName: "qrPopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        padding: 16
        modal: true
        dim: false
        background: Rectangle {
            radius: Theme.radius
            color: Theme.dark ? Theme.surfaceElevated : "white"
            border.color: Theme.outline
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Scan QR Code")
                font.pixelSize: Theme.fontSm
                font.bold: true
                color: Theme.contentPrimary
            }
            QrCode {
                Layout.alignment: Qt.AlignHCenter
                text: root.url
                level: root.level
                size: 180
            }
        }
    }
}
