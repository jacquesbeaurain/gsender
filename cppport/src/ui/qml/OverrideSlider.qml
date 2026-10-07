import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import GSender

// An override (components/RangeSlider in FeedOverride): the title, what it
// acts on now and the percentage over reset, the slider (10-200 %, steps of
// 10, sent on release) and - / +.
ColumnLayout {
    id: override

    property string title
    property string valueText
    property int percent: 100
    property color fill: Theme.blue[400]
    signal changeRequested(int percent)

    spacing: 4

    RowLayout {
        Layout.fillWidth: true
        Label { text: override.title; color: Theme.contentPrimary; font.pixelSize: Theme.fontBase }
        Item { Layout.fillWidth: true }
        Label { text: override.valueText; color: Theme.primaryText; font.pixelSize: Theme.fontBase }
        Item { Layout.fillWidth: true }
        Label {
            objectName: override.objectName + "Percent"
            text: (slider.pressed ? slider.value : override.percent) + "%"
            color: Theme.contentPrimary
            font.pixelSize: Theme.fontBase
        }
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 36
        radius: Theme.radiusSmall
        color: Theme.dark ? Theme.surfaceRaised : Theme.gray[200]
        RowLayout {
            anchors.fill: parent
            anchors.margins: 2
            spacing: 8
            GButton {
                objectName: override.objectName + "Reset"
                iconName: "FaUndo"
                iconSize: 14
                implicitWidth: 39
                implicitHeight: 32
                enabled: override.enabled
                onClicked: override.changeRequested(100)
            }
            Slider {
                id: slider
                objectName: override.objectName + "Slider"
                Layout.fillWidth: true
                from: 10
                to: 200
                stepSize: 10
                snapMode: Slider.SnapAlways
                onPressedChanged: if (!pressed) override.changeRequested(value)
                Binding on value {
                    when: !slider.pressed
                    value: override.percent
                    restoreMode: Binding.RestoreNone
                }
                background: Rectangle {
                    x: slider.leftPadding
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2
                    width: slider.availableWidth
                    height: 16
                    radius: 8
                    color: Theme.dark ? Theme.surfaceElevated : Theme.gray[400]
                    // The track's diagonal stripes (upstream's repeating-linear-gradient:
                    // 20 px of lightgrey in every 40 px across, at -45 degrees).
                    Item {
                        anchors.fill: parent
                        clip: true
                        readonly property real period: 40 * Math.SQRT2
                        Repeater {
                            model: Math.ceil((parent.width + parent.height) / parent.period) + 1
                            Shape {
                                id: stripe
                                required property int index
                                readonly property real t0: index * parent.period + 20 * Math.SQRT2
                                readonly property real t1: index * parent.period + 40 * Math.SQRT2
                                width: parent.width
                                height: parent.height
                                preferredRendererType: Shape.CurveRenderer
                                ShapePath {
                                    strokeWidth: -1
                                    fillColor: "lightgrey"
                                    // Slanting up to the right: the top edge is further right.
                                    PathPolyline {
                                        path: [Qt.point(t0, 0), Qt.point(t1, 0),
                                               Qt.point(t1 - stripe.height, stripe.height),
                                               Qt.point(t0 - stripe.height, stripe.height), Qt.point(t0, 0)]
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        width: slider.visualPosition * parent.width
                        height: parent.height
                        radius: 8
                        color: override.enabled ? override.fill : Theme.gray[500]
                    }
                }
                handle: Rectangle {
                    x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                    y: slider.topPadding + slider.availableHeight / 2 - height / 2
                    width: 24   // upstream's 24 px thumb
                    height: 24
                    radius: 12
                    color: override.enabled ? "white" : Theme.gray[300]
                    border.color: "#475569"
                    border.width: 2
                }
            }
            GButton {
                objectName: override.objectName + "Minus"
                iconName: "FaMinus"
                iconSize: 14
                implicitWidth: 39
                implicitHeight: 32
                enabled: override.enabled
                onClicked: if (override.percent - 10 >= 10) override.changeRequested(override.percent - 10)
            }
            GButton {
                objectName: override.objectName + "Plus"
                iconName: "FaPlus"
                iconSize: 14
                implicitWidth: 39
                implicitHeight: 32
                enabled: override.enabled
                onClicked: if (override.percent + 10 <= 200) override.changeRequested(override.percent + 10)
            }
        }
    }
}
