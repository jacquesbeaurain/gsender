import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import GSender

// Macros (features/Macros): two columns of macros - tap one to run it
// ("Running..." for a moment), its "..." for Edit and Delete; drag one to
// reorder or move it across (a mouse after 10 px, a finger after holding
// 250 ms) - and Add, Import, Export below.
Item {
    id: tab
    objectName: "macrosTab"

    property MacrosModel model: MacrosModel {}

    MacroForm {
        id: form
        model: tab.model
    }
    ConfirmDialog {
        id: confirmDelete
        objectName: "confirmDeleteMacro"
        property string macroId
        property string macroName
        title: qsTr("Delete Macro")
        message: qsTr("Are you sure you want to delete this macro?") + "\n\n" + macroName
        actionText: qsTr("Delete")
        actionVariant: "error"
        onAccepted: {
            tab.model.remove(macroId)
            Backend.notify(qsTr("Deleted Macro"), "success")
        }
    }
    FileDialog {
        id: importDialog
        title: qsTr("Import Macros")
        nameFilters: [qsTr("Macros (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const result = tab.model.importFile(selectedFile)
            if (result.message)
                Backend.notify(result.message, result.ok ? "success" : "error")
        }
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export Macros")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Macros (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const result = tab.model.exportFile(selectedFile)
            if (!result.ok)
                Backend.notify(result.message, "error")
        }
    }

    // The macro being dragged, and where it would land.
    property string dragId: ""
    property string dragName: ""
    property point dragAt
    function dropTarget(scenePoint) {
        const p = columns.mapFromItem(null, scenePoint.x, scenePoint.y)
        const column = p.x < columns.width / 2 ? "column1" : "column2"
        const list = column === "column1" ? column1 : column2
        let index = 0
        for (let i = 0; i < list.children.length; ++i) {
            const child = list.children[i]
            if (!child.macroId || child.macroId === dragId)
                continue
            const centre = list.mapToItem(columns, 0, child.y + child.height / 2).y
            if (centre < p.y)
                ++index
        }
        return { column: column, index: index }
    }

    Component {
        id: macroDelegate
        Rectangle {
            id: macroItem
            required property var modelData
            readonly property string macroId: modelData.id
            property bool running: false
            width: parent ? parent.width : 0
            height: 48
            radius: Theme.radiusSmall
            opacity: tab.dragId === macroId ? 0.5 : 1
            color: !tab.model.canRun ? (Theme.dark ? Theme.surfaceRaised : Theme.gray[300])
                                     : (Theme.dark ? Theme.surfaceRaised : "white")
            border.color: !tab.model.canRun ? Theme.gray[400] : (Theme.dark ? Theme.outline : Theme.gray[200])

            Timer { id: runTimer; interval: 4000; onTriggered: macroItem.running = false }

            // "Running...": a pulsing green sweep.
            Rectangle {
                anchors.fill: runButton
                anchors.margins: 2
                radius: Theme.radiusSmall
                visible: macroItem.running
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.green[500] }
                    GradientStop { position: 0.5; color: Theme.green[500] }
                    GradientStop { position: 1; color: "#dcfce7" }
                }
                SequentialAnimation on opacity {
                    running: macroItem.running
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 0.5; duration: 1000 }
                    NumberAnimation { from: 0.5; to: 1; duration: 1000 }
                }
            }
            Item {
                id: runButton
                objectName: "macro_" + macroItem.modelData.name
                anchors.left: parent.left
                anchors.right: menuButton.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                opacity: tab.model.canRun ? 1 : 0.5
                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    text: macroItem.running ? qsTr("Running...") : macroItem.modelData.name
                    font.pixelSize: Theme.fontBase
                    color: Theme.contentPrimary
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    pressAndHoldInterval: 250
                    property point pressedAt
                    property bool dragging: false
                    function begin() {
                        dragging = true
                        preventStealing = true
                        tab.dragId = macroItem.macroId
                        tab.dragName = macroItem.modelData.name
                    }
                    onPressed: (mouse) => {
                        pressedAt = Qt.point(mouse.x, mouse.y)
                        // A mouse drags at once; a finger first scrolls.
                        preventStealing = mouse.source === Qt.MouseEventNotSynthesized
                    }
                    onPressAndHold: begin()
                    onPositionChanged: (mouse) => {
                        if (!dragging && mouse.source === Qt.MouseEventNotSynthesized
                                && Math.hypot(mouse.x - pressedAt.x, mouse.y - pressedAt.y) > 10)
                            begin()
                        if (dragging)
                            tab.dragAt = mapToItem(tab, mouse.x, mouse.y)
                    }
                    onReleased: (mouse) => {
                        if (dragging) {
                            const target = tab.dropTarget(mapToItem(null, mouse.x, mouse.y))
                            tab.model.move(tab.dragId, target.column, target.index)
                        }
                        dragging = false
                        tab.dragId = ""
                    }
                    onCanceled: { dragging = false; tab.dragId = "" }
                    onClicked: {
                        if (dragging || !tab.model.canRun)
                            return
                        if (tab.model.run(macroItem.macroId)) {
                            macroItem.running = true
                            runTimer.restart()
                            Backend.notify(qsTr("Started running macro '%1'!").arg(macroItem.modelData.name), "info")
                        }
                    }
                }
            }
            Item {
                id: menuButton
                objectName: "macroMenu_" + macroItem.modelData.name
                anchors.right: parent.right
                width: Theme.touchTarget
                height: parent.height
                Icon { anchors.centerIn: parent; name: "FaEllipsisH"; color: Theme.contentSecondary; width: 20; height: 20 }
                TapHandler { onTapped: menu.popup(menuButton, 0, menuButton.height) }
                Menu {
                    id: menu
                    MenuItem {
                        objectName: "macroEdit"
                        text: qsTr("Edit")
                        icon.source: "image://icon/FaEdit/" + String(Theme.contentPrimary).slice(1)
                        height: Theme.touchTarget
                        onTriggered: form.openFor(macroItem.macroId)
                    }
                    MenuItem {
                        objectName: "macroDelete"
                        text: qsTr("Delete")
                        icon.source: "image://icon/FaTrashAlt/" + String(Theme.contentPrimary).slice(1)
                        height: Theme.touchTarget
                        onTriggered: {
                            confirmDelete.macroId = macroItem.macroId
                            confirmDelete.macroName = macroItem.modelData.name
                            confirmDelete.open()
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: columns.height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            RowLayout {
                id: columns
                width: flick.width
                spacing: 4
                Column {
                    id: column1
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    spacing: 4
                    Repeater { model: tab.model.column1; delegate: macroDelegate }
                }
                Column {
                    id: column2
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    spacing: 4
                    Repeater { model: tab.model.column2; delegate: macroDelegate }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: tab.model.count === 0
                text: qsTr("No Macros...")
                color: Theme.contentPrimary
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GButton {
                objectName: "macroAdd"
                Layout.fillWidth: true
                text: qsTr("Add")
                iconName: "FaPlus"
                iconSize: 14
                fontSize: Theme.fontSm
                onClicked: form.openFor("")
            }
            GButton {
                objectName: "macroImport"
                Layout.fillWidth: true
                text: qsTr("Import")
                iconName: "FaFileImport"
                iconSize: 14
                fontSize: Theme.fontSm
                onClicked: importDialog.open()
            }
            GButton {
                objectName: "macroExport"
                Layout.fillWidth: true
                text: qsTr("Export")
                iconName: "FaFileExport"
                iconSize: 14
                fontSize: Theme.fontSm
                onClicked: {
                    if (tab.model.count === 0) {
                        Backend.notify(qsTr("No Macros to Export"), "error")
                        return
                    }
                    exportDialog.currentFile = "file:///" + tab.model.exportName()
                    exportDialog.open()
                }
            }
        }
    }

    // What is being dragged, under the finger.
    Rectangle {
        visible: tab.dragId !== ""
        x: tab.dragAt.x - width / 2
        y: tab.dragAt.y - height / 2
        z: 10
        width: column1.width
        height: 48
        radius: Theme.radiusSmall
        color: Theme.dark ? Theme.surfaceElevated : "white"
        border.color: Theme.robin[500]
        Label {
            anchors.fill: parent
            anchors.leftMargin: 10
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: tab.dragName
            color: Theme.contentPrimary
        }
    }
}
