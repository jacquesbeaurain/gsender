import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The DRO (features/DRO): the units badge; Go To, the corner buttons and
// Park; per axis zero (or home), the work position - tap to type one -, the
// machine position and go to zero; Zero all, the homing switch and Home, XY.
Item {
    id: dro
    objectName: "dro"

    property DroModel model: DroModel {}

    implicitHeight: column.implicitHeight

    // Zero confirmations ("Warn when setting zero").
    ConfirmDialog {
        id: confirmZero
        objectName: "confirmZero"
        property string axis   // "" for all
        title: axis ? qsTr("Zero %1 Axis").arg(axis) : qsTr("Zero All Axes")
        message: axis ? qsTr("Are you sure you want to zero the %1 axis?").arg(axis)
                      : qsTr("Are you sure you want to zero all axes?")
        onAccepted: axis ? dro.model.axisButton(axis) : dro.model.zeroAll()
    }
    function zero(axis) {
        if (dro.model.warnZero && !dro.model.homingMode) {
            confirmZero.axis = axis
            confirmZero.open()
        } else if (axis) {
            dro.model.axisButton(axis)
        } else {
            dro.model.zeroAll()
        }
    }

    // The units badge (UnitBadge): the top-left tab; a tap switches.
    Rectangle {
        id: unitBadge
        objectName: "unitBadge"
        z: 1
        x: -8
        y: -8
        width: badgeText.implicitWidth + 16
        height: badgeText.implicitHeight + 12
        color: Theme.dark ? Theme.surfaceElevated : Theme.gray[300]
        topLeftRadius: Theme.radius
        bottomRightRadius: Theme.radius
        Label {
            id: badgeText
            anchors.centerIn: parent
            text: qsTr("Units:\n%1").arg(dro.model.units)
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontXs
            font.weight: Font.DemiBold
            color: Theme.dark ? Theme.contentMuted : Theme.gray[600]
        }
        TapHandler { onTapped: dro.model.toggleUnits() }
    }

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 4

        // Go To, the corners and Park.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 12
            GButton {
                objectName: "goToButton"
                iconName: "FaPaperPlane"
                enabled: dro.model.canClick
                onClicked: goTo.open()
                GoToPopup {
                    id: goTo
                    model: dro.model
                    y: parent.height + 4
                }
            }
            Grid {
                objectName: "corners"
                visible: dro.model.homingEnabled
                columns: 2
                spacing: 4
                Repeater {
                    model: ["BackLeft", "BackRight", "FrontLeft", "FrontRight"]
                    CornerButton {
                        required property string modelData
                        required property int index
                        objectName: "corner" + modelData
                        corner: index
                        enabled: dro.model.canClick && dro.model.homed
                        onClicked: dro.model.goToCorner(modelData)
                    }
                }
            }
            GButton {
                objectName: "parkButton"
                visible: dro.model.homingEnabled
                iconName: "RiParkingFill"
                iconSize: 16
                text: qsTr("Park")
                enabled: dro.model.canClick && dro.model.homed
                onClicked: dro.model.park()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Label {
                text: dro.model.homingMode ? qsTr("Home") : qsTr("Zero")
                font.pixelSize: Theme.fontSm
                color: Theme.contentMuted
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("Go to")
                font.pixelSize: Theme.fontSm
                color: Theme.contentMuted
            }
        }

        // Repeated by count, so the rows (and a field being typed in) stay
        // while the positions update.
        Repeater {
            model: dro.model.rows.length
            Rectangle {
                id: row
                required property int index
                readonly property var modelData: dro.model.rows[index] || ({})
                objectName: "axisRow" + modelData.label
                Layout.fillWidth: true
                implicitHeight: Theme.touchTarget + 4
                radius: Theme.radiusSmall
                color: "transparent"
                border.color: Theme.dark ? Theme.outline : Theme.gray[200]

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 2
                    spacing: 4
                    GButton {
                        objectName: "zero" + row.modelData.label
                        variant: dro.model.homingMode ? "alt" : "secondary"
                        text: dro.model.homingMode ? "H" + row.modelData.label : row.modelData.label + "0"
                        mono: true
                        bold: true
                        fontSize: Theme.fontXl
                        enabled: row.modelData.enabled
                        Layout.preferredWidth: 64
                        Layout.fillHeight: true
                        onClicked: dro.zero(row.modelData.axis)
                    }
                    NumberField {
                        objectName: "work" + row.modelData.label
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        horizontalAlignment: TextInput.AlignHCenter
                        display: row.modelData.work
                        enabled: row.modelData.enabled
                        font.pixelSize: Theme.fontXl
                        font.bold: true
                        font.family: Theme.monoFont
                        color: Theme.blue[500]
                        background: Rectangle {
                            radius: 4
                            color: parent.activeFocus ? (Theme.dark ? Theme.surfaceSunken : "white") : "transparent"
                            border.color: parent.activeFocus ? Theme.ring : "transparent"
                        }
                        onCommitted: (value) => dro.model.setWorkPosition(row.modelData.axis, value)
                    }
                    Label {
                        objectName: "machine" + row.modelData.label
                        text: row.modelData.machine
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.fontSm
                        color: Theme.gray[400]
                        horizontalAlignment: Text.AlignHCenter
                        Layout.preferredWidth: 80
                    }
                    GButton {
                        objectName: "goZero" + row.modelData.label
                        variant: "alt"
                        text: row.modelData.label
                        mono: true
                        fontSize: Theme.fontLg
                        enabled: row.modelData.gotoEnabled
                        Layout.preferredWidth: 52
                        Layout.fillHeight: true
                        onClicked: dro.model.goToZero(row.modelData.axis)
                    }
                }
            }
        }

        // Zero all, homing, XY.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8
            GButton {
                objectName: "zeroAll"
                text: qsTr("Zero")
                iconName: "VscTarget"
                enabled: dro.model.canClick
                onClicked: dro.zero("")
            }
            Item { Layout.fillWidth: true }
            Switch {
                objectName: "homingSwitch"
                visible: dro.model.singleAxisHoming
                enabled: dro.model.canClick
                checked: dro.model.homingMode
                onToggled: dro.model.homingMode = checked
            }
            GButton {
                objectName: "homeButton"
                visible: dro.model.homingEnabled
                variant: "primary"
                text: qsTr("Home")
                enabled: dro.model.canClick
                onClicked: dro.model.home()
            }
            Item { Layout.fillWidth: true }
            GButton {
                objectName: "goXY"
                variant: "alt"
                text: dro.model.rotaryMode ? "XA" : "XY"
                mono: true
                fontSize: Theme.fontLg
                enabled: dro.model.canClick
                onClicked: dro.model.goToZero("XY")  // in rotary mode A is Y
            }
        }
    }
}
