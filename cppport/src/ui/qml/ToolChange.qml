import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Tool changes (features/Helper/Wizard): the wizard floats over the page's
// left two thirds - the steps down its side, the substep's instructions and
// actions, Back, the progress dots and Next/Complete - or shrinks to a pill
// over the visualizer; the first tool's question; the "Code" strategy's
// change-then-continue prompt.
Item {
    id: root
    objectName: "toolChange"

    property ToolChangeModel model: ToolChangeModel {}
    property bool minimized: false

    Connections {
        target: root.model
        function onFirstToolQuestion(comment) {
            firstTool.comment = comment
            firstTool.open()
        }
        function onCodeChangeWaiting(comment) {
            codeChange.comment = comment
            codeChange.open()
        }
        function onChanged() { if (!root.model.active) root.minimized = false }
    }

    ConfirmDialog {
        id: firstTool
        objectName: "firstToolPrompt"
        property string comment
        closePolicy: Popup.NoAutoClose  // answered one way or the other
        title: qsTr("First Tool Change Detected")
        message: qsTr("A toolchange command was detected at the start of your file.") + "\n\n"
                 + qsTr("If you already have the correct tool installed, you only need to probe your initial tool. Otherwise, run the toolchange routine to install and set up your initial tool.")
                 + (comment ? "\n\n" + qsTr("Comment: %1").arg(comment) : "")
        actionText: qsTr("Run Full Toolchange Routine")
        cancelText: qsTr("Only Probe Tool Length")
        onAccepted: root.model.answerFirstTool(true)
        onRejected: root.model.answerFirstTool(false)
    }
    ConfirmDialog {
        id: codeChange
        objectName: "codeChangePrompt"
        property string comment
        closePolicy: Popup.NoAutoClose
        title: qsTr("Tool Change")
        message: qsTr("Change the tool, then continue the job.") + (comment ? "\n\n" + comment : "")
        actionText: qsTr("Continue")
        onAccepted: root.model.continueCodeChange()
    }
    ConfirmDialog {
        id: confirmCancel
        objectName: "confirmCancelToolChange"
        title: qsTr("Cancel Toolchange Wizard")
        message: qsTr("Are you sure you want to cancel the toolchange wizard? All steps will be lost.")
        actionText: qsTr("Yes")
        onAccepted: root.model.cancel()
    }

    component TitleButton: Rectangle {
        property string iconName
        signal tapped()
        width: 32
        height: 32
        radius: 4
        color: "transparent"
        border.color: Theme.dark ? Theme.outline : Theme.gray[300]
        Icon { anchors.centerIn: parent; name: parent.iconName; color: Theme.contentMuted; width: 14; height: 14 }
        TapHandler { onTapped: parent.tapped() }
    }

    // Minimised: a pill at the top of the visualizer.
    Rectangle {
        objectName: "toolChangePill"
        visible: root.model.active && root.minimized
        x: (root.width * 0.67 - width) / 2
        y: 72
        width: pillRow.implicitWidth + 24
        height: 44
        radius: height / 2
        color: Theme.dark ? Theme.surfaceElevated : Qt.rgba(1, 1, 1, 0.9)
        border.color: Theme.outline
        RowLayout {
            id: pillRow
            anchors.centerIn: parent
            spacing: 8
            Icon { name: "LuWrench"; color: Theme.contentMuted; width: 14; height: 14 }
            Label { text: root.model.title; font.pixelSize: Theme.fontXs; font.weight: Font.Medium; color: Theme.contentPrimary }
            TitleButton { objectName: "toolChangeRestore"; iconName: "LuChevronDown"; onTapped: root.minimized = false }
            TitleButton { iconName: "LuX"; onTapped: confirmCancel.open() }
        }
    }

    // The wizard.
    Rectangle {
        id: panel
        objectName: "toolChangeWizard"
        visible: root.model.active && !root.minimized
        width: Math.min(860, root.width * 0.67 - 16)
        height: Math.min(500, root.height - 80)
        x: (root.width * 0.67 - width) / 2
        y: (root.height - height) / 2
        radius: Theme.radius
        clip: true
        color: Theme.dark ? Theme.surfaceRaised : "white"
        border.color: Theme.outline

        // Blocks what is beneath it.
        TapHandler {}

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0

            // The title bar.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                color: Theme.dark ? Theme.surfaceBase : Theme.gray[100]
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    spacing: 8
                    Icon { name: "LuWrench"; color: Theme.contentMuted; width: 14; height: 14 }
                    Label {
                        text: root.model.title
                        font.pixelSize: Theme.fontBase
                        font.weight: Font.DemiBold
                        color: Theme.contentPrimary
                        Layout.fillWidth: true
                    }
                    TitleButton { objectName: "toolChangeMinimize"; iconName: "FaMinus"; onTapped: root.minimized = true }
                    TitleButton { objectName: "toolChangeCancel"; iconName: "LuX"; onTapped: confirmCancel.open() }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // The steps.
                Rectangle {
                    Layout.preferredWidth: 230
                    Layout.fillHeight: true
                    color: Theme.dark ? Theme.surfaceBase : Theme.gray[50]
                    Flickable {
                        anchors.fill: parent
                        contentHeight: stepColumn.implicitHeight
                        clip: true
                        Column {
                            id: stepColumn
                            width: parent.width
                            Repeater {
                                model: root.model.steps.length
                                Column {
                                    id: stepItem
                                    required property int index
                                    readonly property var step: root.model.steps[index] || ({ title: "", substeps: [] })
                                    readonly property bool done: index < root.model.step
                                    readonly property bool current: index === root.model.step
                                    width: stepColumn.width
                                    Row {
                                        leftPadding: 12
                                        topPadding: 10
                                        bottomPadding: 6
                                        spacing: 6
                                        Rectangle {
                                            width: 18; height: 18; radius: 9
                                            color: stepItem.done ? "#d1fae5" : stepItem.current ? "#dbeafe" : Theme.gray[200]
                                            Label {
                                                anchors.centerIn: parent
                                                visible: !stepItem.done
                                                text: stepItem.index + 1
                                                font.pixelSize: 10
                                                color: stepItem.current ? Theme.blue[700] : Theme.gray[500]
                                            }
                                            Icon {
                                                anchors.centerIn: parent
                                                visible: stepItem.done
                                                name: "FaCheck"; color: "#047857"; width: 9; height: 9
                                            }
                                        }
                                        Label {
                                            width: stepColumn.width - 48
                                            wrapMode: Text.Wrap
                                            text: stepItem.step.title
                                            font.pixelSize: Theme.fontXs
                                            font.weight: Font.Medium
                                            color: stepItem.done ? "#059669" : stepItem.current ? Theme.blue[700] : Theme.gray[400]
                                        }
                                    }
                                    // Substeps, when there are several.
                                    Repeater {
                                        model: stepItem.step.substeps.length > 1 ? stepItem.step.substeps : []
                                        Rectangle {
                                            required property string modelData
                                            required property int index
                                            readonly property bool active: stepItem.current && index === root.model.substep
                                            readonly property bool done: stepItem.done || (stepItem.current && index < root.model.substep)
                                            width: stepColumn.width
                                            height: 30
                                            color: active ? (Theme.dark ? Theme.surfaceHover : "#eff6ff") : "transparent"
                                            Rectangle {
                                                width: 2; height: parent.height
                                                color: parent.active ? Theme.blue[500] : "transparent"
                                            }
                                            Rectangle {
                                                x: 18; anchors.verticalCenter: parent.verticalCenter
                                                width: 6; height: 6; radius: 3
                                                color: parent.done ? "#10b981" : parent.active ? Theme.blue[500] : Theme.gray[300]
                                            }
                                            Label {
                                                x: 32; anchors.verticalCenter: parent.verticalCenter
                                                width: parent.width - 40
                                                elide: Text.ElideRight
                                                text: modelData
                                                font.pixelSize: Theme.fontXs
                                                font.weight: parent.active ? Font.Medium : Font.Normal
                                                color: parent.active ? Theme.blue[700] : parent.done ? Theme.gray[500] : Theme.gray[400]
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.outlineSubtle }

                // The instructions.
                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: instructions.implicitHeight + 32
                    clip: true
                    ColumnLayout {
                        id: instructions
                        x: 16
                        y: 16
                        width: parent.width - 32
                        spacing: 12
                        Label {
                            text: (root.model.steps[root.model.step] || {}).title + "  ›  " + (root.model.current.title || "")
                            font.pixelSize: Theme.fontXs
                            color: Theme.contentMuted
                        }
                        // The wizard's warning, on the first substep.
                        Rectangle {
                            visible: root.model.intro !== "" && root.model.step === 0 && root.model.substep === 0
                                     && !root.model.current.toolBanner
                            Layout.fillWidth: true
                            implicitHeight: introText.implicitHeight + 16
                            radius: 4
                            color: Theme.dark ? "#052e16" : "#ecfdf5"
                            Label {
                                id: introText
                                anchors.fill: parent
                                anchors.margins: 8
                                wrapMode: Text.Wrap
                                text: root.model.intro
                                font.pixelSize: Theme.fontSm
                                color: Theme.dark ? "#6ee7b7" : "#065f46"
                            }
                        }
                        // Install New Tool.
                        Rectangle {
                            objectName: "toolBanner"
                            visible: !!root.model.current.toolBanner && root.model.toolLabel !== ""
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: 320
                            implicitHeight: bannerColumn.implicitHeight + 24
                            radius: 8
                            color: Theme.dark ? "#0d2518" : "#ecfdf5"
                            border.color: "#6ee7b7"
                            Column {
                                id: bannerColumn
                                anchors.centerIn: parent
                                spacing: 4
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: qsTr("INSTALL NEW TOOL")
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    font.letterSpacing: 2
                                    color: "#047857"
                                }
                                Label {
                                    objectName: "toolBannerTool"
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: root.model.toolLabel
                                    font.pixelSize: 28
                                    font.bold: true
                                    color: Theme.contentPrimary
                                }
                                Label {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    visible: root.model.comment !== ""
                                    text: root.model.comment
                                    font.pixelSize: Theme.fontSm
                                    color: Theme.contentSecondary
                                }
                            }
                        }
                        Label {
                            text: (root.model.current.title || "").toUpperCase()
                            font.pixelSize: Theme.fontXs
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.5
                            color: Theme.dark ? "#fbbf24" : Theme.gray[500]
                        }
                        Label {
                            objectName: "toolChangeDescription"
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: root.model.current.description || ""
                            font.pixelSize: Theme.fontSm
                            color: Theme.contentSecondary
                        }
                        // The actions.
                        Repeater {
                            model: root.model.current.actions || []
                            GButton {
                                required property string modelData
                                required property int index
                                objectName: "toolChangeAction_" + index
                                Layout.fillWidth: true
                                variant: root.model.currentDone ? "success" : "primary"
                                iconName: root.model.currentDone ? "FaCheck" : ""
                                iconSize: 14
                                text: modelData
                                enabled: !root.model.running && root.model.ready
                                onClicked: root.model.runAction(index)
                            }
                        }
                        Label {
                            visible: root.model.running
                                     || (!root.model.ready && (root.model.current.actions || []).length > 0)
                            text: root.model.running ? qsTr("Running...")
                                                     : qsTr("Waiting for the initial movements to finish...")
                            font.pixelSize: Theme.fontSm
                            color: Theme.contentMuted
                        }
                    }
                }
            }

            // The footer: Back, the dots, Next/Complete.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                color: Theme.dark ? Theme.surfaceBase : Theme.gray[50]
                Rectangle { width: parent.width; height: 1; color: Theme.outlineSubtle }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    GButton {
                        objectName: "toolChangeBack"
                        variant: "outline"
                        text: qsTr("Back")
                        fontSize: Theme.fontSm
                        enabled: root.model.canBack
                        onClicked: root.model.back()
                    }
                    Item { Layout.fillWidth: true }
                    Row {
                        spacing: 4
                        Repeater {
                            model: root.model.flatCount
                            Rectangle {
                                required property int index
                                width: index === root.model.flatIndex ? 24 : 18
                                height: 3
                                radius: 1
                                color: index === root.model.flatIndex ? Theme.blue[600]
                                     : index < root.model.flatIndex ? Theme.blue[200] : Theme.gray[300]
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    GButton {
                        objectName: "toolChangeNext"
                        variant: "primary"
                        text: root.model.last ? qsTr("Complete") : qsTr("Next")
                        fontSize: Theme.fontSm
                        enabled: root.model.canNext
                        onClicked: root.model.next()
                    }
                }
            }
        }
    }
}
