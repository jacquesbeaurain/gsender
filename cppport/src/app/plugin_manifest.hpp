#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QSet>
#include <vector>

namespace gs::app {

struct PluginContribution {
    QString slot;       /**< "tools-page", "tools-tab", "visualizer-overlay", "settings-section", "standalone" */
    QString label;
    QString icon;
    QString route;
    bool requiresIdle = false;
};

struct PluginCapabilities {
    QSet<QString> requestTypes;
    QSet<QString> topics;
};

struct PluginManifest {
    QString id;
    QString name;
    QString version;
    QString description;
    QString author;
    QString engine;
    QString wasmEntry;
    QString uiEntry;
    std::vector<PluginContribution> contributions;
    PluginCapabilities capabilities;
    /** Parser specs as declared (see plugin_parsers.hpp); the chain validates them when the plugin starts. */
    QJsonArray parsers;

    bool isValid() const { return !id.isEmpty() && !name.isEmpty() && !version.isEmpty(); }
    bool isOfficial() const { return id.startsWith(QStringLiteral("com.sienci.")); }
};

/** Parses a JSON string into a PluginManifest. */
bool parsePluginManifest(const QString& jsonString, PluginManifest* outManifest, QString* outError = nullptr);

/** Reads and parses a manifest file from disk. */
bool loadPluginManifestFile(const QString& filePath, PluginManifest* outManifest, QString* outError = nullptr);

/** Evaluates semver engine compatibility (e.g. ">=1.5.0", "^1.0.0"). */
bool isEngineCompatible(const QString& engineRange, const QString& currentVersion);

}  // namespace gs::app
