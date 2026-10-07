import QtQuick

// A widget's frame (components/Widget/Content): a rounded, bordered panel.
// Children go into its padded content area.
Panel {
    default property alias content: area.data
    property int padding: 8

    color: Theme.secondary
    border.color: Theme.outline

    Item {
        id: area
        anchors.fill: parent
        anchors.margins: parent.padding
    }
}
