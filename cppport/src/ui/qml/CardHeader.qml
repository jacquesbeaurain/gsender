import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A Stats card's header (CardHeader): the title in text-2xl blue-600 with
// pb-2, and its link (StatLink): a small outlined button with an arrow.
RowLayout {
    id: header
    property string title
    property string linkLabel
    signal linkActivated()

    spacing: 8
    Label {
        text: header.title
        font.pixelSize: 24
        color: Theme.primaryText
        Layout.fillWidth: true
        Layout.bottomMargin: 8
    }
    GButton {
        visible: header.linkLabel !== ""
        objectName: "statLink_" + header.linkLabel
        variant: "outline"
        text: header.linkLabel
        iconName: "GoArrowUpRight"
        iconSize: 16
        fontSize: Theme.fontSm
        bold: true
        implicitHeight: 32
        Layout.bottomMargin: 8
        onClicked: header.linkActivated()
    }
}
