import QtQuick
import QtQuick.Layouts
import GSender

// The Stats page's card (StatCard): a rounded, bordered panel; its children
// stack in a padded column (Layout.* attached properties apply).
Rectangle {
    default property alias content: column.data
    property int padding: 16
    property alias spacing: column.spacing

    radius: Theme.radius
    color: Theme.dark ? Theme.surfaceRaised : "white"
    border.color: Theme.dark ? Theme.outline : Theme.gray[200]
    implicitHeight: column.implicitHeight + 2 * padding
    implicitWidth: column.implicitWidth + 2 * padding

    ColumnLayout {
        id: column
        x: parent.padding
        y: parent.padding
        width: parent.width - 2 * parent.padding
        height: parent.height - 2 * parent.padding
        spacing: 8
    }
}
