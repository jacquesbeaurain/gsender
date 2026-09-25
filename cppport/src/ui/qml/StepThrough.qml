import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// G-code Step Through (features/GcodeStepper): the source (searchable, tap a
// line to go to it), the toolpath at the line with the current line's full
// text beneath, the tools with their eye buttons; the scrubber, the step
// controls (play at 0.5x-100x of the file's pace, reset, +-100/+-1000 lines)
// and the status (position, modals - those the line changed marked - and
// hiding the lines already run). Landscape: three columns; portrait: the
// visualizer on top.
Popup {
    id: dialog
    objectName: "stepThrough"

    property StepThroughModel model: StepThroughModel {}
    readonly property bool portrait: height > width

    function openFile() {
        model.load()
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: parent ? parent.width * 0.92 : 1200
    height: parent ? parent.height * 0.9 : 800
    modal: true
    padding: 16
    onClosed: model.stopPlaying()
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.5) }
    background: Rectangle {
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceElevated : "white"
        border.color: Theme.outlineSubtle
    }

    component SectionTitle: Label {
        font.pixelSize: Theme.fontXs
        font.letterSpacing: 1
        color: Theme.contentMuted
    }
    component Panel: Rectangle {
        radius: Theme.radius
        color: Theme.dark ? Theme.surfaceRaised : Theme.gray[50]
        border.color: Theme.dark ? Theme.outline : Theme.gray[200]
    }

    contentItem: ColumnLayout {
        spacing: 12

        // The title and a close button big enough for a finger.
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("G-code Step Through")
                font.pixelSize: Theme.fontLg
                font.bold: true
                color: Theme.contentPrimary
                Layout.fillWidth: true
            }
            Item {
                objectName: "stepThroughClose"
                width: Theme.touchTarget
                height: Theme.touchTarget
                Icon { anchors.centerIn: parent; name: "LuX"; color: Theme.contentMuted; width: 24; height: 24 }
                TapHandler { onTapped: dialog.close() }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: dialog.portrait ? 2 : 3
            rowSpacing: 12
            columnSpacing: 12

            // The source.
            Panel {
                // Landscape: a fifth of the width each side; portrait: halves.
                Layout.fillWidth: dialog.portrait
                Layout.fillHeight: true
                Layout.preferredWidth: dialog.portrait ? 1 : Math.max(170, dialog.availableWidth / 5)
                Layout.row: dialog.portrait ? 1 : 0
                Layout.column: 0
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        SectionTitle {
                            text: qsTr("G-code (%1 lines)").arg(Number(dialog.model.total).toLocaleString(Qt.locale(), "f", 0)).toUpperCase()
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Item {
                            objectName: "stepSearchToggle"
                            width: 36; height: 36
                            Icon { anchors.centerIn: parent; name: "LuSearch"; color: searchRow.visible ? Theme.blue[500] : Theme.contentMuted; width: 16; height: 16 }
                            TapHandler {
                                onTapped: {
                                    searchRow.visible = !searchRow.visible
                                    if (searchRow.visible)
                                        search.forceActiveFocus()
                                    else {
                                        search.text = ""
                                        dialog.model.setSearch("")
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        id: searchRow
                        visible: false
                        Layout.fillWidth: true
                        TextField {
                            id: search
                            objectName: "stepSearch"
                            Layout.fillWidth: true
                            implicitHeight: 36
                            placeholderText: qsTr("Search...")
                            color: Theme.contentPrimary
                            font.pixelSize: Theme.fontSm
                            onTextChanged: dialog.model.setSearch(text)
                            onAccepted: dialog.model.goToNextMatch()
                        }
                        Label {
                            objectName: "stepMatchCount"
                            text: dialog.model.searching ? dialog.model.matchCount.toLocaleString(Qt.locale(), "f", 0) : "—"
                            font.pixelSize: Theme.fontXs
                            color: Theme.contentMuted
                        }
                    }
                    ListView {
                        id: source
                        objectName: "stepSource"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: dialog.model.source
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        readonly property int gutter: String(dialog.model.total).length
                        delegate: Rectangle {
                            required property int number
                            required property string code
                            required property bool current
                            required property bool matched
                            width: ListView.view.width
                            height: 28
                            color: current ? Qt.rgba(0x3b / 255, 0x82 / 255, 0xf6 / 255, 0.27)
                                 : matched ? Qt.rgba(0xfa / 255, 0xcc / 255, 0x15 / 255, 0.31) : "transparent"
                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                x: 4
                                spacing: 8
                                Label {
                                    text: (parent.parent.current ? "›" : " ") + " " + String(parent.parent.number).padStart(source.gutter, " ")
                                    font.family: Theme.monoFont
                                    font.pixelSize: Theme.fontXs
                                    font.bold: parent.parent.current
                                    color: parent.parent.current ? (Theme.dark ? "#bfdbfe" : "#1d4ed8") : Theme.gray[400]
                                }
                                Label {
                                    text: parent.parent.code
                                    textFormat: Text.StyledText
                                    font.family: Theme.monoFont
                                    font.pixelSize: Theme.fontXs
                                    font.bold: parent.parent.current
                                }
                            }
                            TapHandler { onTapped: dialog.model.goToLine(parent.number) }
                        }
                        // Re-centred only when the row has left the view.
                        function follow() {
                            const row = dialog.model.line - 1
                            const top = row * 28
                            if (top < contentY || top + 28 > contentY + height)
                                positionViewAtIndex(row, ListView.Center)
                        }
                        Connections {
                            target: dialog.model
                            function onLineChanged() { if (!scrubber.pressed) source.follow() }
                        }
                    }
                    GButton {
                        objectName: "stepGoToStart"
                        Layout.fillWidth: true
                        text: qsTr("Go to start")
                        fontSize: Theme.fontSm
                        onClicked: dialog.model.goToLine(1)
                    }
                }
            }

            // The toolpath and the current line.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: dialog.portrait ? 3 : -1
                Layout.row: 0
                Layout.column: dialog.portrait ? 0 : 1
                Layout.columnSpan: dialog.portrait ? 2 : 1
                spacing: 8
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radius
                    clip: true
                    color: "transparent"
                    StepToolpathItem {
                        id: stepView
                        objectName: "stepToolpath"
                        anchors.fill: parent
                        model: dialog.model
                    }
                    ToolpathGestures {
                        anchors.fill: parent
                        view: stepView
                    }
                    Rectangle {
                        visible: !dialog.model.indexReady && dialog.model.total > 0
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        width: reading.implicitWidth + 16
                        height: reading.implicitHeight + 8
                        radius: 4
                        color: Qt.rgba(0, 0, 0, 0.6)
                        Label {
                            id: reading
                            anchors.centerIn: parent
                            text: qsTr("Reading positions… %1%").arg(dialog.model.indexProgress)
                            font.pixelSize: Theme.fontXs
                            color: "white"
                        }
                    }
                }
                // The whole current line, refreshed at rest.
                Rectangle {
                    id: readout
                    objectName: "stepLineReadout"
                    readonly property bool idle: !dialog.model.playing && !scrubber.pressed
                    property int number: 1
                    property string lineText: ""
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(96, readoutRow.implicitHeight + 16)
                    radius: Theme.radius
                    opacity: idle ? 1 : 0.5
                    color: Theme.dark ? Theme.surfaceSunken : "white"
                    border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                    function refresh() {
                        if (idle) {
                            number = dialog.model.line
                            lineText = dialog.model.lineText
                        }
                    }
                    onIdleChanged: refresh()
                    Connections {
                        target: dialog.model
                        function onLineChanged() { readout.refresh() }
                    }
                    RowLayout {
                        id: readoutRow
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8
                        Label {
                            text: readout.number
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.fontXs
                            color: Theme.contentMuted
                            Layout.alignment: Qt.AlignTop
                        }
                        Label {
                            objectName: "stepLineText"
                            text: readout.lineText
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.fontXs
                            wrapMode: Text.WrapAnywhere
                            color: Theme.contentSecondary
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            // The tools.
            Panel {
                Layout.fillWidth: dialog.portrait
                Layout.fillHeight: true
                Layout.preferredWidth: dialog.portrait ? 1 : Math.max(180, dialog.availableWidth / 5)
                Layout.row: dialog.portrait ? 1 : 0
                Layout.column: dialog.portrait ? 1 : 2
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    SectionTitle { text: qsTr("Tools").toUpperCase() }
                    Label {
                        visible: dialog.model.tools.length === 0
                        text: qsTr("No tools found in this file.")
                        font.pixelSize: Theme.fontSm
                        color: Theme.contentMuted
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: dialog.model.tools.length
                        delegate: Rectangle {
                            id: card
                            required property int index
                            readonly property var tool: dialog.model.tools[index] || ({})
                            readonly property bool active: index === dialog.model.activeTool
                            objectName: "stepTool_" + index
                            width: ListView.view.width
                            height: Math.max(52, cardRow.implicitHeight + 12)
                            radius: 6
                            opacity: tool.hidden ? 0.5 : 1
                            color: Theme.dark ? Theme.surfaceElevated : "white"
                            border.width: active ? 2 : 1
                            border.color: active ? tool.color : (Theme.dark ? Theme.outline : Theme.gray[300])
                            Rectangle { width: 4; height: parent.height; radius: 2; color: card.tool.color || "transparent" }
                            TapHandler { onTapped: dialog.model.goToLine(card.tool.startLine) }
                            RowLayout {
                                id: cardRow
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                spacing: 6
                                Rectangle {
                                    width: 28; height: 28; radius: 4
                                    color: card.tool.color || "transparent"
                                    Label { anchors.centerIn: parent; text: card.tool.index || ""; font.bold: true; color: card.tool.textColor || "white" }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1  // what is left beside the badge, range and eye
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true
                                        textFormat: Text.StyledText
                                        wrapMode: Text.Wrap
                                        text: "<b>T" + card.tool.number + "</b>"
                                              + (card.tool.comment ? " <font color=\"#8b95a1\">· " + card.tool.comment + "</font>" : "")
                                        font.pixelSize: Theme.fontSm
                                        color: Theme.contentPrimary
                                    }
                                    Label {
                                        visible: !!card.tool.details
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: card.tool.details || ""
                                        font.pixelSize: Theme.fontXs
                                        color: Theme.contentMuted
                                    }
                                }
                                Label {
                                    text: card.tool.range || ""
                                    font.family: Theme.monoFont
                                    font.pixelSize: Theme.fontXs
                                    color: Theme.contentMuted
                                }
                                // The eye: shown or hidden.
                                Rectangle {
                                    objectName: "stepToolEye_" + card.index
                                    width: Theme.touchTarget; height: 32; radius: 4
                                    color: card.tool.hidden ? "transparent" : (card.tool.color || "transparent")
                                    border.color: Theme.dark ? Theme.outline : Theme.gray[300]
                                    Icon {
                                        anchors.centerIn: parent
                                        name: card.tool.hidden ? "LuEyeOff" : "LuEye"
                                        color: card.tool.hidden ? Theme.contentMuted : (card.tool.textColor || "white")
                                        width: 16; height: 16
                                    }
                                    TapHandler { onTapped: dialog.model.toggleTool(card.index) }
                                }
                            }
                        }
                    }
                }
            }
        }

        // The scrubber.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Slider {
                id: scrubber
                objectName: "stepScrubber"
                Layout.fillWidth: true
                from: 1
                to: Math.max(1, dialog.model.total)
                stepSize: 1
                snapMode: Slider.SnapAlways
                enabled: dialog.model.total > 0
                value: dialog.model.line
                onMoved: {
                    dialog.model.stopPlaying()
                    dialog.model.goToLine(value)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: dialog.model.total > 0 ? dialog.model.quarterLabels() : []
                    Label {
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        horizontalAlignment: index === 0 ? Text.AlignLeft : index === 4 ? Text.AlignRight : Text.AlignHCenter
                        text: modelData
                        font.pixelSize: Theme.fontXs
                        color: Theme.contentMuted
                    }
                }
            }
            Label {
                objectName: "stepPosition"
                Layout.alignment: Qt.AlignHCenter
                textFormat: Text.StyledText
                text: "<b>" + dialog.model.line.toLocaleString(Qt.locale(), "f", 0) + "</b> / "
                      + dialog.model.total.toLocaleString(Qt.locale(), "f", 0)
                color: Theme.contentPrimary
            }
        }

        // The step controls and the status.
        GridLayout {
            Layout.fillWidth: true
            columns: dialog.portrait ? 1 : 2
            columnSpacing: 16
            rowSpacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                SectionTitle { text: qsTr("Step Controls").toUpperCase() }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    GButton {
                        objectName: "stepPlay"
                        variant: dialog.model.playing ? "primary" : "secondary"
                        iconName: dialog.model.playing ? "LuPause" : "LuPlay"
                        enabled: dialog.model.total > 0
                        onClicked: dialog.model.togglePlay()
                    }
                    GButton {
                        objectName: "stepReset"
                        iconName: "LuRotateCcw"
                        enabled: dialog.model.total > 0 && dialog.model.line !== 1
                        onClicked: dialog.model.reset()
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: speedRow.implicitHeight + 8
                        radius: Theme.radius
                        color: "transparent"
                        border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                        RowLayout {
                            id: speedRow
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 4
                            Repeater {
                                model: [0.5, 1, 10, 100]
                                GButton {
                                    required property real modelData
                                    objectName: "stepSpeed_" + modelData
                                    Layout.fillWidth: true
                                    text: modelData + "x"
                                    fontSize: Theme.fontSm
                                    variant: dialog.model.speed === modelData ? "primary" : "ghost"
                                    onClicked: dialog.model.speed = modelData
                                }
                            }
                        }
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 8
                    Repeater {
                        model: [-1000, -100, 100, 1000]
                        GButton {
                            required property int modelData
                            objectName: "stepBy_" + modelData
                            Layout.fillWidth: true
                            text: (modelData > 0 ? "+" : "") + modelData
                            iconName: modelData < 0 ? "LuChevronsLeft" : "LuChevronsRight"
                            fontSize: Theme.fontSm
                            enabled: modelData < 0 ? dialog.model.line > 1 : dialog.model.line < dialog.model.total
                            onClicked: dialog.model.step(modelData)
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                SectionTitle { text: dialog.model.positionTitle.toUpperCase() }
                Label {
                    objectName: "stepPositionValues"
                    text: dialog.model.positionText
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.fontSm
                    color: Theme.contentPrimary
                }
                SectionTitle { text: qsTr("Modals").toUpperCase() }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 6
                    rowSpacing: 4
                    columnSpacing: 4
                    Repeater {
                        model: dialog.model.modals.length
                        Rectangle {
                            required property int index
                            readonly property var modal: dialog.model.modals[index] || ({})
                            Layout.fillWidth: true
                            implicitHeight: modalColumn.implicitHeight + 6
                            radius: 3
                            color: modal.changed ? Qt.rgba(0x3b / 255, 0x82 / 255, 0xf6 / 255, 0.2) : "transparent"
                            border.color: modal.changed ? "#3b82f6" : (Theme.dark ? Theme.outline : Theme.gray[300])
                            Column {
                                id: modalColumn
                                anchors.centerIn: parent
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: parent.parent.modal.label || ""; font.pixelSize: 9; color: Theme.contentMuted }
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: parent.parent.modal.value || ""
                                    font.family: Theme.monoFont
                                    font.pixelSize: Theme.fontXs
                                    font.bold: !!parent.parent.modal.changed
                                    color: Theme.contentPrimary
                                }
                            }
                        }
                    }
                }
                GButton {
                    objectName: "stepHideProcessed"
                    text: dialog.model.hideProcessed ? qsTr("Show prior lines") : qsTr("Hide prior lines")
                    active: dialog.model.hideProcessed
                    fontSize: Theme.fontSm
                    iconName: dialog.model.hideProcessed ? "LuEye" : "LuEyeOff"
                    iconSize: 16
                    onClicked: dialog.model.hideProcessed = !dialog.model.hideProcessed
                }
            }
        }
    }
}
