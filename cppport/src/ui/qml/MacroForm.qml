import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Add Macro / Edit Macro (features/Macros/MacroForm): the name, the G-code -
// with the Variables list inserting at the cursor - and the description.
Popup {
    id: form
    objectName: "macroForm"

    property MacrosModel model
    property string macroId: ""   // empty: a new macro

    function openFor(id) {
        macroId = id || ""
        const m = macroId ? model.macro(macroId) : ({})
        nameField.text = m.name || ""
        contentField.text = m.content || ""
        descriptionField.text = m.description || ""
        error.text = ""
        open()
        nameField.forceActiveFocus()
    }
    function submit() {
        const failure = macroId
            ? model.update(macroId, nameField.text, contentField.text, descriptionField.text)
            : model.add(nameField.text, contentField.text, descriptionField.text)
        if (failure) {
            error.text = failure
            return
        }
        Backend.notify(macroId ? qsTr("Updated macro '%1'").arg(nameField.text.trim()) : qsTr("Added New Macro"),
                       "success")
        close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 24
    width: Math.min(640, parent ? parent.width - 32 : 640)
    height: Math.min(implicitHeight, parent ? parent.height - 32 : implicitHeight)
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
    background: Rectangle {
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceElevated : "white"
        border.color: Theme.outlineSubtle
    }

    component FieldBox: Rectangle {
        property Item field
        radius: Theme.radiusSmall
        color: Theme.dark ? Theme.surfaceSunken : "white"
        border.color: field && field.activeFocus ? Theme.ring : Theme.outline
        border.width: field && field.activeFocus ? 2 : 1
    }

    contentItem: ColumnLayout {
        spacing: 10
        Label {
            text: form.macroId ? qsTr("Edit Macro") : qsTr("Add Macro")
            font.pixelSize: Theme.fontLg
            font.bold: true
            color: Theme.contentPrimary
        }
        Label { text: qsTr("Name"); color: Theme.contentPrimary }
        TextField {
            id: nameField
            objectName: "macroName"
            Layout.fillWidth: true
            implicitHeight: 40
            maximumLength: 128
            color: Theme.contentPrimary
            background: FieldBox { field: nameField }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("G-code"); color: Theme.contentPrimary; Layout.fillWidth: true }
            ComboBox {
                id: variables
                objectName: "macroVariables"
                Layout.preferredWidth: 280
                displayText: qsTr("Variables")
                model: form.model ? form.model.variables : []
                textRole: "text"
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    contentItem: Column {
                        Label {
                            text: modelData.group
                            visible: index === 0 || variables.model[index - 1].group !== modelData.group
                            font.pixelSize: Theme.fontXs
                            color: Theme.contentMuted
                        }
                        Label {
                            text: modelData.text.trim()
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.fontSm
                            color: Theme.contentPrimary
                        }
                    }
                }
                // insertAtCaret.
                onActivated: (index) => {
                    const text = model[index].text
                    const at = contentField.cursorPosition
                    contentField.insert(at, text)
                    contentField.cursorPosition = at + text.length
                    contentField.forceActiveFocus()
                }
            }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 220
            Layout.minimumHeight: 100
            TextArea {
                id: contentField
                objectName: "macroContent"
                font.family: Theme.monoFont
                font.pixelSize: Theme.fontSm
                color: Theme.contentPrimary
                wrapMode: TextEdit.NoWrap
                background: FieldBox { field: contentField }
            }
        }
        Label { text: qsTr("Macro Description"); color: Theme.contentPrimary }
        TextArea {
            id: descriptionField
            objectName: "macroDescription"
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            wrapMode: TextEdit.Wrap
            color: Theme.contentPrimary
            background: FieldBox { field: descriptionField }
            onTextChanged: if (length > 128) remove(128, length)
        }
        Label {
            id: error
            visible: text !== ""
            color: Theme.red[500]
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            GButton {
                objectName: "macroSubmit"
                variant: "primary"
                text: form.macroId ? qsTr("Update Macro") : qsTr("Add New Macro")
                onClicked: form.submit()
            }
            GButton {
                objectName: "macroCancel"
                text: qsTr("Cancel")
                onClicked: form.close()
            }
        }
    }
}
