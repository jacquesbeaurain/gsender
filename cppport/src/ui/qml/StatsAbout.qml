import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Stats: About (features/Stats/About): the logo, the version, the licence,
// what gSender is, the team and the release notes.
Flickable {
    id: page
    objectName: "statsAbout"

    property StatsModel model

    contentHeight: column.implicitHeight + 96
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    ColumnLayout {
        id: column
        x: 32
        y: 16
        width: page.width - 64
        spacing: 18
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Image { source: "qrc:/about/icon-square.png"; sourceSize.width: 125; sourceSize.height: 125 }
            ColumnLayout {
                spacing: 2
                Label { text: qsTr("gSender"); font.pixelSize: 32; font.bold: true; color: Theme.contentPrimary }
                Label { text: qsTr("By Sienci Labs"); color: Theme.contentMuted }
                Label { objectName: "aboutVersion"; text: page.model.version; color: Theme.contentMuted }
            }
            Item { Layout.fillWidth: true }
            ColumnLayout {
                spacing: 4
                Label { Layout.alignment: Qt.AlignRight; text: qsTr("Copyright © %1 Sienci Labs Inc.").arg(new Date().getFullYear()); color: Theme.contentMuted }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    Label { text: qsTr("Made in Canada"); color: Theme.contentMuted }
                    Image { source: "qrc:/about/canada-flag-icon.png" }
                }
                Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("GNU GPLv3 License")
                    color: Theme.blue[500]
                    font.underline: true
                    TapHandler { onTapped: Qt.openUrlExternally("https://github.com/Sienci-Labs/gsender/blob/master/LICENSE") }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontLg
            color: Theme.contentPrimary
            text: qsTr("gSender is a free and feature-packed CNC control software, designed to be clean and easy to learn while retaining a depth of capabilities for advanced users. Many thousands of people trust gSender to control their grbl and grblHAL-based CNCs every day, and they keep coming back for its ease of use, engaged community, and reliability.")
        }
        CardHeader { Layout.fillWidth: true; title: qsTr("gSender Team") }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.StyledText
            font.pixelSize: Theme.fontLg
            color: Theme.contentPrimary
            text: "<b>Chris T.</b> (Project Lead), <b>Kevin G.</b> (Lead Dev), <b>Walid K.</b> (Dev Manager), <b>Sophia B.</b> (Dev), <b>Shilpa G</b> (QA), <b>Stephen C.</b> (Docs), <b>Kelly Z.</b> (Icon Design)"
        }
        CardHeader {
            Layout.fillWidth: true
            title: qsTr("Release Notes")
            linkLabel: qsTr("See all latest updates made")
            onLinkActivated: Qt.openUrlExternally("https://github.com/Sienci-Labs/gsender")
        }
        Label {
            visible: page.model.releases.length === 0
            text: qsTr("No release notes found")
            color: Theme.contentMuted
        }
        Repeater {
            model: page.model.releases
            ColumnLayout {
                required property var modelData
                objectName: "release"
                Layout.fillWidth: true
                spacing: 4
                Label { text: modelData.heading; font.pixelSize: Theme.fontLg; font.bold: true; color: Theme.contentPrimary }
                Repeater {
                    model: modelData.notes
                    Label {
                        required property string modelData
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: "• " + modelData
                        color: Theme.contentSecondary
                    }
                }
            }
        }
    }
}
