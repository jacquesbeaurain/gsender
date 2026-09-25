import QtQuick

// A widget's frame (components/Widget/Content): a rounded, bordered panel.
// Children go into its padded content area.
Rectangle {
    default property alias content: area.data
    property int padding: 8

    color: Theme.widget
    border.color: Theme.widgetBorder
    border.width: 1
    radius: Theme.radius

    Item {
        id: area
        anchors.fill: parent
        anchors.margins: parent.padding
    }
}
