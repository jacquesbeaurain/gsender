import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Reusable container for multi-step wizards providing standard title bar,
// step breadcrumbs/progress dots, and navigation action footer.
Rectangle {
    id: root

    property string title: ""
    property var wizardModel: null
    default property alias content: contentSlot.children
    property bool showRestart: true
    property string nextText: (wizardModel && wizardModel.isLastStep) ? qsTr("Complete") : qsTr("Next")
    property bool nextEnabled: wizardModel ? wizardModel.canNext : true
    property bool backEnabled: wizardModel ? wizardModel.canBack : true

    signal nextClicked()
    signal backClicked()
    signal restartClicked()
    signal cancelClicked()

    radius: Theme.radius
    color: Theme.dark ? Theme.surfaceRaised : "white"
    border.color: Theme.outline

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.dark ? Theme.surfaceBase : Theme.gray[100]
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 8
                Label {
                    text: root.title
                    font.pixelSize: Theme.fontBase
                    font.weight: Font.DemiBold
                    color: Theme.contentPrimary
                    Layout.fillWidth: true
                }
            }
        }

        // Content area
        Item {
            id: contentSlot
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        // Navigation footer
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: Theme.dark ? Theme.surfaceBase : Theme.gray[50]
            Rectangle { width: parent.width; height: 1; color: Theme.outlineSubtle }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 12

                GButton {
                    visible: root.showRestart
                    variant: "outline"
                    text: qsTr("Restart")
                    fontSize: Theme.fontSm
                    onClicked: {
                        root.restartClicked()
                        if (root.wizardModel && root.wizardModel.restart) {
                            root.wizardModel.restart()
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                GButton {
                    variant: "outline"
                    text: qsTr("Back")
                    fontSize: Theme.fontSm
                    enabled: root.backEnabled
                    onClicked: {
                        root.backClicked()
                        if (root.wizardModel && root.wizardModel.back) {
                            root.wizardModel.back()
                        }
                    }
                }

                GButton {
                    variant: "primary"
                    text: root.nextText
                    fontSize: Theme.fontSm
                    enabled: root.nextEnabled
                    onClicked: {
                        root.nextClicked()
                        if (root.wizardModel && root.wizardModel.next) {
                            root.wizardModel.next()
                        }
                    }
                }
            }
        }
    }
}
