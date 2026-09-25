import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Start From Line (JobControl/StartFromLine): where the job last stopped,
// the recommendation to resume about 10 lines earlier, the line and the
// safe height, then Start from Line.
Popup {
    id: popup
    objectName: "startFromLinePopup"

    property JobModel model

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 24
    width: Math.min(480, parent ? parent.width - 32 : 480)
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }

    onOpened: {
        line.value = Math.max(model.lastLine - 10, 1)
        height_.value = model.defaultSafeHeight
    }

    background: Rectangle {
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceElevated : "white"
    }

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: qsTr("Start From Line")
            font.pixelSize: Theme.fontLg
            font.bold: true
            color: Theme.contentPrimary
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.StyledText
            color: Theme.contentSecondary
            text: qsTr("Recover a job after power loss, mechanical malfunction, disconnection, or other failure.")
                  + "<br><br>" + qsTr("Your job of <b>%1</b> lines was last stopped around line: <b>%2</b>.")
                        .arg(popup.model.totalLines).arg(popup.model.lastLine)
                  + (popup.model.lastLine > 0
                     ? "<br><br>" + qsTr("For best success, we usually recommend resuming about <b>10 lines</b> earlier: <b>line %1</b>")
                           .arg(Math.max(popup.model.lastLine - 10, 0))
                     : "")
        }
        GridLayout {
            columns: 2
            columnSpacing: 12
            Label { text: qsTr("Resume job at line:"); color: Theme.contentPrimary }
            NumberField {
                id: line
                objectName: "startFromLineLine"
                decimals: 0
                implicitHeight: Theme.touchTarget
                Layout.fillWidth: true
                onCommitted: (text) => value = Math.max(1, Math.min(popup.model.totalLines, Math.round(Number(text))))
            }
            Label { text: qsTr("With safe height:"); color: Theme.contentPrimary }
            NumberField {
                id: height_
                objectName: "startFromLineHeight"
                suffix: popup.model.units
                implicitHeight: Theme.touchTarget
                Layout.fillWidth: true
                onCommitted: (text) => value = Math.max(0, Number(text))
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            GButton {
                text: qsTr("Cancel")
                variant: "outline"
                onClicked: popup.close()
            }
            GButton {
                objectName: "startFromLineStart"
                text: qsTr("Start from Line")
                variant: "primary"
                iconName: "FaPlay"
                iconSize: 14
                onClicked: {
                    forceActiveFocus()
                    if (popup.model.startFromLine(line.value, height_.value))
                        popup.close()
                }
            }
        }
    }
}
