#include "plugin_service.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

namespace gs::app {

PluginService::PluginService(Machine& machine, const QString& storageDir, QObject* parent)
    : QObject(parent), machine_(machine), storage_(storageDir), bridge_(machine, storage_, this) {
    setupTopicBroadcasters();
}

void PluginService::addSearchPath(const QString& path) {
    if (!path.isEmpty() && !searchPaths_.contains(path)) {
        searchPaths_.append(path);
    }
}

void PluginService::scanPlugins() {
    plugins_.clear();
    QSet<QString> seenIds;

    for (const QString& basePath : searchPaths_) {
        const QDir baseDir(basePath);
        if (!baseDir.exists()) {
            continue;
        }

        const QFileInfoList subdirs = baseDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& dirInfo : subdirs) {
            const QString manifestPath = dirInfo.filePath() + QStringLiteral("/gsender-plugin.json");
            if (!QFile::exists(manifestPath)) {
                continue;
            }

            PluginManifest manifest;
            QString error;
            if (loadPluginManifestFile(manifestPath, &manifest, &error)) {
                if (!seenIds.contains(manifest.id)) {
                    seenIds.insert(manifest.id);
                    LoadedPlugin plugin;
                    plugin.manifest = std::move(manifest);
                    plugin.directory = dirInfo.filePath();
                    plugin.enabled = !disabledPluginIds_.contains(plugin.manifest.id);
                    plugins_.push_back(std::move(plugin));
                }
            }
        }
    }

    Q_EMIT pluginsChanged();
}

const LoadedPlugin* PluginService::findPlugin(const QString& id) const {
    for (const auto& plugin : plugins_) {
        if (plugin.manifest.id == id) {
            return &plugin;
        }
    }
    return nullptr;
}

bool PluginService::isPluginEnabled(const QString& id) const {
    const LoadedPlugin* p = findPlugin(id);
    return p ? p->enabled : false;
}

void PluginService::setPluginEnabled(const QString& id, bool enabled) {
    for (auto& plugin : plugins_) {
        if (plugin.manifest.id == id) {
            if (plugin.enabled != enabled) {
                plugin.enabled = enabled;
                if (enabled) {
                    disabledPluginIds_.remove(id);
                } else {
                    disabledPluginIds_.insert(id);
                }
                Q_EMIT pluginsChanged();
            }
            break;
        }
    }
}

std::vector<std::pair<LoadedPlugin, PluginContribution>> PluginService::contributionsForSlot(const QString& slot) const {
    std::vector<std::pair<LoadedPlugin, PluginContribution>> result;
    for (const auto& plugin : plugins_) {
        if (!plugin.enabled) {
            continue;
        }
        for (const auto& contrib : plugin.manifest.contributions) {
            if (contrib.slot == slot) {
                result.push_back({plugin, contrib});
            }
        }
    }
    return result;
}

void PluginService::setupTopicBroadcasters() {
    connect(&machine_, &Machine::stateChanged, this, [this] {
        controller::Controller* c = machine_.controller();
        if (!c) {
            return;
        }
        const auto& status = c->state().status;
        QJsonObject workspaceData;
        workspaceData.insert(QStringLiteral("x"), status.wpos.x());
        workspaceData.insert(QStringLiteral("y"), status.wpos.y());
        workspaceData.insert(QStringLiteral("z"), status.wpos.z());
        workspaceData.insert(QStringLiteral("a"), status.wpos.a());
        bridge_.broadcastTopic(QStringLiteral("workspace"), workspaceData);

        QJsonObject controllerData;
        controllerData.insert(QStringLiteral("activeState"), QString::fromStdString(status.activeState));
        bridge_.broadcastTopic(QStringLiteral("controller"), controllerData);
    });
}

}  // namespace gs::app
