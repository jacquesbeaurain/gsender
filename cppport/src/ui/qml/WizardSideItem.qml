import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Beside a wizard step (secondaryContent): a picture, the jog controls,
// the commands it sends, the TLS's settings or input, or the help link.
ColumnLayout {
    id: side

    property var item: ({})
    property AccessoryModel model

    spacing: 6
    visible: item.kind !== "tlsInput" || model.tlsInputShown

    Label {
        visible: !!side.item.title && side.item.kind !== "link"
        text: side.item.title || ""
        font.bold: true
        color: Theme.contentPrimary
    }
    Image {
        visible: side.item.kind === "image"
        Layout.fillWidth: true
        Layout.preferredHeight: visible && implicitWidth > 0 ? width * implicitHeight / implicitWidth : 0
        source: side.item.kind === "image" ? side.item.url : ""
        fillMode: Image.PreserveAspectFit
        smooth: true
    }
    JogPanel {
        visible: side.item.kind === "jogging"
        objectName: "wizardJog"
        Layout.alignment: Qt.AlignHCenter
    }

    // "Commands to be sent".
    ColumnLayout {
        id: commands
        visible: side.item.kind === "commands"
        Layout.fillWidth: true
        spacing: 6
        readonly property var preview: side.item.source === "spindle" ? side.model.spindlePreview : side.model.autoSpinPreview
        Rectangle {
            implicitWidth: previewLabel.implicitWidth + 16
            implicitHeight: 24
            radius: 6
            color: "#dbeafe"
            Label {
                id: previewLabel
                anchors.centerIn: parent
                text: commands.preview.label || ""
                font.bold: true
                color: "#1e40af"
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: code.implicitHeight + 16
            radius: 6
            color: Theme.dark ? Theme.surfaceSunken : Theme.gray[50]
            border.color: Theme.dark ? Theme.outline : Theme.gray[200]
            Column {
                id: code
                objectName: "commandPreview"
                x: 8; y: 8
                width: parent.width - 16
                Repeater {
                    model: side.item.kind === "commands" ? (commands.preview.lines || []) : []
                    Row {
                        required property string modelData
                        required property int index
                        spacing: 10
                        Label {
                            width: 24
                            horizontalAlignment: Text.AlignRight
                            text: index + 1
                            font.family: "monospace"
                            font.pixelSize: Theme.fontSm
                            color: Theme.gray[400]
                        }
                        Label {
                            text: modelData
                            font.family: "monospace"
                            font.pixelSize: Theme.fontSm
                            color: Theme.contentPrimary
                        }
                    }
                }
            }
        }
    }

    // TLSContinuitySidebar and TLSInputEnable: settings with their verdicts.
    Label {
        visible: side.item.kind === "tlsSettings" || side.item.kind === "tlsInput"
        text: side.item.kind === "tlsInput" ? qsTr("TLS Input") : qsTr("Related Settings")
        font.bold: true
        color: Theme.contentPrimary
    }
    Repeater {
        model: side.item.kind === "tlsSettings" ? side.model.tlsSettings : side.item.kind === "tlsInput" ? side.model.tlsInput : []
        RowLayout {
            required property var modelData
            objectName: "tlsSetting"
            Layout.fillWidth: true
            spacing: 8
            Label { text: modelData.label + ": " + modelData.value; color: Theme.contentPrimary; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Rectangle {
                implicitWidth: verdict.implicitWidth + 12
                implicitHeight: 20
                radius: 4
                color: modelData.ok ? "#dcfce7" : "#fee2e2"
                Label {
                    id: verdict
                    anchors.centerIn: parent
                    text: modelData.verdict
                    font.pixelSize: Theme.fontXs
                    color: modelData.ok ? "#15803d" : "#b91c1c"
                }
            }
        }
    }
    GButton {
        property bool sent: false
        visible: side.item.kind === "tlsInput"
        objectName: "enableTlsInput"
        text: qsTr("Enable TLS Input")
        enabled: !side.model.tlsInputReady
        onClicked: {
            side.model.enableTlsInput()
            sent = true
        }
        Label {
            anchors.left: parent.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            visible: parent.sent
            text: qsTr("TLS input enable command sent.")
            color: Theme.contentSecondary
        }
    }

    // The help link.
    Rectangle {
        visible: side.item.kind === "link"
        Layout.fillWidth: true
        implicitHeight: link.implicitHeight + 20
        radius: 8
        color: Theme.dark ? Theme.surfaceRaised : Theme.gray[50]
        Label {
            id: link
            anchors.fill: parent
            anchors.margins: 10
            wrapMode: Text.Wrap
            textFormat: Text.StyledText
            text: "<b>" + (side.item.title || "") + "</b> " + (side.item.text || "")
                  + " <a href=\"" + (side.item.url || "") + "\">" + qsTr("online resources") + "</a>"
            color: Theme.contentPrimary
            linkColor: Theme.blue[500]
            onLinkActivated: (url) => Qt.openUrlExternally(url)
        }
    }
}
