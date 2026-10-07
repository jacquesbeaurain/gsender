import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// A button's action in the Gamepad tool's table (ButtonActionsTable's
// render.action): a + to add one, else its title with Edit and
// Remove. Green while the button runs it (pressed, with or without the
// 2nd-action button as it needs).
Rectangle {
    id: cell

    property string actionId
    property string title
    property bool highlighted: false
    property string namePrefix
    signal edit()
    signal clear()

    objectName: namePrefix

    implicitHeight: 44
    radius: Theme.radiusSmall
    color: highlighted ? Theme.green[500] : "transparent"
    border.color: Theme.dark ? Theme.outline : Theme.gray[300]
    border.width: Theme.hairline

    // None yet: the +.
    Item {
        anchors.fill: parent
        visible: cell.actionId === ""
        Icon {
            anchors.centerIn: parent
            name: "FaPlus"
            color: Theme.contentMuted
            width: 16; height: 16
        }
        TapHandler { onTapped: cell.edit() }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 4
        visible: cell.actionId !== ""
        spacing: 4
        Label {
            Layout.fillWidth: true
            text: cell.title
            elide: Text.ElideRight
            color: cell.highlighted ? "white" : Theme.contentPrimary
        }
        GButton {
            objectName: cell.namePrefix + "Edit"
            variant: "ghost"
            iconName: "FaEdit"
            iconSize: 16
            implicitWidth: Theme.touchTarget
            onClicked: cell.edit()
        }
        GButton {
            objectName: cell.namePrefix + "Clear"
            variant: "ghost"
            iconName: "FaTrash"
            iconSize: 16
            implicitWidth: Theme.touchTarget
            onClicked: cell.clear()
        }
    }
}
