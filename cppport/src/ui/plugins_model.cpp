#include "plugins_model.hpp"
#include "plugin_qml_context.hpp"

#include "machine.hpp"
#include <QDir>
#include <QUrl>
#include <algorithm>

namespace gs::ui {

PluginsModel::PluginsModel(QObject* parent)
    : UiModelBase(parent), listModel_(this) {
    initRoles();
    connect(&machine_.pluginService(), &app::PluginService::pluginsChanged, this, &PluginsModel::refresh);
    refresh();
}

PluginsModel::PluginsModel(app::Machine& machine, QObject* parent)
    : UiModelBase(machine, parent), listModel_(this) {
    initRoles();
    connect(&machine_.pluginService(), &app::PluginService::pluginsChanged, this, &PluginsModel::refresh);
    refresh();
}

void PluginsModel::initRoles() {
    listModel_.setRoles({
        {Qt::UserRole + 1, "id", [](const PluginItem& p) { return p.id; }},
        {Qt::UserRole + 2, "name", [](const PluginItem& p) { return p.name; }},
        {Qt::UserRole + 3, "version", [](const PluginItem& p) { return p.version; }},
        {Qt::UserRole + 4, "description", [](const PluginItem& p) { return p.description; }},
        {Qt::UserRole + 5, "author", [](const PluginItem& p) { return p.author; }},
        {Qt::UserRole + 6, "engine", [](const PluginItem& p) { return p.engine; }},
        {Qt::UserRole + 7, "official", [](const PluginItem& p) { return p.official; }},
        {Qt::UserRole + 8, "enabled", [](const PluginItem& p) { return p.enabled; }},
        {Qt::UserRole + 9, "directory", [](const PluginItem& p) { return p.directory; }},
        {Qt::UserRole + 10, "uiEntry", [](const PluginItem& p) { return p.uiEntry; }},
        {Qt::UserRole + 11, "wasmEntry", [](const PluginItem& p) { return p.wasmEntry; }},
        {Qt::UserRole + 12, "capabilities", [](const PluginItem& p) { return p.capabilities; }},
        {Qt::UserRole + 13, "topics", [](const PluginItem& p) { return p.topics; }},
        {Qt::UserRole + 14, "contributionCount", [](const PluginItem& p) { return p.contributionCount; }}
    });
}

int PluginsModel::enabledCount() const {
    int total = 0;
    for (const auto& item : listModel_.items()) {
        if (item.enabled) ++total;
    }
    return total;
}

void PluginsModel::setSearch(const QString& search) {
    if (search_ == search) return;
    search_ = search;
    Q_EMIT filterChanged();
    refresh();
}

void PluginsModel::setFilter(const QString& filter) {
    if (filter_ == filter) return;
    filter_ = filter;
    Q_EMIT filterChanged();
    refresh();
}

void PluginsModel::scan() {
    machine_.pluginService().scanPlugins();
}

void PluginsModel::setEnabled(const QString& id, bool enabled) {
    machine_.pluginService().setPluginEnabled(id, enabled);
}

bool PluginsModel::isEnabled(const QString& id) const {
    return machine_.pluginService().isPluginEnabled(id);
}

QVariantMap PluginsModel::getPlugin(const QString& id) const {
    const auto* plugin = machine_.pluginService().findPlugin(id);
    if (!plugin) return {};
    QVariantMap map;
    map[QStringLiteral("id")] = plugin->manifest.id;
    map[QStringLiteral("name")] = plugin->manifest.name;
    map[QStringLiteral("version")] = plugin->manifest.version;
    map[QStringLiteral("description")] = plugin->manifest.description;
    map[QStringLiteral("author")] = plugin->manifest.author;
    map[QStringLiteral("engine")] = plugin->manifest.engine;
    map[QStringLiteral("official")] = plugin->manifest.isOfficial();
    map[QStringLiteral("enabled")] = plugin->enabled;
    map[QStringLiteral("directory")] = plugin->directory;
    map[QStringLiteral("wasmEntry")] = plugin->manifest.wasmEntry;
    map[QStringLiteral("uiEntry")] = plugin->manifest.uiEntry;

    QStringList caps;
    for (const auto& cap : plugin->manifest.capabilities.requestTypes) {
        caps.append(cap);
    }
    caps.sort();
    map[QStringLiteral("capabilities")] = caps;

    QStringList topics;
    for (const auto& top : plugin->manifest.capabilities.topics) {
        topics.append(top);
    }
    topics.sort();
    map[QStringLiteral("topics")] = topics;

    QVariantList contribs;
    for (const auto& c : plugin->manifest.contributions) {
        QVariantMap cm;
        cm[QStringLiteral("slot")] = c.slot;
        cm[QStringLiteral("label")] = c.label;
        cm[QStringLiteral("icon")] = c.icon;
        cm[QStringLiteral("route")] = c.route;
        cm[QStringLiteral("requiresIdle")] = c.requiresIdle;
        contribs.append(cm);
    }
    map[QStringLiteral("contributions")] = contribs;
    return map;
}

QVariantList PluginsModel::contributions(const QString& slot) const {
    QVariantList list;
    const auto pairs = machine_.pluginService().contributionsForSlot(slot);
    for (const auto& [plugin, contrib] : pairs) {
        QVariantMap map;
        map[QStringLiteral("pluginId")] = plugin.manifest.id;
        map[QStringLiteral("pluginName")] = plugin.manifest.name;
        map[QStringLiteral("slot")] = contrib.slot;
        map[QStringLiteral("label")] = contrib.label;
        map[QStringLiteral("icon")] = contrib.icon;
        map[QStringLiteral("route")] = contrib.route;
        map[QStringLiteral("requiresIdle")] = contrib.requiresIdle;
        const QString uiEntry = plugin.manifest.uiEntry.isEmpty()
            ? QString()
            : QDir(plugin.directory).filePath(plugin.manifest.uiEntry);
        map[QStringLiteral("uiEntry")] = uiEntry;
        map[QStringLiteral("uiUrl")] = uiEntry.isEmpty() ? QString() : QUrl::fromLocalFile(uiEntry).toString();
        list.append(map);
    }
    return list;
}

namespace {

QString contributionKey(const QVariantMap& c) {
    return QStringLiteral("plugin:%1:%2").arg(c.value("pluginId").toString(), c.value("route").toString());
}

QString contributionLabel(const QVariantMap& c) {
    const QString label = c.value("label").toString();
    return label.isEmpty() ? c.value("pluginName").toString() : label;
}

}  // namespace

QVariantList PluginsModel::toolCards() const {
    QVariantList cards;
    for (const QVariant& entry : contributions(QStringLiteral("tools-page"))) {
        const QVariantMap c = entry.toMap();
        const QString icon = c.value("icon").toString();
        cards.append(QVariantMap{{"key", contributionKey(c)},
                                 {"title", contributionLabel(c)},
                                 {"description", tr("Plugin tool by %1").arg(c.value("pluginName").toString())},
                                 {"icon", icon.isEmpty() ? QStringLiteral("PiPuzzlePiece") : icon},
                                 {"isPlugin", true},
                                 {"pluginId", c.value("pluginId")},
                                 {"uiUrl", c.value("uiUrl")}});
    }
    return cards;
}

QVariantList PluginsModel::toolTabs() const {
    QVariantList tabs;
    for (const QVariant& entry : contributions(QStringLiteral("tools-tab"))) {
        const QVariantMap c = entry.toMap();
        tabs.append(QVariantMap{{"key", contributionKey(c)},
                                {"label", contributionLabel(c)},
                                {"shown", true},
                                {"isPlugin", true},
                                {"pluginId", c.value("pluginId")},
                                {"uiUrl", c.value("uiUrl")}});
    }
    return tabs;
}

QObject* PluginsModel::createContext(const QString& pluginId) {
    return new PluginQmlContext(machine_.pluginService(), pluginId, this);
}

QVariantList PluginsModel::plugins() const {
    return listModel_.toList();
}

void PluginsModel::refresh() {
    std::vector<PluginItem> items;
    const auto& loaded = machine_.pluginService().plugins();
    const QString q = search_.trimmed();

    for (const auto& p : loaded) {
        if (filter_ == QStringLiteral("official") && !p.manifest.isOfficial()) {
            continue;
        }
        if (filter_ == QStringLiteral("community") && p.manifest.isOfficial()) {
            continue;
        }
        if (filter_ == QStringLiteral("enabled") && !p.enabled) {
            continue;
        }

        if (!q.isEmpty()) {
            const bool match = p.manifest.name.contains(q, Qt::CaseInsensitive)
                || p.manifest.id.contains(q, Qt::CaseInsensitive)
                || p.manifest.description.contains(q, Qt::CaseInsensitive)
                || p.manifest.author.contains(q, Qt::CaseInsensitive);
            if (!match) continue;
        }

        PluginItem item;
        item.id = p.manifest.id;
        item.name = p.manifest.name;
        item.version = p.manifest.version;
        item.description = p.manifest.description;
        item.author = p.manifest.author;
        item.engine = p.manifest.engine;
        item.official = p.manifest.isOfficial();
        item.enabled = p.enabled;
        item.directory = p.directory;
        item.uiEntry = p.manifest.uiEntry;
        item.wasmEntry = p.manifest.wasmEntry;
        for (const auto& cap : p.manifest.capabilities.requestTypes) {
            item.capabilities.append(cap);
        }
        item.capabilities.sort();
        for (const auto& top : p.manifest.capabilities.topics) {
            item.topics.append(top);
        }
        item.topics.sort();
        item.contributionCount = static_cast<int>(p.manifest.contributions.size());

        items.push_back(std::move(item));
    }

    listModel_.reset(std::move(items));
    Q_EMIT pluginsChanged();
}

}  // namespace gs::ui
