import QtQuick
import QtQuick.Controls.Basic
import GSender

// The app's select (shadcn's Select as upstream's Config and tools use it): a
// bordered field with the value and a chevron; a popup of options.
ComboBox {
    id: select

    property string tooltip   // HoverTips; a select is a button: the hand
    property string tooltipSide: "top"
    readonly property bool clickable: true

    implicitHeight: 40
    font.pixelSize: Theme.fontBase

    background: Panel {
        radius: 6
        color: !select.enabled ? Theme.surfaceDisabled : Theme.surfaceRaised
        border.color: select.visualFocus || select.popup.visible ? Theme.ring : Theme.outline
    }
    indicator: Icon {
        x: select.width - width - 12
        y: (select.height - height) / 2
        name: "LuChevronDown"
        size: 16
        color: Theme.contentMuted
    }
    contentItem: Label {
        leftPadding: 12
        rightPadding: 36
        text: select.displayText
        font: select.font
        color: select.enabled ? Theme.contentPrimary : Theme.contentDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    delegate: ItemDelegate {
        required property int index
        width: select.width
        height: 40
        highlighted: select.highlightedIndex === index
        contentItem: Label {
            text: select.textAt(index)
            font: select.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: parent.highlighted ? (Theme.dark ? Theme.surfaceHover : Theme.blue[100]) : "transparent"
        }
    }
    popup: Popup {
        y: select.height + 2
        width: select.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, 320)
        padding: 1
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: select.popup.visible ? select.delegateModel : null
            currentIndex: select.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Panel {
            radius: 6
            color: Theme.surfaceElevated
            border.color: Theme.outline
        }
    }
}
