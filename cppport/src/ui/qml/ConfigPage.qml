import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import GSender

// Config (features/Config): the sections on the left (a tap scrolls to
// one); the search and the changed-only filter; every setting - gSender's
// and the board's - in its section; below, the machine profile, the
// board's defaults and EEPROM files, the application settings' files, and
// Apply Settings with the count of changes.
Item {
    id: page
    objectName: "configPage"

    property ConfigModel model: ConfigModel { objectName: "config" }
    readonly property var rows: model.rows
    readonly property var shownSections: rows.filter(r => r.kind === "section").map(r => r.label)
    property string currentSection: shownSections.length ? shownSections[0] : ""

    function showSection(name) {
        const index = model.sectionRow(name)
        if (index >= 0) {
            list.positionViewAtIndex(index, ListView.Beginning)
            currentSection = name
        }
    }
    function followScroll() {
        const index = list.indexAt(10, list.contentY + 10)
        if (index >= 0 && rows[index])
            currentSection = rows[index].section
    }
    function ask(title, message, action, then) {
        confirm.title = title
        confirm.message = message
        confirm.actionText = action
        confirm.then = then
        confirm.open()
    }

    Rectangle { anchors.fill: parent; color: Theme.dark ? Theme.surfaceBase : Theme.gray[50] }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // The sections.
        Rectangle {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: Theme.dark ? Theme.surfaceRaised : "white"
            border.color: Theme.dark ? Theme.outline : Theme.gray[200]
            Flickable {
                anchors.fill: parent
                anchors.margins: 8
                contentHeight: menu.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: menu
                    width: parent.width
                    spacing: 2
                    Repeater {
                        model: page.shownSections
                        Rectangle {
                            required property string modelData
                            readonly property bool current: page.currentSection === modelData
                            objectName: "configSection_" + modelData
                            width: menu.width
                            height: 44
                            radius: Theme.radiusSmall
                            color: current ? Theme.blue[500] : sectionTap.pressed ? Theme.gray[200] : "transparent"
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 12
                                text: parent.modelData
                                font.bold: parent.current
                                color: parent.current ? "white" : Theme.contentPrimary
                            }
                            TapHandler { id: sectionTap; onTapped: page.showSection(parent.modelData) }
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Search and filter.
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 16
                TextField {
                    objectName: "configSearch"
                    Layout.fillWidth: true
                    implicitHeight: 44
                    placeholderText: qsTr("Search settings...")
                    onTextChanged: page.model.search = text
                }
                GSwitch {
                    objectName: "configOnlyModified"
                    checked: page.model.onlyModified
                    onToggled: page.model.onlyModified = checked
                }
                Label { text: qsTr("Only show changed settings"); color: Theme.contentPrimary }
            }

            ListView {
                id: list
                objectName: "configList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                leftMargin: 16
                rightMargin: 16
                bottomMargin: 16
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: 600
                // By count: a value changing keeps the list where it is.
                model: page.rows.length
                ScrollBar.vertical: ScrollBar {}
                onContentYChanged: page.followScroll()
                delegate: ConfigRow {
                    required property int index
                    width: list.width - 32
                    entry: page.rows[index] || ({})
                    model: page.model
                    onOpenTool: (name) => {
                        Backend.openPage("tools")
                        Backend.openTool(name)
                    }
                }
                Label {
                    visible: page.rows.length === 0
                    anchors.centerIn: parent
                    text: qsTr("No settings match")
                    color: Theme.contentMuted
                }
            }

            // The profile, the files, Apply Settings.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: bar.implicitHeight + 24
                color: Theme.dark ? Theme.surfaceRaised : "white"
                border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                RowLayout {
                    id: bar
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    Label { text: qsTr("Machine"); color: Theme.contentSecondary }
                    ComboBox {
                        objectName: "configProfile"
                        Layout.preferredWidth: 240
                        model: page.model.profiles
                        textRole: "name"
                        valueRole: "id"
                        currentIndex: count > 0 ? indexOfValue(page.model.profileId) : -1
                        onActivated: page.model.profileId = currentValue
                    }
                    GButton {
                        objectName: "configFirmwareMenu"
                        text: qsTr("Firmware")
                        iconName: "MdKeyboardArrowDown"
                        onClicked: firmwareMenu.open()
                        Menu {
                            id: firmwareMenu
                            y: -implicitHeight
                            MenuItem {
                                objectName: "configRestoreFirmware"
                                text: qsTr("Restore Defaults")
                                enabled: page.model.canRestoreFirmwareDefaults
                                onTriggered: page.ask(qsTr("Restore Defaults"),
                                                      qsTr("Are you sure you want to restore your %1 back to its default state?").arg(page.model.profileName),
                                                      qsTr("Restore"), () => page.model.restoreFirmwareDefaults())
                            }
                            MenuItem {
                                text: qsTr("Import EEPROM Settings...")
                                enabled: page.model.idle
                                onTriggered: eepromImport.open()
                            }
                            MenuItem {
                                text: qsTr("Export EEPROM Settings...")
                                enabled: page.model.connected
                                onTriggered: {
                                    eepromExport.currentFile = "file:///" + page.model.eepromFileName()
                                    eepromExport.open()
                                }
                            }
                            MenuItem {
                                text: qsTr("Reload ($$)")
                                enabled: page.model.idle
                                onTriggered: page.model.reloadFirmware()
                            }
                        }
                    }
                    GButton {
                        objectName: "configSettingsMenu"
                        text: qsTr("Settings")
                        iconName: "MdKeyboardArrowDown"
                        onClicked: settingsMenu.open()
                        Menu {
                            id: settingsMenu
                            y: -implicitHeight
                            MenuItem {
                                text: qsTr("Export Settings...")
                                onTriggered: {
                                    settingsExport.currentFile = "file:///" + page.model.settingsFileName()
                                    settingsExport.open()
                                }
                            }
                            MenuItem {
                                text: qsTr("Import Settings...")
                                onTriggered: settingsImport.open()
                            }
                            MenuItem {
                                objectName: "configRestoreSettings"
                                text: qsTr("Restore Defaults")
                                onTriggered: page.ask(qsTr("Restore Settings"),
                                                      qsTr("All your current settings will be removed. Are you sure you want to restore default settings?"),
                                                      qsTr("Restore"), () => page.model.restoreDefaultSettings())
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    GButton {
                        objectName: "configRevert"
                        visible: page.model.pendingChanges > 0
                        variant: "outline"
                        text: qsTr("Discard")
                        onClicked: page.model.revert()
                    }
                    GButton {
                        objectName: "configApply"
                        variant: "primary"
                        text: page.model.pendingChanges > 0 ? qsTr("Apply Settings (%1)").arg(page.model.pendingChanges) : qsTr("Apply Settings")
                        enabled: page.model.pendingChanges > 0
                        onClicked: page.model.apply()
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: confirm
        objectName: "configConfirm"
        property var then: null
        onAccepted: if (then) then()
    }
    FileDialog {
        id: settingsExport
        title: qsTr("Export Settings")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Settings (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const error = page.model.exportSettings(selectedFile)
            Backend.notify(error ? error : qsTr("Settings exported"), error ? "error" : "success")
        }
    }
    FileDialog {
        id: settingsImport
        title: qsTr("Import Settings")
        nameFilters: [qsTr("Settings (*.json)"), qsTr("All files (*)")]
        onAccepted: page.ask(qsTr("Import Settings"),
                             qsTr("All your current settings will be replaced. Are you sure you want to import your settings?"),
                             qsTr("Import"), () => {
                                 const report = page.model.importSettings(selectedFile)
                                 Backend.notify(report, page.model.lastImportOk() ? "success" : "error")
                             })
    }
    FileDialog {
        id: eepromImport
        title: qsTr("Import EEPROM Settings")
        nameFilters: [qsTr("Settings (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const error = page.model.importEeprom(selectedFile)
            if (error)
                Backend.notify(error, "error")
        }
    }
    FileDialog {
        id: eepromExport
        title: qsTr("Export EEPROM Settings")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Settings (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const error = page.model.exportEeprom(selectedFile)
            Backend.notify(error ? error : qsTr("EEPROM Settings exported"), error ? "error" : "success")
        }
    }
}
