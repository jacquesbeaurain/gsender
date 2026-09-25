import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The job's progress over the visualizer (JobControl/ProgressArea,
// SDCardProgress): the percentage, a bar, the time left and elapsed; the
// file the SD card runs.
Rectangle {
    id: area
    objectName: "progressArea"

    property JobModel model

    visible: model.showProgress
    width: 256
    implicitHeight: content.implicitHeight + 16
    radius: 2
    color: Theme.dark ? Theme.surfaceRaised : Theme.gray[100]
    border.color: Theme.dark ? Theme.outline : Theme.gray[500]

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        RowLayout {
            Label {
                objectName: "progressPercent"
                text: Math.min(100, Math.floor(area.model.percent))
                font.pixelSize: 24
                font.bold: true
                color: Theme.contentPrimary
            }
            Label { text: "%"; color: Theme.contentPrimary; Layout.alignment: Qt.AlignBottom; bottomPadding: 3 }
            Item { Layout.fillWidth: true }
            Label {
                visible: area.model.paused
                text: qsTr("Paused")
                font.bold: true
                color: "#c27924"
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 10
            radius: 5
            color: Theme.dark ? Theme.surfaceElevated : Theme.gray[300]
            Rectangle {
                width: parent.width * Math.min(1, area.model.percent / 100)
                height: parent.height
                radius: 5
                color: Theme.green[500]
            }
        }
        Label {
            visible: area.model.sdFile !== ""
            text: qsTr("SD card: %1").arg(area.model.sdFile)
            font.pixelSize: Theme.fontSm
            color: Theme.contentSecondary
        }
        RowLayout {
            visible: area.model.sdFile === ""
            Label {
                objectName: "progressLines"
                text: qsTr("Line %1 of %2").arg(area.model.received).arg(area.model.total)
                font.pixelSize: Theme.fontXs
                color: Theme.contentMuted
            }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "progressRemaining"
                text: qsTr("%1 left").arg(area.model.remaining)
                font.pixelSize: Theme.fontXs
                color: Theme.contentSecondary
            }
        }
        Label {
            visible: area.model.sdFile === ""
            text: qsTr("Elapsed %1").arg(area.model.elapsed)
            font.pixelSize: Theme.fontXs
            color: Theme.contentMuted
        }
    }
}
