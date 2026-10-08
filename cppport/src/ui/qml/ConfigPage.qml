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
    // Whether the row belongs to a subsection (its fieldset).
    function inSubsection(index) {
        for (let i = index; i >= 0; --i) {
            const kind = (rows[i] || ({})).kind
            if (i !== index && kind === "subsection")
                return rows[index].kind !== "section" && rows[index].kind !== "subsection"
            if (kind === "section")
                return false
        }
        return false
    }
    function ask(title, message, action, then) {
        confirm.title = title
        confirm.message = message
        confirm.actionText = action
        confirm.then = then
        confirm.open()
    }

    Rectangle { anchors.fill: parent; color: Theme.surfaceRaised }

    // upstream's max-xl: the page is in its compact sizes.
    readonly property bool compact: Window.window && Window.window.width <= 1280
    // The section icons (SettingsMenu's).
    readonly property var sectionIcons: ({
        "Basics": "FaCog", "Customize UI": "MdSettingsApplications", "Motors": "PiEngine", "Probe": "MdTouchApp",
        "Action Buttons": "RxButton", "Homing/Limits": "FaHome", "Spindle/Laser": "GiTargetLaser",
        "Accessory Outputs": "CiMapPin", "Rotary": "FaArrowsSpin", "Automations": "FaRobot",
        "Tool Changing": "IoIosSwap", "Ethernet": "BsEthernet", "Status Lights": "CiLight",
        "Advanced Motors": "SiCoronaengine", "More Settings": "MdOutlineReadMore", "Accessibility": "MdAccessibility"
    })

    // upstream's ActionButton: an icon over a label, in a bordered group.
    component ActionButton: Item {
        id: action
        property string label
        property string iconName
        property bool dividerBefore: true
        property string tooltip   // HoverTips
        signal clicked()
        implicitWidth: Math.max(64, actionLabel.implicitWidth + 40)
        implicitHeight: 48
        Rectangle { visible: action.dividerBefore; width: 1; height: parent.height; color: Theme.border }
        Rectangle {
            anchors.fill: parent
            color: actionTap.pressed || actionHover.hovered ? (Theme.dark ? Theme.surfaceHover : Theme.gray[50]) : "transparent"
            opacity: action.enabled ? 1 : 0.5
        }
        Column {
            anchors.centerIn: parent
            spacing: 0
            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: action.iconName
                size: 20
                color: action.enabled ? (actionHover.hovered ? Theme.blue[600] : Theme.contentSoft) : Theme.gray[400]
            }
            Label {
                id: actionLabel
                anchors.horizontalCenter: parent.horizontalCenter
                text: action.label
                font.pixelSize: Theme.fontSm
                color: action.enabled ? (actionHover.hovered ? Theme.blue[600] : Theme.contentSoft) : Theme.gray[400]
            }
        }
        HoverHandler { id: actionHover }
        TapHandler { id: actionTap; enabled: action.enabled; onTapped: action.clicked() }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // The sections (Menu): an icon and the name, spread over the height;
        // the EEPROM tab has none.
        Panel {
            radius: 0
            Layout.preferredWidth: 203
            Layout.fillHeight: true
            Column {
                id: menu
                anchors.fill: parent
                anchors.margins: 1
                visible: page.model.scope === "config"
                Repeater {
                    model: page.shownSections
                    Item {
                        id: sectionItem
                        required property string modelData
                        readonly property bool current: page.currentSection === modelData
                        objectName: "configSection_" + modelData
                        width: menu.width
                        height: menu.height / Math.max(1, page.shownSections.length)
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: page.compact ? 4 : 16
                            spacing: 8
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: page.sectionIcons[sectionItem.modelData] || "FaCog"
                                size: page.compact ? 20 : 24
                                color: sectionItem.current ? Theme.primaryText : (sectionHover.hovered ? Theme.blue[600] : Theme.contentSoft)
                            }
                            Label {
                                anchors.verticalCenter: parent.verticalCenter
                                text: sectionItem.modelData
                                font.pixelSize: page.compact ? Theme.fontSm : Theme.fontBase
                                                                color: sectionItem.current ? Theme.primaryText : (sectionHover.hovered ? Theme.blue[600] : Theme.contentPrimary)
                            }
                        }
                        HoverHandler { id: sectionHover }
                        TapHandler { onTapped: page.showSection(sectionItem.modelData) }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Search, the modified filter, the application's preferences.
            Panel {
                radius: 0
                Layout.fillWidth: true
                implicitHeight: 73
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: page.compact ? 20 : 96
                    anchors.rightMargin: 16
                    spacing: 8
                    // Search.
                    Item {
                        Layout.preferredWidth: 213
                        Layout.preferredHeight: 41
                        TextField {
                            id: searchField
                            objectName: "configSearch"
                            anchors.fill: parent
                            leftPadding: 40
                            font.pixelSize: Theme.fontSm
                            placeholderText: qsTr("Search Config")
                            onTextChanged: page.model.search = text
                            background: Panel {
                                color: Theme.dark ? Theme.surfaceElevated : Theme.gray[50]
                                border.color: searchField.activeFocus ? Theme.blue[500] : Theme.outline
                            }
                        }
                        Icon {
                            x: 12
                            anchors.verticalCenter: parent.verticalCenter
                            name: "LuSearch"
                            size: 16
                            color: Theme.contentMuted
                        }
                    }
                    Panel {
                        objectName: "configSearchClear"
                        Layout.preferredWidth: clearLabel.implicitWidth + 20
                        Layout.preferredHeight: 41
                        color: Theme.robin[400]
                        border.color: Theme.blue[400]
                        Label { id: clearLabel; anchors.centerIn: parent; text: qsTr("Clear"); color: "white"; font.pixelSize: Theme.fontSm; font.weight: Font.DemiBold }
                        TapHandler { onTapped: searchField.text = "" }
                    }
                    // FilterDefaultToggle.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 32
                        spacing: 16
                        Label {
                            text: page.compact ? qsTr("Modified") : qsTr("View Modified")
                            font.pixelSize: page.compact ? Theme.fontXs : Theme.fontBase
                            color: Theme.contentMuted
                        }
                        GSwitch {
                            objectName: "configOnlyModified"
                            checked: page.model.onlyModified
                            onToggled: page.model.onlyModified = checked
                        }
                        Item { Layout.fillWidth: true }
                    }
                    // ApplicationPreferences.
                    Item {
                        Layout.preferredWidth: prefs.implicitWidth + 2
                        Layout.preferredHeight: 66
                        Panel {
                            anchors.fill: parent
                            anchors.topMargin: 9
                            radius: 4
                            color: "transparent"
                        }
                        Rectangle {   // the legend interrupts the border
                            x: 10
                            width: legend.implicitWidth + 8
                            height: legend.implicitHeight
                            color: Theme.surfaceRaised
                            Label { id: legend; anchors.centerIn: parent; text: qsTr("gSender Preferences"); color: Theme.contentSoft }
                        }
                        Row {
                            id: prefs
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            ActionButton {
                                objectName: "configPrefsReset"
                                dividerBefore: false
                                label: qsTr("Reset")
                                iconName: "GrPowerReset"
                                onClicked: page.ask(qsTr("Restore Settings"),
                                                    qsTr("All your current settings will be removed. Are you sure you want to restore default settings?"),
                                                    qsTr("Restore"), () => page.model.restoreDefaultSettings())
                            }
                            ActionButton {
                                objectName: "configPrefsImport"
                                label: qsTr("Import")
                                iconName: "PiDownloadSimple"
                                onClicked: settingsImport.open()
                            }
                            ActionButton {
                                objectName: "configPrefsExport"
                                label: qsTr("Export")
                                iconName: "PiUploadSimple"
                                onClicked: {
                                    settingsExport.currentFile = "file:///" + page.model.settingsFileName()
                                    settingsExport.open()
                                }
                            }
                        }
                    }
                }
            }

            // All Config | EEPROM.
            Panel {
                radius: 0
                Layout.fillWidth: true
                implicitHeight: 37
                color: Theme.muted
                Row {
                    anchors.fill: parent
                    Repeater {
                        model: [{ scope: "config", label: qsTr("All Config") }, { scope: "eeprom", label: qsTr("EEPROM") }]
                        Item {
                            id: tabItem
                            required property var modelData
                            readonly property bool current: page.model.scope === modelData.scope
                            objectName: "configTab_" + modelData.scope
                            width: parent.width / 2
                            height: parent.height
                            Rectangle {
                                visible: tabItem.current
                                anchors.fill: parent
                                anchors.margins: 3
                                color: Theme.muted
                            }
                            Label {
                                anchors.centerIn: parent
                                text: tabItem.modelData.label
                                font.pixelSize: Theme.fontSm
                                font.weight: Font.DemiBold
                                color: tabItem.current ? Theme.blue[500] : Theme.contentPrimary
                            }
                            Rectangle {
                                visible: tabItem.current
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 3
                                color: Theme.blue[500]
                            }
                            TapHandler { onTapped: page.model.scope = tabItem.modelData.scope }
                        }
                    }
                }
            }

            // The disconnected notice (EEPROMNotConnectedWarning), then the settings.
            ListView {
                id: list
                objectName: "configList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.bottomMargin: 117   // the scroll area ends above the profile bar
                leftMargin: page.compact ? 8 : 40
                rightMargin: page.compact ? 22 : 54
                topMargin: 16
                bottomMargin: 16
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                cacheBuffer: 600
                // By count: a value changing keeps the list where it is.
                model: page.rows.length
                ScrollBar.vertical: GScrollBar {}
                onContentYChanged: page.followScroll()
                header: Item {
                    width: list.width - list.leftMargin - list.rightMargin
                    height: page.model.connected ? 0 : 68
                    visible: !page.model.connected
                    Panel {
                        objectName: "configNotConnected"
                        anchors.fill: parent
                        anchors.bottomMargin: 16
                        color: Theme.tw.yellow[50]   // the notice keeps its light colours in dark mode
                        border.color: Theme.tw.yellow[300]
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 12
                            Icon { name: "BsInfoCircleFill"; size: 16; color: Theme.tw.yellow[800] }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                font.pixelSize: Theme.fontSm
                                color: Theme.tw.yellow[800]
                                textFormat: Text.StyledText
                                text: qsTr("<b>Disconnected!</b> Some settings may not appear unless connected to a machine.")
                            }
                        }
                    }
                }
                delegate: ConfigRow {
                    required property int index
                    width: list.width - list.leftMargin - list.rightMargin
                    entry: page.rows[index] || ({})
                    model: page.model
                    inSub: page.inSubsection(index)
                    subLast: inSub && (index === page.rows.length - 1 || ["section", "subsection"].includes((page.rows[index + 1] || ({})).kind || ""))
                    first: index === 0 || (page.rows[index - 1] || ({})).kind === "section"
                    last: index === page.rows.length - 1 || (page.rows[index + 1] || ({})).kind === "section"
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
        }
    }

    // The profile bar (ProfileBar): floating over the bottom right.
    Rectangle {
        id: profileBar
        anchors.right: parent.right
        anchors.rightMargin: page.compact ? 0 : 56
        anchors.bottom: parent.bottom
        anchors.bottomMargin: page.compact ? 16 : 32
        width: barRow.implicitWidth + 32
        height: 64
        color: Theme.surfaceRaised
        RowLayout {
            id: barRow
            anchors.centerIn: parent
            spacing: 16
            Panel {
                implicitHeight: 48
                implicitWidth: profileRow.implicitWidth + 2
                color: "transparent"
                RowLayout {
                    id: profileRow
                    anchors.centerIn: parent
                    spacing: 0
                    Item {
                        Layout.preferredWidth: 256
                        Layout.preferredHeight: 48
                        GSelect {
                            objectName: "configProfile"
                            anchors.fill: parent
                            anchors.margins: 4
                            font.pixelSize: Theme.fontSm
                            model: page.model.profiles
                            textRole: "name"
                            valueRole: "id"
                            currentIndex: count > 0 ? indexOfValue(page.model.profileId) : -1
                            onActivated: page.model.profileId = currentValue
                        }
                    }
                    ActionButton {
                        objectName: "configRestoreFirmware"
                        label: qsTr("Defaults")
                        // RestoreDefaultDialog's (upstream also opens it by itself once the
                        // profile changes).
                        tooltip: qsTr("Make sure you click to apply your defaults to your controller")
                        iconName: "GrRevert"
                        enabled: page.model.canRestoreFirmwareDefaults
                        onClicked: page.ask(qsTr("Restore Defaults"),
                                            qsTr("Are you sure you want to restore your %1 back to its default state?").arg(page.model.profileName),
                                            qsTr("Restore"), () => page.model.restoreFirmwareDefaults())
                    }
                    ActionButton {
                        objectName: "configFlash"
                        label: qsTr("Flash")
                        iconName: "PiLightning"
                        // Only the simulators can be flashed.
                        enabled: flashDialog.model.available
                        tooltip: enabled ? "" : qsTr("Firmware flashing is only available on the simulated boards")
                        onClicked: flashDialog.open()
                    }
                    ActionButton {
                        objectName: "configEepromImport"
                        label: qsTr("Import")
                        iconName: "PiDownloadSimpleBold"
                        enabled: page.model.idle
                        onClicked: eepromImport.open()
                    }
                    ActionButton {
                        objectName: "configEepromExport"
                        label: qsTr("Export")
                        iconName: "PiUploadSimpleBold"
                        enabled: page.model.connected
                        onClicked: {
                            eepromExport.currentFile = "file:///" + page.model.eepromFileName()
                            eepromExport.open()
                        }
                    }
                }
            }
            Rectangle {
                id: applyButton
                objectName: "configApply"
                readonly property bool active: page.model.pendingChanges > 0
                implicitHeight: 52
                implicitWidth: applyLabel.implicitWidth + 24
                radius: 6
                color: "transparent"
                border.width: 3
                border.color: active ? Theme.green[600] : Theme.outline
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 3
                    radius: 3
                    color: applyButton.active ? Theme.green[600] : (Theme.dark ? Theme.surfaceElevated : Theme.gray[300])
                }
                Label {
                    id: applyLabel
                    anchors.centerIn: parent
                    text: page.compact ? qsTr("Apply") : qsTr("Apply Settings")
                    font.pixelSize: Theme.fontLg
                    color: applyButton.active ? "white" : (Theme.dark ? Theme.contentSecondary : Theme.gray[600])
                }
                TapHandler { enabled: applyButton.active; onTapped: page.model.apply() }
            }
        }
    }

    ConfirmDialog {
        id: confirm
        objectName: "configConfirm"
        property var then: null
        onAccepted: if (then) then()
    }
    FlashDialog { id: flashDialog }
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
