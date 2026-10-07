import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A tool form's switch: the model's option `key`, with its label.
RowLayout {
    property var model
    property string key
    property string label
    // Upstream's "Default is on" tooltip, where it gives one.
    property bool showDefault: true

    spacing: 6
    Label { text: parent.label; font.pixelSize: Theme.fontSm }
    GSwitch {
        objectName: (parent.model ? parent.model.objectName : "") + "_" + parent.key
        property string tooltip: !parent.showDefault || !parent.model || !parent.model.defaults
                                 || parent.model.defaults[parent.key] === undefined ? ""
                                 : parent.model.defaults[parent.key] ? qsTr("Default is on") : qsTr("Default is off")
        checked: parent.model ? !!parent.model.options[parent.key] : false
        onToggled: parent.model.setOption(parent.key, checked)
    }
}
