#pragma once

#include "ui_model_base.hpp"
#include "struct_list_model.hpp"
#include "plugin_manifest.hpp"
#include "plugin_service.hpp"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace gs::app {
class Machine;
}

namespace gs::ui {

struct PluginItem {
    QString id;
    QString name;
    QString version;
    QString description;
    QString author;
    QString engine;
    bool official = false;
    bool enabled = true;
    QString directory;
    QString uiEntry;
    QString wasmEntry;
    QStringList capabilities;
    QStringList topics;
    int contributionCount = 0;
};

class PluginsModel : public UiModelBase {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY pluginsChanged)
    Q_PROPERTY(int enabledCount READ enabledCount NOTIFY pluginsChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(StructListModelBase* pluginsModel READ pluginsModel CONSTANT)
    // The plugins' Tools page cards ("tools-page") {key, title, description,
    // icon, isPlugin, pluginId, uiUrl} and tool area tabs ("tools-tab")
    // {key, label, shown, isPlugin, pluginId, uiUrl}; a key is
    // "plugin:<id>:<route>".
    Q_PROPERTY(QVariantList toolCards READ toolCards NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantList toolTabs READ toolTabs NOTIFY pluginsChanged)

public:
    explicit PluginsModel(QObject* parent = nullptr);
    explicit PluginsModel(app::Machine& machine, QObject* parent = nullptr);

    int count() const { return listModel_.count(); }
    int enabledCount() const;

    QString search() const { return search_; }
    void setSearch(const QString& search);

    QString filter() const { return filter_; }
    void setFilter(const QString& filter);

    QVariantList plugins() const;
    StructListModelBase* pluginsModel() { return &listModel_; }
    QVariantList toolCards() const;
    QVariantList toolTabs() const;

    Q_INVOKABLE void scan();
    Q_INVOKABLE void setEnabled(const QString& id, bool enabled);
    Q_INVOKABLE bool isEnabled(const QString& id) const;
    Q_INVOKABLE QVariantMap getPlugin(const QString& id) const;
    Q_INVOKABLE QVariantList contributions(const QString& slot) const;

    /** Creates an isolated QML bridge context for a specific plugin. */
    Q_INVOKABLE QObject* createContext(const QString& pluginId);

Q_SIGNALS:
    void pluginsChanged();
    void filterChanged();

private:
    void initRoles();
    void refresh();

    QString search_;
    QString filter_ = QStringLiteral("all");
    StructListModel<PluginItem> listModel_;
};

}  // namespace gs::ui
