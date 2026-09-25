import QtQuick
import QtQuick.Layouts
import GSender

// Coolant (features/Coolant): Mist (M7), Flood (M8) and Off (M9), lit while
// the board has them on; on an idle machine with no job running.
Item {
    id: tab
    objectName: "coolantTab"

    property CoolantModel model: CoolantModel {}

    RowLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 16, 480)
        spacing: 8
        ActiveStateButton {
            objectName: "coolantMist"
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            text: qsTr("Mist")
            iconName: "FaShower"
            active: Backend.connected && tab.model.mistActive
            enabled: tab.model.canClick
            onClicked: tab.model.mist()
        }
        ActiveStateButton {
            objectName: "coolantFlood"
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            text: qsTr("Flood")
            iconName: "FaWater"
            active: Backend.connected && tab.model.floodActive
            enabled: tab.model.canClick
            onClicked: tab.model.flood()
        }
        ActiveStateButton {
            objectName: "coolantOff"
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            text: qsTr("Off")
            iconName: "FaBan"
            enabled: tab.model.canClick
            onClicked: tab.model.off()
        }
    }
}
