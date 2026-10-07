import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// The main visualizer: the toolpath drawn by ToolpathItem, driven by touch
// or mouse - one finger (or the left button) orbits, two fingers (or the
// right/middle button, or Shift+drag) pan, a pinch or the wheel zooms, a
// double tap fits.
Rectangle {
    id: frame
    objectName: "visualizer"

    property alias view: toolpath
    property string activeOverlayPluginId: ""

    color: "transparent"
    radius: Theme.radius
    clip: true

    ToolpathItem {
        id: toolpath
        objectName: "toolpath"
        anchors.fill: parent
        pluginHost: true
        keyboardControl: Backend.visualizerKeyboardControl
    }

    // The visualizer's keyboard shortcuts.
    Connections {
        target: Backend
        function onShortcutTriggered(id) {
            const views = { VISUALIZER_VIEW_3D: "3d", VISUALIZER_VIEW_TOP: "top", VISUALIZER_VIEW_FRONT: "front",
                            VISUALIZER_VIEW_RIGHT: "right", VISUALIZER_VIEW_LEFT: "left", VISUALIZER_VIEW_RESET: "3d" }
            if (views[id])
                toolpath.setView(views[id])
            else if (id === "VISUALIZER_VIEW_CYCLE")
                toolpath.cycleView()
            else if (id === "VISUALIZER_ZOOM_IN")
                toolpath.zoomAt(toolpath.width / 2, toolpath.height / 2, 1.25)
            else if (id === "VISUALIZER_ZOOM_OUT")
                toolpath.zoomAt(toolpath.width / 2, toolpath.height / 2, 0.8)
            else if (id === "VISUALIZER_ZOOM_FIT")
                toolpath.fit()
        }
    }

    ToolpathGestures {
        anchors.fill: parent
        view: toolpath
    }

    WorkspaceSelector {
        id: workspaceSelector
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
    }

    // Accessibility's G-code summary, shown visually (AccessibilityAnnouncer).
    Panel {
        objectName: "jobSummary"
        visible: Backend.jobSummary !== ""
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        width: Math.min(560, workspaceSelector.x - 16)
        height: summaryColumn.implicitHeight + 24
        radius: Theme.radiusSmall
        border.color: Theme.outlineSubtle
        Rectangle { width: 4; height: parent.height; radius: 2; color: Theme.primary }
        ColumnLayout {
            id: summaryColumn
            anchors.fill: parent
            anchors.margins: 12
            anchors.leftMargin: 16
            spacing: 4
            Label { text: qsTr("Job Summary"); font.bold: true }
            Label {
                Layout.fillWidth: true
                text: Backend.jobSummary
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSm
                color: Theme.contentSecondary
            }
        }
    }

    // Navigation & Viewport Controls (bottom-left overlay)
    // 3D Navigation Cube (hidden in lightweight mode matching upstream)
    NavCubeItem {
        id: navCube
        objectName: "navCube"
        visible: !Backend.liteMode
        anchors.left: parent.left
        // gviewer's 90 px cube sits 58 px from the corner; its item is larger, so
        // the cube's corners are not cut off.
        anchors.leftMargin: 28
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 28
        width: 150
        height: 150
        view: toolpath
    }

    // Utility Row (Iso view + Ortho/Perspective toggle) stacked above ViewCube
    Row {
        id: cubeUtilityRow
        anchors.bottom: Backend.liteMode ? parent.bottom : navCube.top
        anchors.bottomMargin: Backend.liteMode ? 58 : 1
        anchors.left: parent.left
        anchors.leftMargin: 28 + (150 - width) / 2
        spacing: 8

        OverlayButton {
            objectName: "view3D"
            size: 36
            icon: "FaCube"
            tooltip: qsTr("Go to iso view")
            onTapped: toolpath.setView("3d")
        }
        OverlayButton {
            id: btnProjection
            objectName: "viewOrtho"
            size: 36
            active: !Backend.perspective
            activeBorder: 1.5
            icon: active ? "LuSquare" : "LuBox"
            tooltip: active ? qsTr("Switch to perspective view") : qsTr("Switch to orthographic view")
            onTapped: Backend.togglePerspective()
        }
    }

    // The visualizer's round buttons (bg-dark-darker/70, a faint border; the
    // accent's border and icon while `active`).
    component OverlayButton: Rectangle {
        id: overlayButton
        property real size: 44
        property string icon
        property string tooltip
        property bool active: false
        property color accent: Theme.tw.blue[400]
        property real activeBorder: 2
        signal tapped()

        width: size
        height: size
        radius: size / 2
        color: Qt.rgba(12/255, 16/255, 20/255, 0.75)
        border.color: active ? accent : Qt.rgba(156/255, 163/255, 175/255, 0.4)
        border.width: active ? activeBorder : Theme.hairline
        Icon {
            anchors.centerIn: parent
            name: overlayButton.icon
            color: overlayButton.active ? overlayButton.accent : "white"
            size: overlayButton.size < 44 ? 16 : 20
        }
        TapHandler { onTapped: overlayButton.tapped() }
    }
    // Move To Here's and the plugin overlays' accent (rgba(14, 246, 174)).
    readonly property color armedAccent: "#0ef6ae"

    // Lightweight Mode Toggle (floating circular button above utility row)
    OverlayButton {
        id: btnLightweight
        objectName: "btnLightweight"
        anchors.bottom: cubeUtilityRow.top
        anchors.bottomMargin: 6
        anchors.horizontalCenter: cubeUtilityRow.horizontalCenter
        active: Backend.liteMode
        icon: "FaFeatherAlt"
        tooltip: active ? qsTr("Disable lightweight mode") : qsTr("Enable lightweight mode")
        onTapped: Backend.toggleLiteMode()
    }

    // Move To Here (upstream's crosshair, shown once connected): press and
    // hold a spot of the top view to rapid the spindle there.
    OverlayButton {
        id: btnMoveToHere
        objectName: "btnMoveToHere"
        visible: toolpath.moveToHereAvailable
        anchors.bottom: btnLightweight.top
        anchors.bottomMargin: 6
        anchors.horizontalCenter: cubeUtilityRow.horizontalCenter
        active: toolpath.moveToHere
        accent: frame.armedAccent
        icon: "LuCrosshair"
        tooltip: qsTr("Move To Here: press and hold a spot to move the spindle there")
        onTapped: toolpath.toggleMoveToHere()
    }

    // Plugin Visualizer Overlay floating toggle buttons (stacked above lightweight toggle)
    PluginsModel { id: visualizerPluginsModel }
    Repeater {
        id: overlayToggleButtons
        model: visualizerPluginsModel.count >= 0 ? visualizerPluginsModel.contributions("visualizer-overlay") : []
        OverlayButton {
            required property var modelData
            required property int index
            objectName: "btnOverlay_" + modelData.pluginId
            anchors.bottom: btnLightweight.top
            anchors.bottomMargin: (btnMoveToHere.visible ? 60 : 10) + index * 52
            anchors.horizontalCenter: cubeUtilityRow.horizontalCenter
            active: frame.activeOverlayPluginId === modelData.pluginId
            accent: frame.armedAccent
            icon: "LuCrosshair"
            tooltip: modelData.label || modelData.pluginName
            onTapped: frame.activeOverlayPluginId = active ? "" : modelData.pluginId
        }
    }

    // Floating Overlay Panels on the right (matching upstream OverlayPanel, shown ONLY when opened)
    Repeater {
        id: visualizerOverlays
        model: visualizerPluginsModel.count >= 0 ? visualizerPluginsModel.contributions("visualizer-overlay") : []
        Panel {
            required property var modelData
            visible: frame.activeOverlayPluginId === modelData.pluginId
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.margins: 16
            width: 320
            border.color: Theme.outline
            clip: true
            z: 20

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                // Header with title and Close (X) button
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: modelData.label || modelData.pluginName
                        font.bold: true
                        font.pixelSize: Theme.fontBase
                    }
                    Rectangle {
                        width: 28
                        height: 28
                        radius: 14
                        color: closeHover.hovered ? (Theme.dark ? Theme.gray[700] : Theme.gray[200]) : "transparent"
                        Icon {
                            anchors.centerIn: parent
                            name: "LuX"
                            size: 16
                        }
                        HoverHandler { id: closeHover }
                        TapHandler { onTapped: frame.activeOverlayPluginId = "" }
                    }
                }

                // Mounted Plugin View
                PluginHost {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    pluginId: modelData.pluginId
                    uiEntryUrl: modelData.uiUrl
                }
            }
        }
    }

    // Test automation proxy for legacy view actions
    Rectangle {
        objectName: "viewTop"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        width: 40
        height: 40
        opacity: 0.001
        z: 10
        TapHandler { onTapped: toolpath.setView("top") }
    }
    Rectangle {
        objectName: "viewFit"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: 52
        anchors.topMargin: 8
        width: 40
        height: 40
        opacity: 0.001
        z: 10
        TapHandler { onTapped: toolpath.fit() }
    }
}
