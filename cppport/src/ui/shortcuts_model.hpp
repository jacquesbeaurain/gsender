#pragma once

// Tools > Keyboard Shortcuts (features/Keyboard) for QML: every action with
// its keys, category and on/off switch, filtered by category or name; keys
// another active action uses are refused; changes are saved as they are
// made (upstream's page), keeping only what differs from the defaults.

#include <QUrl>
#include "app_settings.hpp"
#include "shortcuts.hpp"

#include "ui_model_base.hpp"
#include "struct_list_model.hpp"
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <map>
#include <string>
#include <vector>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class ShortcutsModel : public UiModelBase {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QStringList categories READ categories CONSTANT)  // "All" first
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY changed)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY changed)
    // Each {id, title, keys (as the platform writes them), category, active}.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    // Every action on / off (the toolbar's Enable All and Disable All are
    // disabled when they would change nothing).
    Q_PROPERTY(bool allActive READ allActive NOTIFY changed)
    Q_PROPERTY(bool noneActive READ noneActive NOTIFY changed)

public:
    explicit ShortcutsModel(QObject* parent = nullptr);

    bool enabled() const;
    void setEnabled(bool enabled);
    QStringList categories() const;
    QString category() const { return category_; }
    void setCategory(const QString& category);
    QString search() const { return search_; }
    void setSearch(const QString& search);
    struct ShortcutRow {
        QString id;
        QString title;
        QString keys;
        QString category;
        bool active = false;
    };
    QVariantList rows() const;
    bool allActive() const;
    bool noneActive() const;

    // A key press as a shortcut (portable text, e.g. "Shift+Right"); empty
    // for a modifier alone.
    Q_INVOKABLE QString keyFor(int key, int modifiers, const QString& text) const;
    Q_INVOKABLE QString nativeKeys(const QString& portable) const;
    Q_INVOKABLE QString keys(const QString& id) const;         // portable
    Q_INVOKABLE QString defaultKeys(const QString& id) const;  // portable
    Q_INVOKABLE bool isActive(const QString& id) const;
    // The action already using `keys`, else "" once they are set.
    Q_INVOKABLE QString setKeys(const QString& id, const QString& keys);
    Q_INVOKABLE void setActive(const QString& id, bool active);
    Q_INVOKABLE void resetAll();
    Q_INVOKABLE void setAllActive(bool active);
    // Delete this shortcut: its keys cleared (the action stays listed).
    Q_INVOKABLE void clearKeys(const QString& id);
    // Export and Import (gsender-shortcuts-<date>.json, upstream's format:
    // {version, exportDate, shortcuts: {id: {cmd, title, keys, isActive,
    // category}}}); "" on success, else the failure. Import merges what
    // matches an action.
    Q_INVOKABLE QString exportTo(const QUrl& file) const;
    Q_INVOKABLE QString importFrom(const QUrl& file);
    Q_INVOKABLE QString exportFileName() const;
    // Print: the active shortcuts by category as a page in the browser,
    // which prints it; "" or the failure.
    Q_INVOKABLE QString print() const;


private:
    void save();
    void syncRows() const;

    std::vector<app::ShortcutAction> actions_;  // the table and the macros
    std::map<std::string, app::ShortcutBinding> edits_;
    QString category_;
    QString search_;
    mutable StructListModel<ShortcutRow> rowsModel_;
};

}  // namespace gs::ui
