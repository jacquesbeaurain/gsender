import QtQuick
import QtQuick.Controls.Basic
import GSender

// A navigation rail entry (NavbarLink): an icon over its label; the page
// shown is tinted blue with a gradient running into the page.
Item {
    id: link

    property string label
    property string icon       // resources/icons name, drawn in the state's colour
    property url image         // or a picture as it is (Carve)
    property bool active: false
    signal clicked()

    implicitHeight: 92

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 4
        visible: link.active
        radius: 5
        border.width: 2
        border.color: Theme.dark ? Theme.outline : Theme.gray[400]
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.4; color: Theme.dark ? Qt.rgba(59 / 255, 130 / 255, 246 / 255, 0.2) : Qt.rgba(121 / 255, 170 / 255, 216 / 255, 0.3) }
            GradientStop { position: 1.0; color: Theme.dark ? Theme.surfaceRaised : "white" }
        }
        // The page side stays open.
        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 2
            anchors.bottomMargin: 2
            width: 2
            color: Theme.dark ? Theme.surfaceRaised : "white"
        }
    }
    // The rail's edge beside the entries that are not shown.
    Rectangle {
        visible: !link.active
        anchors.right: parent.right
        width: 2
        height: parent.height
        color: Theme.dark ? Theme.outline : Theme.gray[400]
    }

    Column {
        anchors.centerIn: parent
        spacing: 2
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: link.icon !== ""
            name: link.icon
            color: link.active ? Theme.blue[600] : (Theme.dark ? Theme.contentMuted : Theme.gray[600])
            width: 26
            height: 26
        }
        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: link.image.toString() !== ""
            source: link.image
            sourceSize.height: 48
            height: 48
            fillMode: Image.PreserveAspectFit
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: link.label
            font.pixelSize: Theme.fontSm
            color: link.active ? Theme.blue[600] : (Theme.dark ? Theme.contentMuted : Theme.gray[500])
        }
    }

    TapHandler {
        onTapped: link.clicked()
    }
}
