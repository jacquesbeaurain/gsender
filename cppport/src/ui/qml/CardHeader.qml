import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A Stats card's header (CardHeader) and its link (StatLink: "More ›").
RowLayout {
    id: header
    property string title
    property string linkLabel
    signal linkActivated()

    spacing: 8
    Label {
        text: header.title
        font.pixelSize: Theme.fontXl
        color: Theme.primaryText
        Layout.fillWidth: true
    }
    Label {
        visible: header.linkLabel !== ""
        text: header.linkLabel + " ›"
        font.pixelSize: Theme.fontSm
        color: Theme.primaryText
        TapHandler { onTapped: header.linkActivated() }
        Layout.minimumHeight: Theme.touchTarget
        verticalAlignment: Text.AlignVCenter
    }
}
