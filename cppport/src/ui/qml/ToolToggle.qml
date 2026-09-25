import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A tool form's switch: the model's option `key`, with its label.
RowLayout {
    property var model
    property string key
    property string label

    spacing: 6
    Label { text: parent.label; font.pixelSize: Theme.fontSm; color: Theme.contentPrimary }
    GSwitch {
        objectName: (parent.model ? parent.model.objectName : "") + "_" + parent.key
        checked: parent.model ? !!parent.model.options[parent.key] : false
        onToggled: parent.model.setOption(parent.key, checked)
    }
}
