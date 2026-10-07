import QtQuick
import QtQuick.Layouts
import GSender

// A tool form's number: the model's option `key`, committed when edited;
// upstream's large centred blue figures. Its tooltip gives the default
// (upstream's "Default is 25 mm"), in `defaultUnit`: the suffix, else the
// model's units.
NumberField {
    property var model
    property string key
    property string defaultUnit: suffix !== "" ? suffix : (model && model.units !== undefined ? model.units : "")
    readonly property var defaultValue: model && model.defaults ? model.defaults[key] : undefined

    objectName: (model ? model.objectName : "") + "_" + key
    tooltip: defaultValue === undefined ? ""
             : qsTr("Default is %1").arg(defaultUnit === "%" ? defaultValue + "%"
                                         : defaultValue + (defaultUnit ? " " + defaultUnit : ""))
    Layout.fillWidth: true
    value: model && model.options[key] !== undefined ? model.options[key] : 0
    horizontalAlignment: TextInput.AlignHCenter
    color: Theme.primaryText
    font.pixelSize: Theme.fontLg
    onCommitted: (text) => { if (text !== "" && !isNaN(Number(text))) model.setOption(key, Number(text)) }
}
