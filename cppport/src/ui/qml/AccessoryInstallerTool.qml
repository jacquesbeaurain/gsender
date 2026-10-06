import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Accessory Installation (features/AccessoryInstaller and components/
// Wizard): the wizards' hub; a wizard's landing page - the checks it must
// pass, its configurations; a configuration's steps one page at a time,
// Next opening once a page is done, with what goes beside it; the closing
// page. A wizard with one configuration and nothing failing opens straight
// into it.
ToolPage {
    id: tool
    objectName: "accessoryInstallerTool"
    title: qsTr("Accessory Installation")

    property AccessoryModel model: AccessoryModel { objectName: "accessories" }

    property string vacuumSize: "4x8"  // the Vacuum Table's choice, across its steps

    // Where the model's walk is (AccessoryModel decides every move).
    readonly property string screen: model.screen
    readonly property var wizard: model.screen !== "hub" ? model.wizard : null
    readonly property var sub: model.screen === "run" ? model.subWizard : null
    readonly property var steps: model.steps
    readonly property int step: model.stepIndex
    readonly property bool atCompletion: model.atCompletion
    readonly property var failed: model.failed

    function openWizard(id) { return model.openWizard(id) }

    Connections {
        target: tool.model
        // The page is made afresh each time it shows, as a remounted component.
        function onPageChanged() {
            pageLoader.sourceComponent = null
            const current = tool.steps[tool.step]
            if (tool.sub && !tool.atCompletion && current)
                pageLoader.sourceComponent = tool.pages[current.id] || null
        }
    }

    readonly property var pages: ({
        "zero-position": vacuumZero, "select-size": vacuumSize, "load-mounting-gcode": loadMounting,
        "load-grid-gcode": loadGrid, "options": tlsOptions, "tls-location": tlsLocation,
        "manual-position": manualPosition, "continuity-check": continuity, "eeprom-config": autoSpinEeprom,
        "test": autoSpinTest, "spindle-config": spindleConfig, "modbus-config": modbusConfig
    })
    Component { id: vacuumZero; AccVacuumZero { model: tool.model; installer: tool } }
    Component { id: vacuumSize; AccVacuumSize { model: tool.model; installer: tool } }
    Component { id: loadMounting; AccLoadProgram { model: tool.model; installer: tool } }
    Component { id: loadGrid; AccLoadProgram { model: tool.model; installer: tool; grid: true } }
    Component { id: tlsOptions; AccTlsOptions { model: tool.model; installer: tool } }
    Component { id: tlsLocation; AccPosition { model: tool.model; installer: tool } }
    Component { id: manualPosition; AccPosition { model: tool.model; installer: tool; manual: true } }
    Component { id: continuity; AccContinuity { model: tool.model; installer: tool } }
    Component { id: autoSpinEeprom; AccAutoSpinEeprom { model: tool.model; installer: tool } }
    Component { id: autoSpinTest; AccAutoSpinTest { model: tool.model; installer: tool } }
    Component { id: spindleConfig; AccSpindleConfig { model: tool.model; installer: tool } }
    Component { id: modbusConfig; AccSpindleConfig { model: tool.model; installer: tool; modbus: true } }

    // ---- the hub (WizardsHub) ----
    Flickable {
        visible: tool.screen === "hub"
        anchors.fill: parent
        contentHeight: hub.implicitHeight + 16
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
            id: hub
            width: parent.width
            spacing: 16
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Select a wizard to configure and install your CNC accessories. Each wizard will guide you through the setup process step by step.")
                color: Theme.contentSecondary
            }
            GridLayout {
                Layout.fillWidth: true
                columns: tool.width > 1000 ? 3 : 2
                rowSpacing: 16
                columnSpacing: 16
                Repeater {
                    model: tool.model.wizards
                    Rectangle {
                        required property var modelData
                        readonly property int stepCount: modelData.subWizards.reduce((n, s) => n + s.steps.length, 0)
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        implicitHeight: card.implicitHeight + 32
                        radius: 10
                        color: Theme.dark ? Theme.surfaceRaised : "white"
                        border.width: 2
                        border.color: Theme.dark ? Theme.outline : Theme.gray[200]
                        ColumnLayout {
                            id: card
                            x: 16; y: 16
                            width: parent.width - 32
                            spacing: 8
                            Label { text: modelData.title; font.pixelSize: Theme.fontXl; font.bold: true; color: Theme.contentPrimary }
                            Label {
                                text: (modelData.subWizards.length === 1 ? qsTr("1 configuration") : qsTr("%1 configurations").arg(modelData.subWizards.length))
                                      + "\n" + qsTr("%1 total steps").arg(parent.parent.stepCount)
                                      + (modelData.subWizards[0].estimatedTime ? "\n" + modelData.subWizards[0].estimatedTime : "")
                                color: Theme.contentMuted
                                font.pixelSize: Theme.fontSm
                            }
                            GButton {
                                objectName: "wizard-" + modelData.id
                                Layout.topMargin: 8
                                variant: "primary"
                                text: qsTr("Start Wizard") + " →"
                                onClicked: tool.openWizard(modelData.id)
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- a wizard's landing page (WizardLanding) ----
    RowLayout {
        visible: tool.screen === "landing" && tool.wizard !== null
        anchors.fill: parent
        spacing: 32
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: 12
            GButton {
                objectName: "wizard-back"
                variant: "ghost"
                iconName: "LuArrowLeft"
                text: qsTr("Back to Wizards")
                onClicked: tool.model.backToHub()
            }
            Label {
                objectName: "wizardTitle"
                text: tool.wizard ? tool.wizard.title : ""
                font.pixelSize: 26
                font.bold: true
                color: Theme.contentPrimary
            }
            WizardText {
                readonly property var first: tool.wizard && tool.wizard.subWizards.length ? tool.wizard.subWizards[0] : null
                text: !first ? "" : [
                    first.estimatedTime ? "<b>" + qsTr("Estimated time:") + "</b> " + first.estimatedTime : "",
                    first.configVersion ? qsTr("Configuration File Version: %1").arg(first.configVersion) : "",
                    first.description ? "<br>" + first.description : ""
                ].filter(t => t !== "").join("<br>")
            }
            // ValidationBanner: the first failing reason.
            Rectangle {
                visible: tool.failed.length > 0
                Layout.fillWidth: true
                implicitHeight: banner.implicitHeight + 20
                radius: 8
                color: "#fef9c3"
                border.color: "#fde68a"
                Label {
                    id: banner
                    objectName: "wizardChecks"
                    anchors.fill: parent
                    anchors.margins: 10
                    wrapMode: Text.Wrap
                    text: tool.failed.length > 0 ? tool.failed[0] : ""
                    color: "#854d0e"
                }
            }
            Repeater {
                model: tool.wizard ? tool.wizard.subWizards : []
                GButton {
                    required property var modelData
                    required property int index
                    objectName: "sub-wizard-" + modelData.id
                    Layout.fillWidth: true
                    variant: index === 0 ? "primary" : "outline"
                    text: modelData.title + "  →"
                    enabled: tool.failed.length === 0
                    onClicked: tool.model.startSubWizard(modelData.id)
                }
            }
            // The help link, with its QR code (SecondaryContentPanel).
            RowLayout {
                id: helpRow
                readonly property string url: tool.wizard && tool.wizard.helpUrl ? tool.wizard.helpUrl : "https://resources.sienci.com/"
                Layout.fillWidth: true
                WizardText {
                    Layout.fillWidth: true
                    text: "<b>" + qsTr("Need Help?") + "</b><br>" + qsTr("Follow along in our") + " <a href=\"" + helpRow.url + "\">" + qsTr("online resources") + "</a>"
                    linkColor: Theme.blue[500]
                    onLinkActivated: (link) => Qt.openUrlExternally(link)
                }
                QrLinkButton {
                    objectName: "wizardHelpQr"
                    url: helpRow.url
                }
            }
        }
        Image {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            source: tool.wizard && tool.wizard.image ? tool.wizard.image : "qrc:/accessories/placeholder.png"
            fillMode: Image.PreserveAspectFit
            verticalAlignment: Image.AlignTop
            smooth: true
        }
    }

    // ---- the steps ----
    ColumnLayout {
        visible: tool.screen === "run" && tool.sub !== null
        anchors.fill: parent
        spacing: 12
        readonly property bool single: tool.steps.length === 1

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Label {
                objectName: "wizardProgress"
                text: tool.atCompletion ? qsTr("All Steps Complete")
                      : parent.parent.single ? (tool.steps[tool.step] ? tool.steps[tool.step].title : "")
                      : qsTr("Step %1 of %2").arg(tool.step + 1).arg(tool.steps.length)
                font.bold: true
                color: Theme.contentPrimary
            }
            // ProgressBar: the steps behind, all at the end.
            Rectangle {
                visible: !parent.parent.single
                Layout.fillWidth: true
                height: 8
                radius: 4
                color: Theme.gray[200]
                Rectangle {
                    width: parent.width * (tool.atCompletion ? 1 : tool.step / Math.max(tool.steps.length, 1))
                    height: parent.height
                    radius: 4
                    color: Theme.blue[500]
                }
            }
            Item { visible: parent.parent.single; Layout.fillWidth: true }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 24
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                contentHeight: stepColumn.implicitHeight + 8
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ColumnLayout {
                    id: stepColumn
                    width: parent.width
                    spacing: 12
                    Label {
                        objectName: "wizardStepTitle"
                        visible: !tool.atCompletion && tool.steps.length > 1
                        text: tool.steps[tool.step] ? tool.steps[tool.step].title : ""
                        font.pixelSize: Theme.fontXl
                        font.bold: true
                        color: Theme.contentPrimary
                    }
                    Label {
                        visible: !tool.atCompletion && !!tool.sub && !!tool.sub.configVersion
                        text: tool.sub ? qsTr("Configuration File Version: %1").arg(tool.sub.configVersion) : ""
                        color: Theme.contentMuted
                        font.pixelSize: Theme.fontSm
                    }
                    Loader {
                        id: pageLoader
                        objectName: "wizardPage"
                        Layout.fillWidth: true
                        onLoaded: if (item.complete) tool.model.setStepComplete(true)
                    }

                    // The closing page.
                    ColumnLayout {
                        visible: tool.atCompletion
                        Layout.fillWidth: true
                        Layout.topMargin: 24
                        spacing: 16
                        readonly property var content: tool.sub && tool.sub.completion ? tool.sub.completion : ({ done: "", next: [], warning: "", image: "" })
                        Image {
                            objectName: "wizardCompleteImage"
                            visible: !!parent.content.image
                            Layout.alignment: Qt.AlignHCenter
                            Layout.maximumHeight: 256   // max-h-64
                            Layout.preferredHeight: Math.min(implicitHeight, 256)
                            Layout.preferredWidth: implicitHeight > 0 ? implicitWidth * Layout.preferredHeight / implicitHeight : 0
                            fillMode: Image.PreserveAspectFit
                            source: parent.content.image || ""
                        }
                        Label {
                            objectName: "wizardComplete"
                            Layout.alignment: Qt.AlignHCenter
                            text: (parent.content.image ? "" : "✔ ") + qsTr("Setup Complete!")
                            font.pixelSize: 30
                            font.bold: true
                            color: Theme.green[500]
                        }
                        WizardText { horizontalAlignment: Text.AlignHCenter; text: parent.content.done }
                        Rectangle {
                            visible: parent.content.next.length > 0
                            Layout.fillWidth: true
                            implicitHeight: nextSteps.implicitHeight + 24
                            radius: 8
                            color: Theme.dark ? Theme.surfaceRaised : "#eff6ff"
                            WizardText {
                                id: nextSteps
                                anchors.fill: parent
                                anchors.margins: 12
                                text: "<b>" + qsTr("Next Steps:") + "</b><ul>" + parent.parent.content.next.map(s => "<li>" + s + "</li>").join("") + "</ul>"
                            }
                        }
                        Rectangle {
                            visible: parent.content.warning !== ""
                            Layout.fillWidth: true
                            implicitHeight: warning.implicitHeight + 24
                            radius: 8
                            color: "#fef9c3"
                            WizardText {
                                id: warning
                                anchors.fill: parent
                                anchors.margins: 12
                                color: "#854d0e"
                                text: "⚠ " + parent.parent.content.warning
                            }
                        }
                    }
                }
            }
            // Beside the step; the closing page takes the width.
            Flickable {
                visible: !tool.atCompletion
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                contentHeight: sideColumn.implicitHeight + 8
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ColumnLayout {
                    id: sideColumn
                    width: parent.width
                    spacing: 16
                    Repeater {
                        model: tool.atCompletion || !tool.steps[tool.step] ? [] : tool.steps[tool.step].side
                        WizardSideItem {
                            required property var modelData
                            Layout.fillWidth: true
                            item: modelData
                            model: tool.model
                        }
                    }
                }
            }
        }

        RowLayout {
            visible: !parent.single || tool.atCompletion
            Layout.fillWidth: true
            spacing: 12
            GButton {
                visible: tool.atCompletion
                objectName: "wizardRestart"
                variant: "outline"
                text: qsTr("Restart Wizard")
                onClicked: tool.model.restart()
            }
            Item { Layout.fillWidth: true }
            GButton {
                visible: !tool.atCompletion
                objectName: "wizardPrevious"
                variant: "outline"
                text: qsTr("Previous")
                enabled: tool.model.canBack
                onClicked: tool.model.back()
            }
            GButton {
                visible: !tool.atCompletion
                objectName: "wizardNext"
                variant: "primary"
                text: qsTr("Next")
                enabled: tool.model.canNext
                onClicked: tool.model.next()
            }
            GButton {
                visible: tool.atCompletion
                objectName: "wizardExit"
                variant: "primary"
                text: qsTr("Exit")
                onClicked: tool.model.exitSubWizard()
            }
        }
    }

    Connections {
        target: pageLoader.item
        ignoreUnknownSignals: true
        function onCompleteChanged() {
            tool.model.setStepComplete(pageLoader.item.complete)
        }
        function onFinish() {
            // A page that loaded a file into the visualizer: to the Carve page.
            Backend.openPage("carve")
            Qt.callLater(tool.back)
        }
    }
}
