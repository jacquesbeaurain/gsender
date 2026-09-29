#include "plugin_storage.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace gs::app {

PluginStorage::PluginStorage(const QString& storageDir) : storageDir_(storageDir) {
    QDir dir(storageDir_);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
}

QString PluginStorage::storagePath(const QString& pluginId) const {
    const QString safeName = QString(pluginId).replace(QLatin1Char('/'), QLatin1Char('_'))
                                              .replace(QLatin1Char('\\'), QLatin1Char('_'));
    return QDir(storageDir_).filePath(safeName + QStringLiteral(".json"));
}

void PluginStorage::ensureLoaded(const QString& pluginId) const {
    if (cache_.contains(pluginId)) {
        return;
    }

    QMap<QString, QString> store;
    QFile file(storagePath(pluginId));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                store.insert(it.key(), it.value().toString());
            }
        }
    }
    cache_.insert(pluginId, std::move(store));
}

bool PluginStorage::persist(const QString& pluginId) const {
    if (!cache_.contains(pluginId)) {
        return false;
    }

    QJsonObject obj;
    const auto& store = cache_[pluginId];
    for (auto it = store.begin(); it != store.end(); ++it) {
        obj.insert(it.key(), it.value());
    }

    QFile file(storagePath(pluginId));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

std::optional<QString> PluginStorage::get(const QString& pluginId, const QString& key) const {
    ensureLoaded(pluginId);
    const auto& store = cache_[pluginId];
    const auto it = store.find(key);
    if (it != store.end()) {
        return it.value();
    }
    return std::nullopt;
}

bool PluginStorage::set(const QString& pluginId, const QString& key, const QString& value) {
    ensureLoaded(pluginId);
    cache_[pluginId].insert(key, value);
    return persist(pluginId);
}

bool PluginStorage::deleteKey(const QString& pluginId, const QString& key) {
    ensureLoaded(pluginId);
    if (cache_[pluginId].remove(key) > 0) {
        return persist(pluginId);
    }
    return false;
}

QMap<QString, QString> PluginStorage::getAll(const QString& pluginId) const {
    ensureLoaded(pluginId);
    return cache_[pluginId];
}

bool PluginStorage::setAll(const QString& pluginId, const QMap<QString, QString>& entries) {
    ensureLoaded(pluginId);
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        cache_[pluginId].insert(it.key(), it.value());
    }
    return persist(pluginId);
}

bool PluginStorage::clear(const QString& pluginId) {
    ensureLoaded(pluginId);
    cache_[pluginId].clear();
    return persist(pluginId);
}

}  // namespace gs::app
