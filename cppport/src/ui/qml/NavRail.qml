import QtQuick
import QtQuick.Layouts
import GSender

// The navigation rail (workspace/Sidebar, features/navbar): Carve, Stats,
// Tools, Config at the bottom, the rail's edge above them.
Item {
    id: rail
    objectName: "navRail"

    property int currentIndex: 0

    implicitWidth: 70

    // The edge above the entries (and the Helper toggle's place).
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: entries.top
        width: 2
        color: Theme.dark ? Theme.outline : Theme.gray[400]
    }

    ColumnLayout {
        id: entries
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 0
        Repeater {
            model: [
                { label: qsTr("Carve"), image: "image://icon/Carve", icon: "" },
                { label: qsTr("Stats"), image: "", icon: "IoSpeedometerOutline" },
                { label: qsTr("Tools"), image: "", icon: "RiToolsFill" },
                { label: qsTr("Config"), image: "", icon: "FaTasks" }
            ]
            NavButton {
                required property var modelData
                required property int index
                objectName: "nav" + modelData.label
                Layout.fillWidth: true
                label: modelData.label
                icon: modelData.icon
                image: modelData.image
                active: rail.currentIndex === index
                onClicked: rail.currentIndex = index
            }
        }
    }
}
