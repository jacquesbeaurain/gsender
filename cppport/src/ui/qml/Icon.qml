import QtQuick

// An icon of resources/icons (upstream's react-icons) in a colour.
Image {
    property string name
    property color color: Theme.contentPrimary

    source: name ? Theme.icon(name, color) : ""
    sourceSize: Qt.size(width, height)
    width: 24
    height: 24
    fillMode: Image.PreserveAspectFit
    smooth: true
}
