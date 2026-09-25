import QtQuick
import QtQuick.Controls.Basic
import GSender

// A number input (ControlledInput type="number"): shows `value` until
// edited; Enter or leaving the field commits the text (`committed`), Escape
// restores it. Tapping it selects everything, ready to type over.
TextField {
    id: field

    property real value
    property int decimals: 3
    property string suffix
    // Shown as it is instead of the formatted value (the DRO's own text).
    property var display: undefined
    signal committed(string text)
    property bool edited: false

    function show() {
        if (display !== undefined) {
            text = display
            return
        }
        // Trailing zeros after the point go (not a whole number's own).
        const fixed = Number(value).toFixed(decimals)
        text = fixed.includes(".") ? fixed.replace(/\.?0+$/, "") : fixed
    }

    implicitHeight: 40
    horizontalAlignment: TextInput.AlignRight
    inputMethodHints: Qt.ImhFormattedNumbersOnly
    color: Theme.contentPrimary
    selectByMouse: true
    rightPadding: suffixLabel.visible ? suffixLabel.implicitWidth + 12 : 8
    leftPadding: 8
    font.pixelSize: Theme.fontSm

    background: Rectangle {
        radius: Theme.radiusSmall
        color: !field.enabled ? Theme.surfaceDisabled : (Theme.dark ? Theme.surfaceSunken : "white")
        border.color: field.activeFocus ? Theme.ring : Theme.outline
        border.width: field.activeFocus ? 2 : 1
    }

    Label {
        id: suffixLabel
        visible: field.suffix !== ""
        text: field.suffix
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: Theme.fontXs
        color: Theme.contentMuted
    }

    Component.onCompleted: show()
    onValueChanged: if (!activeFocus) show()
    onDisplayChanged: if (!activeFocus) show()
    onTextEdited: edited = true
    onActiveFocusChanged: {
        if (activeFocus) {
            edited = false
            selectAll()
        } else {
            // Only what was typed is committed.
            if (edited)
                committed(text)
            edited = false
            show()
        }
    }
    onAccepted: focus = false
    Keys.onEscapePressed: {
        edited = false
        show()
        focus = false
    }
}
