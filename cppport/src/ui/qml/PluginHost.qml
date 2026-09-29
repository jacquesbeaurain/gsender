import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import GSender

// Generic container for hosting third-party QML plugin components with injected gsender bridge context.
Item {
    id: host

    property string pluginId: ""
    property string uiEntryUrl: ""
    property var gsender: null

    PluginsModel { id: pModel }

    Component.onCompleted: {
        if (pluginId && !gsender) {
            gsender = pModel.createContext(pluginId)
        }
    }

    onPluginIdChanged: {
        if (pluginId) {
            gsender = pModel.createContext(pluginId)
        }
    }

    onGsenderChanged: {
        if (loader.item) {
            if (loader.item.hasOwnProperty("gsender")) loader.item.gsender = host.gsender
            if (loader.item.hasOwnProperty("pluginId")) loader.item.pluginId = host.pluginId
        }
    }

    Loader {
        id: loader
        anchors.fill: parent
        source: host.uiEntryUrl
        onLoaded: {
            if (item) {
                if (item.hasOwnProperty("gsender")) item.gsender = host.gsender
                if (item.hasOwnProperty("pluginId")) item.pluginId = host.pluginId
            }
        }
    }
}
