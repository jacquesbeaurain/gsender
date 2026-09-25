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
        color: Theme.blue[500]
        Layout.fillWidth: true
    }
    Label {
        visible: header.linkLabel !== ""
        text: header.linkLabel + " ›"
        font.pixelSize: Theme.fontSm
        color: Theme.blue[500]
        TapHandler { onTapped: header.linkActivated() }
        Layout.minimumHeight: Theme.touchTarget
        verticalAlignment: Text.AlignVCenter
    }
}
