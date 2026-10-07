import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A Stats card's empty state (EmptyDataPlaceholder, EmptyJobList): a
// text-6xl icon over the message, centred.
ColumnLayout {
    property string icon
    property string text
    spacing: 8
    Icon {
        Layout.alignment: Qt.AlignHCenter
        name: parent.icon
        size: 60
        color: Theme.contentBody
    }
    Label {
        Layout.alignment: Qt.AlignHCenter
        text: parent.text
        color: Theme.contentBody
    }
}
