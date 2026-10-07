import QtQuick

// An icon of resources/icons (upstream's react-icons) in a colour, `size`
// square (set width and height instead for another shape).
Image {
    property string name
    property color color: Theme.contentPrimary
    property real size: 24

    source: name ? Theme.icon(name, color) : ""
    sourceSize: Qt.size(width, height)
    width: size
    height: size
    fillMode: Image.PreserveAspectFit
    smooth: true
}
