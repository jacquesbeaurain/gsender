import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The DRO (features/DRO): the units badge; Go To, the corner buttons and
// Park; per axis zero (or home), the work position - tap to type one -, the
// machine position and go to zero; Zero all, the homing switch and Home, XY.
//
// Sizes follow upstream's page at the 1280 px window (max-xl: the axis rows
// are scaled to 95%): rows 32 px high with 7 px between them.
Item {
    id: dro
    objectName: "dro"

    property DroModel model: DroModel {}

    // Upstream's max-xl:scale-95 on the rows, below the xl breakpoint.
    readonly property real ds: Window.window && Window.window.width <= 1280 ? 0.95 : 1

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
        if (dro.model.confirmZero) {
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
        x: 5
        y: 5
        width: badgeText.implicitWidth + 16
        height: 28
        color: Theme.dark ? Theme.surfaceElevated : Theme.gray[300]
        topLeftRadius: Theme.radius
        bottomRightRadius: Theme.radius
        Label {
            id: badgeText
            anchors.centerIn: parent
            text: dro.model.units
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
        spacing: 0

        // Go To, the corners and Park: the corner grid centred, Go To and Park
        // 88 px to either side of the centre (upstream's flex row).
        Item {
            Layout.fillWidth: true
            Layout.topMargin: 9
            implicitHeight: 32
            GButton {
                objectName: "goToButton"
                x: parent.width / 2 - 88 - width / 2
                iconName: "FaPaperPlane"
                iconSize: 14
                implicitWidth: 39
                implicitHeight: 32
                enabled: dro.model.canClick
                onClicked: goTo.open()
                GoToPopup {
                    id: goTo
                    model: dro.model
                    y: parent.height + 4
                }
            }
            // The 64 x 56 grid of the four corners, drawn with perspective.
            Item {
                objectName: "corners"
                visible: dro.model.homingEnabled
                // translateX(-25px) under the 1.4 scale moves the painted pad 35 px
                // left of the box, and it sits centred between Go To and Park.
                x: parent.width / 2 + 31 - 32
                y: parent.height / 2 - 28
                width: 64
                height: 56
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
                x: parent.width / 2 + 88 - width / 2
                iconName: "RiParkingFill"
                iconSize: 16
                implicitWidth: 41
                implicitHeight: 32
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Go to Park Location")
                enabled: dro.model.canClick && dro.model.homed
                onClicked: dro.model.park()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 25
            Layout.rightMargin: 25
            Layout.topMargin: -1   // the labels sit clear of the X0 and X buttons
            Label {
                text: dro.model.homingMode ? qsTr("Home") : qsTr("Zero")
                font.pixelSize: Theme.fontSm
                color: Theme.dark ? Theme.contentMuted : Theme.gray[400]
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("Go to")
                font.pixelSize: Theme.fontSm
                color: Theme.dark ? Theme.contentMuted : Theme.gray[400]
            }
        }

        // Repeated by count, so the rows (and a field being typed in) stay
        // while the positions update.
        Repeater {
            model: dro.model.rows.length
            Item {
                id: row
                required property int index
                readonly property var modelData: dro.model.rows[index] || ({})
                objectName: "axisRow" + modelData.label
                Layout.fillWidth: true
                Layout.leftMargin: 14
                Layout.rightMargin: 14
                Layout.topMargin: index === 0 ? -2.3 : 7 * dro.ds
                implicitHeight: 32 * dro.ds

                // The row's own line (upstream's bottom border).
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.dark ? Theme.outline : Theme.gray[200]
                }

                RowLayout {
                    anchors.fill: parent
                    spacing: 4
                    GButton {
                        objectName: "zero" + row.modelData.label
                        variant: dro.model.homingMode ? "alt" : "secondary"
                        text: dro.model.homingMode ? "H" + row.modelData.label : row.modelData.label + "0"
                        mono: true
                        bold: true
                        fontSize: 20 * dro.ds
                        enabled: row.modelData.enabled
                        implicitHeight: 32 * dro.ds
                        Layout.preferredWidth: 47 * dro.ds
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
                        font.pixelSize: 20 * dro.ds
                        font.bold: true
                        font.family: Theme.monoFont
                        color: Theme.primaryText
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
                        font.pixelSize: Theme.fontSm * dro.ds
                        color: Theme.gray[400]
                        horizontalAlignment: Text.AlignHCenter
                        Layout.preferredWidth: 80
                    }
                    GButton {
                        objectName: "goZero" + row.modelData.label
                        variant: "alt"
                        text: row.modelData.label
                        mono: true
                        fontSize: Theme.fontLg * dro.ds
                        enabled: row.modelData.gotoEnabled
                        implicitHeight: 32 * dro.ds
                        Layout.preferredWidth: 35 * dro.ds
                        Layout.fillHeight: true
                        onClicked: dro.model.goToZero(row.modelData.axis)
                    }
                }
            }
        }

        // Zero all, homing, XY.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 7 * dro.ds - 0.7
            spacing: 8
            GButton {
                objectName: "zeroAll"
                text: qsTr("Zero")
                iconName: "VscTarget"
                iconSize: 19 * dro.ds
                fontSize: Theme.fontSm * dro.ds
                implicitHeight: 32 * dro.ds
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
                fontSize: Theme.fontSm * dro.ds
                implicitHeight: 32 * dro.ds
                enabled: dro.model.canClick
                onClicked: dro.model.home()
            }
            Item { Layout.fillWidth: true }
            GButton {
                objectName: "goXY"
                variant: "alt"
                text: dro.model.rotaryMode ? "XA" : "XY"
                mono: true
                fontSize: Theme.fontLg * dro.ds
                implicitHeight: 32 * dro.ds
                implicitWidth: 45 * dro.ds
                enabled: dro.model.canClick
                onClicked: dro.model.goToZero("XY")  // in rotary mode A is Y
            }
        }
    }
}
