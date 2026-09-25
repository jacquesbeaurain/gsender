import QtQuick
import QtQuick.Controls.Basic
import GSender

// Upstream's Button (components/Button): the variants primary, secondary,
// alt, warning, error, success, outline, ghost; `active` for a pressed-in
// toggle; disabled greyed. An optional icon before the text. At least the
// touch target's height.
AbstractButton {
    id: button

    property string variant: "secondary"
    property bool active: false
    property string iconName
    property color iconColor: foreground
    property int iconSize: 20
    property int fontSize: Theme.fontBase
    property bool mono: false
    property bool bold: false

    readonly property var variants: ({
        primary: { bg: Theme.blue[500], border: Theme.blue[500], fg: "white" },
        secondary: { bg: Theme.dark ? Theme.surfaceRaised : "white", border: Theme.dark ? Theme.outline : "#689AC9",
                     fg: Theme.dark ? Theme.contentSecondary : Theme.gray[600] },
        alt: { bg: "#689AC9", border: "#689AC9", fg: "white" },
        warning: { bg: Qt.rgba(0xbb / 255, 0x6a / 255, 0x0c / 255, 0.9), border: "#c9883d", fg: "white" },
        error: { bg: Theme.red[500], border: Theme.red[700], fg: "white" },
        success: { bg: Theme.green[500], border: Theme.green[700], fg: "white" },
        outline: { bg: Theme.dark ? Theme.surfaceRaised : "white", border: Theme.dark ? Theme.outline : "#689AC9",
                   fg: Theme.contentPrimary },
        ghost: { bg: "transparent", border: "transparent", fg: Theme.dark ? Theme.contentSecondary : Theme.gray[600] }
    })
    readonly property var colors: variants[variant] || variants.secondary
    readonly property color foreground: !enabled ? (Theme.dark ? Theme.contentDisabled : Theme.gray[500]) : colors.fg

    implicitHeight: Theme.touchTarget
    implicitWidth: Math.max(Theme.touchTarget, contentRow.implicitWidth + 24)
    focusPolicy: Qt.StrongFocus

    background: Rectangle {
        radius: 4
        color: !button.enabled ? (Theme.dark ? Theme.surfaceRaised : Theme.gray[300])
             : button.active ? (Theme.dark ? Theme.surfaceActive : Theme.gray[200])
             : button.colors.bg
        border.color: !button.enabled ? (Theme.dark ? Theme.outlineDisabled : Theme.gray[400]) : button.colors.border
        border.width: button.variant === "ghost" ? 0 : 1
        opacity: button.pressed ? 0.8 : 1
        // The pressed-in shadow of `active` and a press.
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: button.pressed || button.active
            color: Qt.rgba(59 / 255, 130 / 255, 246 / 255, 0.1)
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.color: Theme.ring
            border.width: 2
            visible: button.visualFocus
        }
    }

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: 6
            Icon {
                visible: button.iconName !== ""
                name: button.iconName
                color: button.iconColor
                width: button.iconSize
                height: button.iconSize
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                visible: button.text !== ""
                text: button.text
                color: button.foreground
                font.pixelSize: button.fontSize
                font.family: button.mono ? Theme.monoFont : font.family
                font.bold: button.bold
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
