import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import GSender

// SD Card (features/SDCard): the card's status, Refresh Files and Upload
// (or the upload's progress); the files - each run or deleted (asked
// first), the tool changer's macros and those the firmware refuses marked.
// Files dropped on the page go up too.
ToolPage {
    id: tool
    objectName: "sdCardTool"
    title: qsTr("SD Card")

    property SdCardModel model

    Component.onCompleted: model.opened()

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        // StatusIndicator.
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: 64
                radius: Theme.radius
                color: Theme.dark ? Theme.surfaceRaised : "white"
                border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    Label { text: qsTr("SD Card Status:"); font.bold: true; color: Theme.contentPrimary; Layout.fillWidth: true }
                    Rectangle {
                        readonly property string state: tool.model.status === qsTr("Mounted") ? "mounted"
                                                        : tool.model.status === qsTr("Unmounted") ? "unmounted" : "none"
                        implicitWidth: statusLabel.implicitWidth + 28
                        implicitHeight: 30
                        radius: 15
                        color: "transparent"
                        border.width: 2
                        border.color: state === "mounted" ? "#bbf7d0" : state === "unmounted" ? "#fecaca" : Theme.gray[300]
                        Label {
                            id: statusLabel
                            objectName: "sdStatus"
                            anchors.centerIn: parent
                            text: tool.model.status
                            font.bold: true
                            color: parent.state === "mounted" ? "#15803d" : parent.state === "unmounted" ? "#b91c1c" : Theme.contentSecondary
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: 64
                radius: Theme.radius
                color: Theme.dark ? Theme.surfaceRaised : "white"
                border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    visible: tool.model.uploadState === "idle"
                    GButton {
                        objectName: "sdRefresh"
                        Layout.fillWidth: true
                        iconName: "LuRefreshCw"
                        text: qsTr("Refresh Files")
                        enabled: tool.model.available
                        onClicked: tool.model.refreshFiles()
                    }
                    GButton {
                        objectName: "sdUpload"
                        Layout.fillWidth: true
                        variant: "primary"
                        iconName: "LuUpload"
                        text: qsTr("Upload")
                        enabled: tool.model.available && tool.model.mounted
                        onClicked: uploadModal.open()
                    }
                }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: tool.model.uploadState === "uploading"
                    spacing: 4
                    Label {
                        objectName: "sdUploadProgress"
                        text: qsTr("Uploading... %1%").arg(tool.model.uploadProgress)
                        color: Theme.contentSecondary
                        font.pixelSize: Theme.fontSm
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 8
                        radius: 4
                        color: Theme.gray[200]
                        Rectangle {
                            width: parent.width * tool.model.uploadProgress / 100
                            height: parent.height
                            radius: 4
                            color: Theme.blue[500]
                        }
                    }
                }
                Label {
                    objectName: "sdUploadComplete"
                    anchors.centerIn: parent
                    visible: tool.model.uploadState === "complete"
                    text: "✔ " + qsTr("Upload complete!")
                    font.bold: true
                    color: Theme.green[500]
                }
            }
        }

        // FileList: a message, or the files.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.dark ? Theme.surfaceRaised : "white"
            border.color: dropArea.containsDrag ? Theme.blue[500] : (Theme.dark ? Theme.outline : Theme.gray[200])
            border.width: dropArea.containsDrag ? 2 : 1
            clip: true

            ColumnLayout {
                visible: tool.model.message !== ""
                anchors.centerIn: parent
                width: parent.width - 32
                spacing: 6
                Icon {
                    Layout.alignment: Qt.AlignHCenter
                    name: "LuHardDrive"
                    color: Theme.gray[400]
                    width: 40; height: 40
                }
                Label {
                    objectName: "sdMessage"
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: tool.model.message
                    font.bold: tool.model.available
                    font.pixelSize: Theme.fontLg
                    color: Theme.contentPrimary
                }
                Label {
                    visible: tool.model.available
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Upload files or refresh to see SD card contents")
                    color: Theme.contentMuted
                }
            }

            ColumnLayout {
                visible: tool.model.message === ""
                anchors.fill: parent
                anchors.margins: 1
                spacing: 0
                Label {
                    objectName: "sdFilesTitle"
                    Layout.margins: 16
                    text: qsTr("Files (%1)").arg(tool.model.files.length)
                    font.bold: true
                    font.pixelSize: Theme.fontLg
                    color: Theme.contentPrimary
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    color: Theme.dark ? Theme.surfaceElevated : Theme.gray[100]
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 12
                        Label { text: qsTr("File Name"); font.bold: true; color: Theme.contentPrimary; Layout.fillWidth: true }
                        Label { text: qsTr("Size"); font.bold: true; color: Theme.contentPrimary; Layout.preferredWidth: 90 }
                        Label { text: qsTr("Actions"); font.bold: true; color: Theme.contentPrimary; Layout.preferredWidth: 220 }
                    }
                }
                ListView {
                    id: fileList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    // By count, so state changes keep the list where it is.
                    model: tool.model.files.length
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: fileRow
                        required property int index
                        readonly property var file: tool.model.files[index] || ({})
                        objectName: "sdFile_" + (file.name || "")
                        width: fileList.width
                        implicitHeight: 56
                        color: file.atci ? "#fefce8" : index % 2 ? (Theme.dark ? Theme.surfaceElevated : Theme.gray[50]) : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 12
                            Label {
                                text: fileRow.file.name || ""
                                color: fileRow.file.atci ? "#374151" : Theme.contentPrimary
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Label {
                                visible: !!fileRow.file.atci
                                text: qsTr("ATC Macro")
                                font.italic: true
                                font.pixelSize: Theme.fontSm
                                color: "#374151"
                            }
                            Rectangle {
                                visible: !!fileRow.file.unusable
                                implicitWidth: unusableLabel.implicitWidth + 12
                                implicitHeight: 20
                                radius: 4
                                color: "#fef2f2"
                                Label {
                                    id: unusableLabel
                                    anchors.centerIn: parent
                                    text: qsTr("Unusable")
                                    font.pixelSize: Theme.fontXs
                                    font.bold: true
                                    color: Theme.red[500]
                                }
                                ToolTip.visible: unusableHover.hovered
                                ToolTip.text: fileRow.file.problem || ""
                                HoverHandler { id: unusableHover }
                            }
                            Label {
                                text: fileRow.file.size || ""
                                color: Theme.contentSecondary
                                font.pixelSize: Theme.fontSm
                                Layout.preferredWidth: 90
                            }
                            RowLayout {
                                Layout.preferredWidth: 220
                                spacing: 8
                                GButton {
                                    objectName: "sdRun_" + (fileRow.file.name || "")
                                    iconName: "LuPlay"
                                    text: qsTr("Run")
                                    enabled: !!fileRow.file.runnable
                                    onClicked: tool.model.runFile(fileRow.file.name)
                                }
                                GButton {
                                    objectName: "sdDelete_" + (fileRow.file.name || "")
                                    variant: "error"
                                    iconName: "LuTrash2"
                                    text: qsTr("Delete")
                                    enabled: !!fileRow.file.deletable
                                    onClicked: {
                                        const name = fileRow.file.name
                                        deleteConfirm.name = name
                                        deleteConfirm.open()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            DropArea {
                id: dropArea
                anchors.fill: parent
                enabled: tool.model.available && tool.model.uploadState === "idle"
                onDropped: (drop) => { tool.model.upload(drop.urls); drop.accept() }
            }
        }
    }

    ConfirmDialog {
        id: deleteConfirm
        objectName: "sdDeleteConfirm"
        property string name
        title: qsTr("Delete File")
        message: qsTr("Are you sure you want to delete %1?").arg(name)
        actionText: qsTr("Delete")
        actionVariant: "error"
        onAccepted: tool.model.deleteFile(name)
    }

    // UploadModal: files browsed for or dropped in, each with its size and
    // a remove button; Upload sends them all.
    Popup {
        id: uploadModal
        objectName: "sdUploadModal"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        padding: 24
        width: Math.min(480, parent ? parent.width - 32 : 480)
        onClosed: tool.model.clearPending()
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
        background: Rectangle {
            radius: Theme.radius
            color: Theme.dark ? Theme.surfaceElevated : "white"
            border.color: Theme.outlineSubtle
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: qsTr("Upload Files"); font.pixelSize: Theme.fontLg; font.bold: true; color: Theme.contentPrimary }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 120
                radius: 8
                color: modalDrop.containsDrag ? "#eff6ff" : "transparent"
                border.color: modalDrop.containsDrag ? Theme.blue[500] : Theme.gray[300]
                border.width: 2
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    Icon { Layout.alignment: Qt.AlignHCenter; name: "LuUpload"; color: Theme.gray[400]; width: 28; height: 28 }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Drop files here, or")
                        color: Theme.contentMuted
                    }
                    GButton {
                        objectName: "sdBrowse"
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Browse Files")
                        onClicked: browse.open()
                    }
                }
                DropArea {
                    id: modalDrop
                    anchors.fill: parent
                    onDropped: (drop) => { tool.model.addPending(drop.urls); drop.accept() }
                }
            }
            Label {
                visible: tool.model.pending.length > 0
                text: qsTr("Selected Files (%1)").arg(tool.model.pending.length)
                font.bold: true
                color: Theme.contentPrimary
            }
            Repeater {
                model: tool.model.pending
                RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 8
                    Label { text: modelData.name; color: Theme.contentPrimary; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    Label { text: modelData.size; color: Theme.contentMuted; font.pixelSize: Theme.fontSm }
                    GButton {
                        objectName: "sdPendingRemove_" + index
                        variant: "ghost"
                        iconName: "LuX"
                        onClicked: tool.model.removePending(index)
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: 8
                spacing: 8
                GButton {
                    variant: "outline"
                    text: qsTr("Cancel")
                    onClicked: uploadModal.close()
                }
                GButton {
                    objectName: "sdUploadPending"
                    variant: "primary"
                    text: qsTr("Upload (%1)").arg(tool.model.pending.length)
                    enabled: tool.model.pending.length > 0
                    onClicked: {
                        tool.model.uploadPending()
                        uploadModal.close()
                    }
                }
            }
        }
    }
    FileDialog {
        id: browse
        title: qsTr("Upload Files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [tool.model.fileFilter]
        onAccepted: tool.model.addPending(selectedFiles)
    }
}
