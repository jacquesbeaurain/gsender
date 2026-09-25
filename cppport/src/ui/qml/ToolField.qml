import QtQuick
import QtQuick.Layouts
import GSender

// A tool form's number: the model's option `key`, committed when edited;
// upstream's large centred blue figures.
NumberField {
    property var model
    property string key

    objectName: (model ? model.objectName : "") + "_" + key
    Layout.fillWidth: true
    value: model && model.options[key] !== undefined ? model.options[key] : 0
    horizontalAlignment: TextInput.AlignHCenter
    color: Theme.blue[500]
    font.pixelSize: Theme.fontLg
    onCommitted: (text) => { if (text !== "" && !isNaN(Number(text))) model.setOption(key, Number(text)) }
}
