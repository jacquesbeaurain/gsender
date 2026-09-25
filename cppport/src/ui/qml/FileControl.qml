import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import GSender

// File control (features/FileControl): the button group on the card's top
// edge - Load File, the recent files, reload, close - and the file's
// information: name, size, lines, path, Info or Size, the editor and
// step-through buttons; without a file, the recent files and the last job.
Item {
    id: control
    objectName: "fileControl"

    property FileModel model: FileModel {}
    signal openEditor()
    signal openStepThrough()

    FileDialog {
        id: dialog
        title: qsTr("Load G-code")
        nameFilters: [qsTr("G-code files (*.gcode *.gc *.nc *.ngc *.tap *.txt *.cnc)"), qsTr("All files (*)")]
        onAccepted: {
            const error = control.model.load(selectedFile)
            if (error)
                Backend.notify(error, "error")
        }
    }
    function load() { dialog.open() }

    ConfirmDialog {
        id: confirmClose
        objectName: "confirmCloseFile"
        title: qsTr("Are you sure?")
        message: qsTr("This will close the current file. Any unsaved changes will be lost.")
        actionText: qsTr("Close File")
        onAccepted: control.model.close()
    }
    Connections {
        target: control.model
        function onNotice(text) { Backend.notify(text, "info") }
    }

    Card {
        anchors.fill: parent
        anchors.topMargin: 24

        // Without a file: the recent files and the last job.
        RowLayout {
            anchors.fill: parent
            anchors.topMargin: 28
            spacing: 24
            visible: !control.model.loaded
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                Label {
                    text: qsTr("Recent Files")
                    color: Theme.contentPrimary
                    Layout.leftMargin: 16
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 12
                    color: Theme.dark ? Theme.surfaceRaised : "white"
                    border.width: 2
                    border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                    clip: true
                    ListView {
                        objectName: "recentFiles"
                        anchors.fill: parent
                        anchors.margins: 4
                        model: control.model.recentFiles
                        delegate: Item {
                            required property var modelData
                            width: ListView.view.width
                            height: Theme.touchTarget
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                spacing: 6
                                Icon { name: "LiaFileUploadSolid"; color: Theme.contentPrimary; width: 24; height: 24 }
                                Label {
                                    text: parent.parent.modelData.name
                                    elide: Text.ElideRight
                                    color: Theme.contentPrimary
                                    Layout.fillWidth: true
                                }
                            }
                            TapHandler {
                                enabled: control.model.canLoad
                                onTapped: {
                                    const error = control.model.load(parent.modelData.path)
                                    if (error)
                                        Backend.notify(error, "error")
                                }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                visible: control.model.lastJob.file !== undefined
                Layout.preferredWidth: 2
                Layout.fillWidth: true
                spacing: 12
                Label {
                    text: qsTr("Last Job")
                    color: Theme.contentSecondary
                }
                RowLayout {
                    spacing: 8
                    Icon { name: "LuFileCode2"; color: Theme.contentMuted; width: 18; height: 18 }
                    Label {
                        text: control.model.lastJob.file || ""
                        font.bold: true
                        color: Theme.contentMuted
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                RowLayout {
                    spacing: 8
                    Icon { name: "MdInfoOutline"; color: Theme.contentMuted; width: 18; height: 18 }
                    Label {
                        objectName: "lastJobStatus"
                        text: control.model.lastJob.status || ""
                        font.bold: true
                        color: control.model.lastJob.status === "COMPLETE" ? "#22c55e" : Theme.red[500]
                    }
                }
                RowLayout {
                    spacing: 8
                    Icon { name: "FiClock"; color: Theme.contentMuted; width: 18; height: 18 }
                    Label {
                        text: control.model.lastJob.duration || ""
                        font.bold: true
                        color: Theme.contentMuted
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // A file: its information.
        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 26
            spacing: 2
            visible: control.model.loaded
            // The name elides; the extension stays.
            Row {
                Layout.alignment: Qt.AlignHCenter
                Label {
                    objectName: "fileName"
                    text: control.model.baseName
                    font.pixelSize: Theme.fontLg
                    font.bold: true
                    color: Theme.contentPrimary
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, control.width - 80)
                }
                Label {
                    text: control.model.extension ? "." + control.model.extension : ""
                    font.pixelSize: Theme.fontLg
                    font.bold: true
                    color: Theme.contentPrimary
                }
            }
            Label {
                objectName: "fileSize"
                Layout.alignment: Qt.AlignHCenter
                text: control.model.analyzing ? qsTr("Analysing...")
                                              : qsTr("%1 (%2 lines)").arg(control.model.sizeText).arg(control.model.lines)
                font.pixelSize: Theme.fontXs
                color: Theme.gray[500]
            }
            Label {
                visible: control.model.path !== ""
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: parent.width
                text: control.model.path
                elide: Text.ElideMiddle
                font.pixelSize: Theme.fontXs
                color: Theme.gray[500]
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 6
                spacing: 16
                // Info / Size.
                ColumnLayout {
                    spacing: 0
                    Label { text: qsTr("Info"); font.pixelSize: Theme.fontSm; color: Theme.gray[500]; Layout.alignment: Qt.AlignHCenter }
                    // A vertical switch (Switch position="vertical").
                    Rectangle {
                        id: sizeSwitch
                        objectName: "infoSizeSwitch"
                        property bool checked: false
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: 24
                        implicitHeight: 44
                        radius: 12
                        color: checked ? Theme.blue[500] : (Theme.dark ? Theme.surfaceElevated : Theme.gray[300])
                        Rectangle {
                            x: 2
                            y: sizeSwitch.checked ? parent.height - height - 2 : 2
                            width: 20
                            height: 20
                            radius: 10
                            color: "white"
                            Behavior on y { NumberAnimation { duration: 120 } }
                        }
                        TapHandler { onTapped: sizeSwitch.checked = !sizeSwitch.checked }
                    }
                    Label { text: qsTr("Size"); font.pixelSize: Theme.fontSm; color: Theme.gray[500]; Layout.alignment: Qt.AlignHCenter }
                }
                GridLayout {
                    objectName: "fileInfo"
                    visible: !sizeSwitch.checked
                    columns: 2
                    rowSpacing: 0
                    columnSpacing: 6
                    Label { text: qsTr("Estimated Time"); font.bold: true; font.pixelSize: Theme.fontSm; color: Theme.contentPrimary }
                    Label { objectName: "estimatedTime"; text: control.model.estimatedTime; font.pixelSize: Theme.fontSm; color: Theme.contentPrimary }
                    Label { text: qsTr("Feed"); font.bold: true; font.pixelSize: Theme.fontSm; color: Theme.contentPrimary }
                    Label { text: control.model.feedText; font.pixelSize: Theme.fontSm; color: Theme.contentPrimary }
                    Label { text: qsTr("Speed"); font.bold: true; font.pixelSize: Theme.fontSm; color: Theme.contentPrimary }
                    Label { text: control.model.speedText; font.pixelSize: Theme.fontSm; color: Theme.contentPrimary }
                    Label { text: qsTr("Tools"); font.bold: true; font.pixelSize: Theme.fontSm; color: Theme.contentPrimary }
                    Label { text: control.model.toolsText; font.pixelSize: Theme.fontSm; color: Theme.contentPrimary }
                }
                // The extent (Size): a bordered table.
                Grid {
                    objectName: "fileExtent"
                    visible: sizeSwitch.checked
                    columns: 4
                    // The cells row by row: the header, then X, Y, Z (A).
                    Repeater {
                        model: {
                            const rows = [{ axis: "", size: qsTr("Size"), min: qsTr("Min"), max: qsTr("Max") }]
                                .concat(control.model.extent)
                            let cells = []
                            rows.forEach((row, r) => [row.axis, row.size, row.min, row.max].forEach(
                                (text, c) => cells.push({ text: text, bold: r === 0 || c === 0, first: c === 0 })))
                            return cells
                        }
                        Rectangle {
                            required property var modelData
                            width: modelData.first ? 24 : 64
                            height: 22
                            color: "transparent"
                            border.color: Theme.dark ? Theme.outline : Theme.gray[300]
                            Label {
                                anchors.centerIn: parent
                                text: parent.modelData.text
                                font.pixelSize: Theme.fontSm
                                font.bold: parent.modelData.bold
                                color: Theme.contentPrimary
                            }
                        }
                    }
                }
                ColumnLayout {
                    spacing: 6
                    GButton {
                        objectName: "openEditor"
                        iconName: "LuPencil"
                        variant: "outline"
                        implicitWidth: Theme.touchTarget
                        onClicked: control.openEditor()
                    }
                    GButton {
                        objectName: "openStepThrough"
                        iconName: "LuFootprints"
                        variant: "outline"
                        implicitWidth: Theme.touchTarget
                        enabled: !control.model.analyzing
                        onClicked: control.openStepThrough()
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    // The button group on the card's top edge.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        width: buttons.implicitWidth + 4
        height: 48
        radius: Theme.radiusSmall
        color: Theme.dark ? Theme.surfaceRaised : "white"
        border.color: Theme.blue[500]
        border.width: 2
        clip: true
        RowLayout {
            id: buttons
            anchors.fill: parent
            anchors.margins: 2
            spacing: 0
            component Divider: Rectangle { width: 2; Layout.fillHeight: true; color: Theme.dark ? Theme.outline : Theme.gray[300] }
            GButton {
                objectName: "loadFile"
                variant: "ghost"
                iconName: "FaFolderOpen"
                text: qsTr("Load File")
                enabled: control.model.canLoad
                Layout.fillHeight: true
                onClicked: control.load()
            }
            Divider {}
            GButton {
                objectName: "recentFilesButton"
                variant: "ghost"
                iconName: "MdKeyboardArrowDown"
                iconSize: 32
                implicitWidth: 60
                enabled: control.model.canLoad && control.model.recentFiles.length > 0
                Layout.fillHeight: true
                onClicked: recentMenu.open()
                Menu {
                    id: recentMenu
                    objectName: "recentFilesMenu"
                    y: parent.height
                    Repeater {
                        model: control.model.recentFiles
                        MenuItem {
                            required property var modelData
                            text: modelData.name
                            height: Theme.touchTarget
                            onTriggered: {
                                const error = control.model.load(modelData.path)
                                if (error)
                                    Backend.notify(error, "error")
                            }
                        }
                    }
                }
            }
            Divider {}
            GButton {
                objectName: "reloadFile"
                variant: "ghost"
                iconName: "FaRedo"
                implicitWidth: 60
                enabled: control.model.canReload
                Layout.fillHeight: true
                onClicked: control.model.reload()
            }
            Divider {}
            GButton {
                objectName: "closeFile"
                variant: "ghost"
                iconName: "MdClose"
                iconSize: 24
                implicitWidth: 60
                enabled: control.model.loaded && control.model.canLoad
                Layout.fillHeight: true
                onClicked: confirmClose.open()
            }
        }
    }
}
