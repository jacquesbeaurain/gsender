import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The tool area (features/Tools, components/Tabs): the tabs above the card -
// scrolled with the arrows when they do not fit, the chosen one underlined
// in blue - and the chosen tab's content. Every tab stays loaded once shown,
// so a tab keeps its state when another is chosen.
Item {
    id: tools
    objectName: "toolsWidget"

    // {key, label, component, shown}; the first shown one to start with.
    property list<QtObject> tabs
    property string current: ""

    readonly property var shownTabs: {
        const list = []
        for (let i = 0; i < tabs.length; ++i)
            if (tabs[i].shown)
                list.push(tabs[i])
        return list
    }
    // A tab that goes (Spindle/Laser off, say) gives way to the first.
    onShownTabsChanged: {
        if (!shownTabs.some(t => t.key === current) && shownTabs.length)
            current = shownTabs[0].key
    }
    Component.onCompleted: if (!current && shownTabs.length) current = shownTabs[0].key

    // Chosen, and scrolled into view.
    function select(key, button) {
        current = key
        if (button.x < strip.contentX)
            strip.contentX = button.x
        else if (button.x + button.width > strip.contentX + strip.width)
            strip.contentX = button.x + button.width - strip.width
    }

    RowLayout {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        height: 40
        spacing: 0
        Item {
            objectName: "toolsScrollLeft"
            readonly property bool can: !strip.atXBeginning
            Layout.preferredWidth: 32
            Layout.fillHeight: true
            Icon {
                anchors.centerIn: parent
                name: "MdKeyboardArrowLeft"
                color: parent.can ? Theme.gray[400] : (Theme.dark ? Theme.contentMuted : Theme.gray[200])
                width: 24
                height: 24
            }
            TapHandler { enabled: parent.can; onTapped: strip.contentX = Math.max(0, strip.contentX - 100) }
        }
        Flickable {
            id: strip
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: row.width
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            Behavior on contentX { NumberAnimation { duration: 150 } }
            Row {
                id: row
                height: strip.height
                Repeater {
                    id: tabButtons
                    model: tools.shownTabs
                    Item {
                        required property var modelData
                        readonly property bool selected: tools.current === modelData.key
                        objectName: "toolsTab_" + modelData.key
                        // flex-grow: the tabs share what width there is.
                        width: Math.max(label.implicitWidth + 32, strip.width / Math.max(1, tabButtons.count))
                        height: row.height
                        Label {
                            id: label
                            anchors.centerIn: parent
                            text: modelData.label
                            font.pixelSize: Theme.fontBase
                            font.weight: Font.Medium
                            color: parent.selected ? (Theme.dark ? Theme.blue[400] : Theme.blue[600])
                                                   : (Theme.dark ? Theme.contentSecondary : Theme.gray[600])
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            color: parent.selected ? (Theme.dark ? Theme.blue[400] : Theme.blue[600]) : "transparent"
                        }
                        TapHandler { onTapped: tools.select(modelData.key, parent) }
                    }
                }
            }
        }
        Item {
            objectName: "toolsScrollRight"
            readonly property bool can: !strip.atXEnd
            Layout.preferredWidth: 32
            Layout.fillHeight: true
            Icon {
                anchors.centerIn: parent
                name: "MdKeyboardArrowRight"
                color: parent.can ? Theme.gray[400] : (Theme.dark ? Theme.contentMuted : Theme.gray[200])
                width: 24
                height: 24
            }
            TapHandler { enabled: parent.can; onTapped: strip.contentX = Math.min(strip.contentWidth - strip.width, strip.contentX + 100) }
        }
    }

    Card {
        anchors.fill: parent
        anchors.topMargin: header.height + 2
        padding: 4

        Repeater {
            model: tools.tabs.length
            Loader {
                required property int index
                readonly property var tab: tools.tabs[index]
                anchors.fill: parent
                visible: tab.shown && tools.current === tab.key
                // Loaded when first shown, then kept.
                active: visible || status === Loader.Ready
                sourceComponent: tab.component
            }
        }
    }
}
