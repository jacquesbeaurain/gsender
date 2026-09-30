import QtQuick
import QtQuick.Controls.Basic
import GSender

// The main visualizer: the toolpath drawn by ToolpathItem, driven by touch
// or mouse - one finger (or the left button) orbits, two fingers (or the
// right/middle button, or Shift+drag) pan, a pinch or the wheel zooms, a
// double tap fits.
Rectangle {
    id: frame
    objectName: "visualizer"

    property alias view: toolpath

    color: "transparent"
    radius: Theme.radius
    clip: true

    ToolpathItem {
        id: toolpath
        objectName: "toolpath"
        anchors.fill: parent
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
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
    }

    // Navigation & Viewport Controls (bottom-left overlay)
    // 3D Navigation Cube
    NavCubeItem {
        id: navCube
        objectName: "navCube"
        anchors.left: parent.left
        anchors.leftMargin: 60
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 60
        width: 84
        height: 84
        view: toolpath
    }

    // Utility Row (Iso view + Ortho/Perspective toggle) stacked above ViewCube
    Row {
        id: cubeUtilityRow
        anchors.bottom: navCube.top
        anchors.bottomMargin: 28
        anchors.horizontalCenter: navCube.horizontalCenter
        spacing: 8

        // Iso View
        Rectangle {
            id: btnIso
            objectName: "view3D"
            width: 36
            height: 36
            radius: 18
            color: Theme.dark ? Qt.rgba(12/255, 16/255, 20/255, 0.75) : Qt.rgba(240/255, 245/255, 252/255, 0.85)
            border.color: Theme.dark ? Qt.rgba(156/255, 163/255, 175/255, 0.4) : Qt.rgba(160/255, 175/255, 195/255, 0.6)
            border.width: 1

            Icon {
                anchors.centerIn: parent
                name: "FaCube"
                color: isoHover.hovered ? "white" : (Theme.dark ? Theme.gray[300] : Theme.gray[700])
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
            color: Theme.dark ? Qt.rgba(12/255, 16/255, 20/255, 0.75) : Qt.rgba(240/255, 245/255, 252/255, 0.85)
            border.color: isOrtho ? Qt.rgba(96/255, 165/255, 250/255, 0.95)
                                  : (Theme.dark ? Qt.rgba(156/255, 163/255, 175/255, 0.4) : Qt.rgba(160/255, 175/255, 195/255, 0.6))
            border.width: isOrtho ? 1.5 : 1.0

            Icon {
                anchors.centerIn: parent
                name: btnProjection.isOrtho ? "LuSquare" : "LuBox"
                color: btnProjection.isOrtho ? Qt.rgba(96/255, 165/255, 250/255, 0.95)
                                             : (orthoHover.hovered ? "white" : (Theme.dark ? Theme.gray[300] : Theme.gray[700]))
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
        anchors.horizontalCenter: navCube.horizontalCenter
        width: 44
        height: 44
        radius: 22
        readonly property bool isLite: Backend.liteMode
        color: Theme.dark ? Qt.rgba(12/255, 16/255, 20/255, 0.75) : Qt.rgba(240/255, 245/255, 252/255, 0.85)
        border.color: isLite ? Qt.rgba(96/255, 165/255, 250/255, 0.95)
                             : (Theme.dark ? Qt.rgba(156/255, 163/255, 175/255, 0.4) : Qt.rgba(160/255, 175/255, 195/255, 0.6))
        border.width: isLite ? 2.0 : 1.0

        Icon {
            anchors.centerIn: parent
            name: "FaFeatherAlt"
            color: btnLightweight.isLite ? Qt.rgba(96/255, 165/255, 250/255, 0.95)
                                         : (liteHover.hovered ? "white" : (Theme.dark ? Theme.gray[300] : Theme.gray[700]))
            width: 20
            height: 20
        }

        HoverHandler { id: liteHover }
        ToolTip.visible: liteHover.hovered
        ToolTip.text: isLite ? qsTr("Disable lightweight mode") : qsTr("Enable lightweight mode")
        TapHandler { onTapped: Backend.toggleLiteMode() }
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

    // Third-party plugin visualizer overlays (slot: visualizer-overlay).
    PluginsModel { id: visualizerPluginsModel }
    Repeater {
        id: visualizerOverlays
        model: visualizerPluginsModel.count >= 0 ? visualizerPluginsModel.contributions("visualizer-overlay") : []
        PluginHost {
            anchors.fill: parent
            pluginId: modelData.pluginId
            uiEntryUrl: modelData.uiUrl
        }
    }
}
