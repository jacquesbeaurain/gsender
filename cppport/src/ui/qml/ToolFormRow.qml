import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A tool form's row (InputArea): the label, then its inputs.
RowLayout {
    property string label
    default property alias inputs: box.data

    Layout.fillWidth: true
    spacing: 12
    Label {
        text: parent.label
        font.pixelSize: Theme.fontSm
        color: Theme.contentPrimary
        wrapMode: Text.Wrap
        Layout.preferredWidth: 150
    }
    RowLayout { id: box; Layout.fillWidth: true; spacing: 8 }
}
