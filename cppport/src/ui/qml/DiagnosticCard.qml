import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The Diagnostic card: what the support file holds, and Download.
StatCard {
    id: card
    property bool titled: false
    signal download()

    CardHeader { visible: card.titled; Layout.fillWidth: true; title: qsTr("Diagnostic File") }
    Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.contentMuted
        text: qsTr("Share this file with our customer support or community so others can help you better. It contains your machine errors, profile, settings, and more.")
    }
    GButton {
        objectName: "downloadDiagnostics"
        Layout.fillWidth: true
        text: qsTr("Download Diagnostic File")
        iconName: "FaDownload"
        iconSize: 16
        onClicked: card.download()
    }
}
