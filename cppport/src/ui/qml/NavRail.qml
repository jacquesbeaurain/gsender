import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The navigation rail (workspace/Sidebar, features/navbar): Carve, Stats,
// Tools, Config at the bottom, the rail's edge above them.
Item {
    id: rail
    objectName: "navRail"

    property int currentIndex: 0

    implicitWidth: 61

    // The edge above the entries (and the Helper toggle's place).
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: entries.top
        width: 2
        color: Theme.dark ? Theme.outline : Theme.gray[400]
    }

    // The Helper toggle (features/Helper/HelperToggle): grey and disabled
    // without a helper on offer; orange, tinted and bouncing with one, a tap
    // hides or shows it. It sits 169 px above the entries, as upstream's
    // flexible top section places it.
    Item {
        id: helperToggle
        objectName: "helperToggle"
        anchors.left: parent.left
        anchors.bottom: entries.top
        anchors.bottomMargin: 169
        width: 59
        height: 54
        property real bounce: 0
        transform: Translate { y: helperToggle.bounce }

        SequentialAnimation on bounce {
            running: Backend.helperActive && !Theme.reducedMotion
            loops: Animation.Infinite
            NumberAnimation { to: -14; duration: 500; easing.type: Easing.OutQuad }
            NumberAnimation { to: 0; duration: 500; easing.type: Easing.InQuad }
            onRunningChanged: if (!running) helperToggle.bounce = 0
        }
        Rectangle {
            anchors.fill: parent
            radius: 12
            visible: Backend.helperActive
            color: Qt.rgba(0xe6 / 255, 0xc8 / 255, 0xa5 / 255, 0.3)
            border.color: Theme.dark ? Theme.outline : Theme.gray[200]
            border.width: Theme.hairline
        }
        Column {
            anchors.centerIn: parent
            spacing: 2
            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "RiSpeakLine"
                width: 34.6
                height: 31.5
                color: Backend.helperActive ? Theme.orange[600] : Theme.gray[400]
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Backend.helperActive ? Backend.helperTitle : qsTr("Helper")
                font.pixelSize: Theme.fontXs
                color: Theme.dark ? Theme.contentMuted : Theme.gray[600]
                elide: Text.ElideRight
                width: Math.min(implicitWidth, helperToggle.width - 4)
            }
        }
        TapHandler {
            enabled: Backend.helperActive
            onTapped: Backend.toggleHelper()
        }
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
                // Upstream: the Carve entry carries its 76 px picture.
                Layout.preferredHeight: modelData.image !== "" ? 142 : 86
                label: modelData.label
                icon: modelData.icon
                image: modelData.image
                active: rail.currentIndex === index
                onClicked: rail.currentIndex = index
            }
        }
    }
}
