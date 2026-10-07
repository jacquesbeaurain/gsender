import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Plugins Manager (features/Plugins) for QML: discover installed plugins,
// filter by category/search, inspect permissions and contributions, and toggle runtime state.
ToolPage {
    id: tool
    objectName: "pluginsTool"
    title: qsTr("Plugins")
    description: qsTr("Manage installed plugins, view permissions, and configure runtime extensions")

    property PluginsModel model: PluginsModel { objectName: "plugins" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        // Action & Filter Bar
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            TextField {
                id: searchInput
                objectName: "pluginsSearch"
                Layout.preferredWidth: 260
                implicitHeight: 40
                placeholderText: qsTr("Search plugins...")
                color: Theme.contentPrimary
                background: Rectangle {
                    radius: Theme.radius
                    color: Theme.dark ? Theme.surfaceRaised : "white"
                    border.color: searchInput.activeFocus ? Theme.blue[500] : (Theme.dark ? Theme.outline : Theme.gray[300])
                    border.width: Theme.hairline
                }
                onTextChanged: tool.model.search = text
            }

            GSelect {
                id: filterCombo
                objectName: "pluginsFilter"
                Layout.preferredWidth: 160
                implicitHeight: 40
                model: [qsTr("All"), qsTr("Official"), qsTr("Community"), qsTr("Enabled")]
                onActivated: (index) => {
                    const filters = ["all", "official", "community", "enabled"]
                    tool.model.filter = filters[index]
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                text: qsTr("%1 plugins (%2 enabled)").arg(tool.model.count).arg(tool.model.enabledCount)
                color: Theme.contentMuted
                font.pixelSize: Theme.fontSm
                Layout.alignment: Qt.AlignVCenter
            }

            GButton {
                objectName: "pluginsRescan"
                variant: "outline"
                iconName: "LuRefreshCw"
                iconSize: 18
                text: qsTr("Rescan")
                onClicked: tool.model.scan()
            }
        }

        // Main List or Empty State
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: "transparent"
            border.color: Theme.dark ? Theme.outline : Theme.gray[200]
            border.width: Theme.hairline
            clip: true

            // Empty state when no plugins are discovered
            Item {
                anchors.fill: parent
                visible: tool.model.count === 0

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 16
                    Icon {
                        Layout.alignment: Qt.AlignHCenter
                        name: "PiPuzzlePiece"
                        color: Theme.gray[400]
                        width: 64; height: 64
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("No Plugins Found")
                        font.pixelSize: Theme.fontXl
                        font.bold: true
                        color: Theme.contentPrimary
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Place WebAssembly and QML plugin packages into the plugins directory to extend gSender.")
                        color: Theme.contentMuted
                        font.pixelSize: Theme.fontSm
                    }
                    GButton {
                        Layout.alignment: Qt.AlignHCenter
                        variant: "outline"
                        iconName: "LuRefreshCw"
                        text: qsTr("Rescan Directory")
                        onClicked: tool.model.scan()
                    }
                }
            }

            // Plugin cards list
            ListView {
                id: pluginsList
                objectName: "pluginsList"
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12
                visible: tool.model.count > 0
                model: tool.model.pluginsModel
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: pluginCard
                    // The Repeater below shadows `model`, so it reads this instead.
                    readonly property var capabilities: model.capabilities
                    width: pluginsList.width
                    implicitHeight: cardContent.implicitHeight + 24
                    radius: Theme.radius
                    color: Theme.dark ? Theme.surfaceRaised : Theme.gray[50]
                    border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                    border.width: Theme.hairline

                    ColumnLayout {
                        id: cardContent
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        // Top Header Row
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Icon {
                                name: "PiPuzzlePiece"
                                color: model.official ? Theme.primaryText : Theme.contentPrimary
                                width: 24; height: 24
                            }

                            ColumnLayout {
                                spacing: 2
                                RowLayout {
                                    spacing: 8
                                    Label {
                                        text: model.name
                                        font.bold: true
                                        font.pixelSize: Theme.fontBase
                                        color: Theme.contentPrimary
                                    }
                                    Rectangle {
                                        implicitHeight: 18
                                        implicitWidth: versionLabel.implicitWidth + 8
                                        radius: 4
                                        color: Theme.dark ? Theme.gray[700] : Theme.gray[200]
                                        Label {
                                            id: versionLabel
                                            anchors.centerIn: parent
                                            text: "v" + model.version
                                            font.pixelSize: Theme.fontXs
                                            color: Theme.contentPrimary
                                        }
                                    }
                                    Rectangle {
                                        implicitHeight: 18
                                        implicitWidth: badgeLabel.implicitWidth + 8
                                        radius: 4
                                        color: model.official ? (Theme.dark ? "#1a3b2b" : "#d1fae5") : (Theme.dark ? Theme.gray[700] : Theme.gray[200])
                                        Label {
                                            id: badgeLabel
                                            anchors.centerIn: parent
                                            text: model.official ? qsTr("Official") : qsTr("Community")
                                            font.pixelSize: Theme.fontXs
                                            font.weight: Font.DemiBold
                                            color: model.official ? (Theme.dark ? "#34d399" : "#065f46") : Theme.contentMuted
                                        }
                                    }
                                }
                                Label {
                                    text: qsTr("By %1 • %2").arg(model.author).arg(model.id)
                                    font.pixelSize: Theme.fontSm
                                    color: Theme.contentMuted
                                }
                            }

                            Item { Layout.fillWidth: true }

                            GSwitch {
                                objectName: "pluginEnable_" + model.id
                                checked: model.enabled
                                onToggled: tool.model.setEnabled(model.id, checked)
                            }
                        }

                        // Description
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: model.description
                            font.pixelSize: Theme.fontSm
                            color: Theme.contentPrimary
                        }

                        // Capabilities / Permissions Pills
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: model.capabilities && model.capabilities.length > 0

                            Label {
                                text: qsTr("Capabilities:")
                                font.pixelSize: Theme.fontXs
                                font.bold: true
                                color: Theme.contentMuted
                            }

                            Repeater {
                                model: pluginCard.capabilities
                                Rectangle {
                                    implicitHeight: 18
                                    implicitWidth: capText.implicitWidth + 8
                                    radius: 3
                                    color: Theme.dark ? Theme.gray[800] : Theme.gray[200]
                                    Label {
                                        id: capText
                                        anchors.centerIn: parent
                                        text: modelData
                                        font.pixelSize: Theme.fontXs
                                        color: Theme.dark ? Theme.gray[300] : Theme.gray[700]
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
    }
}
