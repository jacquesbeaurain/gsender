#pragma once

#include <QString>
#include <QMap>
#include <optional>

namespace gs::app {

/** Manages isolated key/value persistent storage for plugins.
 *  Each plugin's slice is stored under <storageDir>/<pluginId>.json. */
class PluginStorage {
public:
    explicit PluginStorage(const QString& storageDir);

    std::optional<QString> get(const QString& pluginId, const QString& key) const;
    bool set(const QString& pluginId, const QString& key, const QString& value);
    bool deleteKey(const QString& pluginId, const QString& key);
    QMap<QString, QString> getAll(const QString& pluginId) const;
    bool setAll(const QString& pluginId, const QMap<QString, QString>& entries);
    bool clear(const QString& pluginId);

    QString storagePath(const QString& pluginId) const;

private:
    QString storageDir_;
    mutable QMap<QString, QMap<QString, QString>> cache_;

    void ensureLoaded(const QString& pluginId) const;
    bool persist(const QString& pluginId) const;
};

}  // namespace gs::app
