import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// An AlertDialog: title, question, Cancel and the action. open() it;
// `accepted` fires on the action.
Popup {
    id: dialog

    property string title
    property string message
    property string actionText: qsTr("Continue")
    property string actionVariant: "primary"
    property string cancelText: qsTr("Cancel")
    signal accepted()
    signal rejected()  // Cancel (not closing it otherwise)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 24
    width: Math.min(460, parent ? parent.width - 32 : 460)

    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceElevated : "white"
        border.color: Theme.outlineSubtle
    }

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: dialog.title
            font.pixelSize: Theme.fontLg
            font.bold: true
            color: Theme.contentPrimary
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            text: dialog.message
            font.pixelSize: Theme.fontSm
            color: Theme.contentMuted
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: 8
            spacing: 8
            GButton {
                objectName: "confirmCancel"
                visible: dialog.cancelText !== ""  // a notice has only its action
                text: dialog.cancelText
                variant: "outline"
                onClicked: {
                    dialog.close()
                    dialog.rejected()
                }
            }
            GButton {
                objectName: "confirmAction"
                text: dialog.actionText
                variant: dialog.actionVariant
                onClicked: {
                    dialog.close()
                    dialog.accepted()
                }
            }
        }
    }
}
