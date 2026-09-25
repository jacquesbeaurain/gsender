import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
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
        Label { text: override.valueText; color: Theme.blue[500]; font.pixelSize: Theme.fontBase }
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
        implicitHeight: Theme.touchTarget + 4
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
                    width: 28
                    height: 28
                    radius: 14
                    color: override.enabled ? "white" : Theme.gray[300]
                    border.color: "#475569"
                    border.width: 2
                }
            }
            GButton {
                objectName: override.objectName + "Minus"
                iconName: "FaMinus"
                iconSize: 14
                enabled: override.enabled
                onClicked: if (override.percent - 10 >= 10) override.changeRequested(override.percent - 10)
            }
            GButton {
                objectName: override.objectName + "Plus"
                iconName: "FaPlus"
                iconSize: 14
                enabled: override.enabled
                onClicked: if (override.percent + 10 <= 200) override.changeRequested(override.percent + 10)
            }
        }
    }
}
