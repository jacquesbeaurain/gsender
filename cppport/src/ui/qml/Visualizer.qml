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
            Label { text: qsTr("Job Summary"); font.bold: true; color: Theme.contentPrimary }
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

        // Iso View
        Panel {
            id: btnIso
            objectName: "view3D"
            width: 36
            height: 36
            radius: 18
            color: Qt.rgba(12/255, 16/255, 20/255, 0.75)
            border.color: Qt.rgba(156/255, 163/255, 175/255, 0.4)

            Icon {
                anchors.centerIn: parent
                name: "FaCube"
                color: isoHover.hovered ? "white" : "white"
                width: 16
                height: 16
            }

            HoverHandler { id: isoHover }
            ToolTip.visible: isoHover.hovered
            ToolTip.text: qsTr("Go to iso view")
            TapHandler { onTapped: toolpath.setView("3d") }
        }

        // Ortho / Perspective Toggle
        Rectangle {
            id: btnProjection
            objectName: "viewOrtho"
            width: 36
            height: 36
            radius: 18
            readonly property bool isOrtho: !Backend.perspective
            color: Qt.rgba(12/255, 16/255, 20/255, 0.75)
            border.color: isOrtho ? "#60a5fa"
                                  : Qt.rgba(156/255, 163/255, 175/255, 0.4)
            border.width: isOrtho ? 1.5 : 1.0

            Icon {
                anchors.centerIn: parent
                name: btnProjection.isOrtho ? "LuSquare" : "LuBox"
                color: btnProjection.isOrtho ? "#60a5fa"
                                             : (orthoHover.hovered ? "white" : "white")
                width: 16
                height: 16
            }

            HoverHandler { id: orthoHover }
            ToolTip.visible: orthoHover.hovered
            ToolTip.text: isOrtho ? qsTr("Switch to perspective view") : qsTr("Switch to orthographic view")
            TapHandler { onTapped: Backend.togglePerspective() }
        }
    }

    // Lightweight Mode Toggle (floating circular button above utility row)
    Rectangle {
        id: btnLightweight
        objectName: "btnLightweight"
        anchors.bottom: cubeUtilityRow.top
        anchors.bottomMargin: 6
        anchors.horizontalCenter: cubeUtilityRow.horizontalCenter
        width: 44
        height: 44
        radius: 22
        readonly property bool isLite: Backend.liteMode
        color: Qt.rgba(12/255, 16/255, 20/255, 0.75)
        border.color: isLite ? "#60a5fa"
                             : Qt.rgba(156/255, 163/255, 175/255, 0.4)
        border.width: isLite ? 2.0 : 1.0

        Icon {
            anchors.centerIn: parent
            name: "FaFeatherAlt"
            color: btnLightweight.isLite ? "#60a5fa"
                                         : (liteHover.hovered ? "white" : "white")
            width: 20
            height: 20
        }

        HoverHandler { id: liteHover }
        ToolTip.visible: liteHover.hovered
        ToolTip.text: isLite ? qsTr("Disable lightweight mode") : qsTr("Enable lightweight mode")
        TapHandler { onTapped: Backend.toggleLiteMode() }
    }

    // Move To Here (upstream's crosshair, shown once connected): press and
    // hold a spot of the top view to rapid the spindle there.
    Rectangle {
        id: btnMoveToHere
        objectName: "btnMoveToHere"
        visible: toolpath.moveToHereAvailable
        anchors.bottom: btnLightweight.top
        anchors.bottomMargin: 6
        anchors.horizontalCenter: cubeUtilityRow.horizontalCenter
        width: 44
        height: 44
        radius: 22
        readonly property bool armed: toolpath.moveToHere
        color: Qt.rgba(12/255, 16/255, 20/255, 0.75)
        border.color: armed ? "#0ef6ae" : Qt.rgba(156/255, 163/255, 175/255, 0.4)
        border.width: armed ? 2.0 : 1.0

        Icon {
            anchors.centerIn: parent
            name: "LuCrosshair"
            color: btnMoveToHere.armed ? "#0ef6ae" : "white"
            width: 20
            height: 20
        }

        HoverHandler { id: moveHover }
        ToolTip.visible: moveHover.hovered
        ToolTip.text: qsTr("Move To Here: press and hold a spot to move the spindle there")
        TapHandler { onTapped: toolpath.toggleMoveToHere() }
    }

    // Plugin Visualizer Overlay floating toggle buttons (stacked above lightweight toggle)
    PluginsModel { id: visualizerPluginsModel }
    Repeater {
        id: overlayToggleButtons
        model: visualizerPluginsModel.count >= 0 ? visualizerPluginsModel.contributions("visualizer-overlay") : []
        Rectangle {
            required property var modelData
            required property int index
            objectName: "btnOverlay_" + modelData.pluginId
            anchors.bottom: btnLightweight.top
            anchors.bottomMargin: (btnMoveToHere.visible ? 60 : 10) + index * 52
            anchors.horizontalCenter: cubeUtilityRow.horizontalCenter
            width: 44
            height: 44
            radius: 22
            readonly property bool isOpen: frame.activeOverlayPluginId === modelData.pluginId
            color: Qt.rgba(12/255, 16/255, 20/255, 0.75)
            border.color: isOpen ? "#0ef6ae"
                                 : Qt.rgba(156/255, 163/255, 175/255, 0.4)
            border.width: isOpen ? 2.0 : 1.0

            Icon {
                anchors.centerIn: parent
                name: "LuCrosshair"
                color: parent.isOpen ? "#0ef6ae"
                                     : (overlayBtnHover.hovered ? "white" : "white")
                width: 20
                height: 20
            }

            HoverHandler { id: overlayBtnHover }
            ToolTip.visible: overlayBtnHover.hovered
            ToolTip.text: modelData.label || modelData.pluginName
            TapHandler {
                onTapped: {
                    frame.activeOverlayPluginId = (frame.activeOverlayPluginId === parent.modelData.pluginId) ? "" : parent.modelData.pluginId
                }
            }
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
                        color: Theme.contentPrimary
                    }
                    Rectangle {
                        width: 28
                        height: 28
                        radius: 14
                        color: closeHover.hovered ? (Theme.dark ? Theme.gray[700] : Theme.gray[200]) : "transparent"
                        Icon {
                            anchors.centerIn: parent
                            name: "LuX"
                            width: 16
                            height: 16
                            color: Theme.contentPrimary
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
