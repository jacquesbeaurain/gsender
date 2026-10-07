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
    property color textColor: foreground
    property int iconSize: 20
    property int fontSize: Theme.fontBase
    property bool mono: false
    property bool bold: false

    // Shown on hovering, disabled or not (HoverTips), on `tooltipSide`.
    property string tooltip
    property string tooltipSide: "top"
    property int tooltipDelay: 0   // 0: upstream Tooltip's 1.5 s

    // Each variant's fill, border and text, and its hovered fill and text:
    // upstream's Button always adds shadcn's ghost hover (bg-accent,
    // text-accent-foreground), which wins over the variant's colours except
    // primary's hover:bg-blue-700, secondary's and outline's
    // hover:bg-gray-200 (surface-raised in dark) and the dark: text colours.
    readonly property var variants: ({
        primary: { bg: Theme.primary, border: Theme.primary, fg: "white",
                   hoverBg: Theme.blue[700], hoverFg: Theme.contentPrimary },
        secondary: { bg: Theme.surfaceRaised, border: Theme.robin[500], fg: Theme.contentSoft,
                     hoverBg: Theme.dark ? Theme.surfaceRaised : Theme.gray[200], hoverFg: Theme.contentPrimary },
        alt: { bg: Theme.robin[500], border: Theme.robin[500], fg: "white",
               hoverBg: Theme.accent, hoverFg: Theme.contentPrimary },
        warning: { bg: Qt.rgba(0xbb / 255, 0x6a / 255, 0x0c / 255, 0.9), border: "#c9883d", fg: "white",
                   hoverBg: Theme.accent, hoverFg: Theme.contentPrimary },
        error: { bg: Theme.red[500], border: Theme.red[700], fg: "white",
                 hoverBg: Theme.accent, hoverFg: Theme.contentPrimary },
        success: { bg: Theme.green[500], border: Theme.green[700], fg: "white",
                   hoverBg: Theme.accent, hoverFg: Theme.contentPrimary },
        outline: { bg: Theme.surfaceRaised, border: Theme.robin[500], fg: Theme.contentPrimary,
                   hoverBg: Theme.dark ? Theme.surfaceRaised : Theme.gray[200], hoverFg: Theme.contentPrimary },
        ghost: { bg: "transparent", border: "transparent", fg: Theme.dark ? Theme.contentSecondary : Theme.gray[600],
                 hoverBg: Theme.accent, hoverFg: Theme.dark ? Theme.contentSecondary : Theme.contentPrimary }
    })
    readonly property var colors: variants[variant] || variants.secondary
    readonly property bool hoverLook: enabled && hovered
    readonly property color foreground: !enabled ? (Theme.dark ? Theme.contentDisabled : Theme.gray[600])
                                      : hoverLook ? colors.hoverFg : colors.fg

    implicitHeight: Theme.touchTarget
    implicitWidth: Math.max(Theme.touchTarget, contentRow.implicitWidth + 24)
    focusPolicy: Qt.StrongFocus
    // A mouse hovers whatever the platform's hover-effects hint (a finger never does).
    hoverEnabled: true
    // shadcn's disabled:opacity-50, on top of the disabled colours; upstream's
    // hover:opacity-90.
    opacity: !enabled ? 0.5 : hovered ? 0.9 : 1

    background: Rectangle {
        radius: 4
        color: !button.enabled ? Theme.buttonDisabled
             : button.hoverLook ? button.colors.hoverBg
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
            visible: button.visualFocus && Theme.focusRings
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
                size: button.iconSize
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                visible: button.text !== ""
                text: button.text
                color: button.textColor
                font.pixelSize: button.fontSize
                font.family: button.mono ? Theme.monoFont : font.family
                font.bold: button.bold
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
