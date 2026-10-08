#include "shortcuts_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLocale>
#include <QMap>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>

#include <algorithm>

namespace gs::ui {
namespace {

QKeySequence parse(const QString& portable) {
    return QKeySequence::fromString(portable, QKeySequence::PortableText);
}

}  // namespace

ShortcutsModel::ShortcutsModel(QObject* parent)
    : UiModelBase(parent),
      actions_(app::shortcutActions(machine_)),
      edits_(machine_.settings().shortcuts) {
    rowsModel_.setRoles({
        {Qt::UserRole + 1, "id", [](const ShortcutRow& r) { return r.id; }},
        {Qt::UserRole + 2, "title", [](const ShortcutRow& r) { return r.title; }},
        {Qt::UserRole + 3, "keys", [](const ShortcutRow& r) { return r.keys; }},
        {Qt::UserRole + 4, "category", [](const ShortcutRow& r) { return r.category; }},
        {Qt::UserRole + 5, "active", [](const ShortcutRow& r) { return r.active; }},
    });
    connect(&machine_, &app::Machine::appSettingsChanged, this, &ShortcutsModel::changed);
}

bool ShortcutsModel::enabled() const {
    return machine_.settings().shortcutsEnabled;
}

void ShortcutsModel::setEnabled(bool enabled) {
    if (enabled == this->enabled()) {
        return;
    }
    app::AppSettings settings = machine_.settings();
    settings.shortcutsEnabled = enabled;
    machine_.setSettings(settings);
    Q_EMIT changed();
}

QStringList ShortcutsModel::categories() const {
    QStringList list{tr("All")};
    for (const app::ShortcutAction& action : actions_) {
        if (!list.contains(action.category)) {
            list.append(action.category);
        }
    }
    return list;
}

void ShortcutsModel::setCategory(const QString& category) {
    category_ = category == tr("All") ? QString() : category;
    Q_EMIT changed();
}

void ShortcutsModel::setSearch(const QString& search) {
    search_ = search.trimmed();
    Q_EMIT changed();
}

void ShortcutsModel::syncRows() const {
    std::vector<ShortcutRow> list;
    for (const app::ShortcutAction& action : actions_) {
        if (!category_.isEmpty() && action.category != category_) {
            continue;
        }
        const QString title = action.grblHalOnly ? action.title + tr(" (grblHAL)") : action.title;
        if (!search_.isEmpty() && !title.contains(search_, Qt::CaseInsensitive) &&
            !action.category.contains(search_, Qt::CaseInsensitive)) {
            continue;
        }
        list.push_back({
            action.id,
            title,
            nativeKeys(keys(action.id)),
            action.category,
            isActive(action.id),
        });
    }
    rowsModel_.reset(std::move(list));
}

QVariantList ShortcutsModel::rows() const {
    syncRows();
    return rowsModel_.toVariantList();
}

QString ShortcutsModel::keyFor(int key, int modifiers, const QString& text) const {
    switch (key) {
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Meta:
        case Qt::Key_Alt:
        case Qt::Key_AltGr:
        case Qt::Key_unknown:
            return {};
        default: break;
    }
    const QKeyEvent event(QEvent::KeyPress, key, Qt::KeyboardModifiers(modifiers), text);
    return QKeySequence(app::shortcutKey(event)).toString(QKeySequence::PortableText);
}

QString ShortcutsModel::nativeKeys(const QString& portable) const {
    return parse(portable).toString(QKeySequence::NativeText);
}

QString ShortcutsModel::keys(const QString& id) const {
    if (const auto it = edits_.find(id.toStdString()); it != edits_.end()) {
        return parse(QString::fromStdString(it->second.keys)).toString(QKeySequence::PortableText);
    }
    return defaultKeys(id);
}

QString ShortcutsModel::defaultKeys(const QString& id) const {
    const app::ShortcutAction* action = app::findShortcutAction(actions_, id);
    return action ? parse(action->defaultKeys).toString(QKeySequence::PortableText) : QString();
}

bool ShortcutsModel::isActive(const QString& id) const {
    if (const auto it = edits_.find(id.toStdString()); it != edits_.end()) {
        return it->second.active;
    }
    const app::ShortcutAction* action = app::findShortcutAction(actions_, id);
    return !action || action->defaultActive;
}

QString ShortcutsModel::setKeys(const QString& id, const QString& keys) {
    const QKeySequence sequence = parse(keys);
    const QKeySequence single = sequence.isEmpty() ? QKeySequence() : QKeySequence(sequence[0]);
    const QString text = single.toString(QKeySequence::PortableText);
    if (!single.isEmpty()) {
        for (const app::ShortcutAction& other : actions_) {
            if (other.id != id && isActive(other.id) && this->keys(other.id) == text) {
                return other.title;
            }
        }
    }
    // New keys switch the shortcut on (upstream's EditArea saves isActive:
    // true) - which is how a macro's shortcut starts working.
    app::ShortcutBinding& binding = edits_[id.toStdString()];
    binding.active = !single.isEmpty() || isActive(id);
    binding.keys = text.toStdString();
    save();
    return {};
}

void ShortcutsModel::setActive(const QString& id, bool active) {
    edits_[id.toStdString()] = app::ShortcutBinding{keys(id).toStdString(), active};
    save();
}

void ShortcutsModel::resetAll() {
    edits_.clear();
    app::AppSettings settings = machine_.settings();
    settings.shortcuts.clear();
    settings.shortcutsEnabled = true;
    machine_.setSettings(settings);
    Q_EMIT changed();
}

bool ShortcutsModel::allActive() const {
    return std::all_of(actions_.begin(), actions_.end(), [this](const app::ShortcutAction& a) { return isActive(a.id); });
}

bool ShortcutsModel::noneActive() const {
    return std::none_of(actions_.begin(), actions_.end(), [this](const app::ShortcutAction& a) { return isActive(a.id); });
}

void ShortcutsModel::setAllActive(bool active) {
    for (const app::ShortcutAction& action : actions_) {
        edits_[action.id.toStdString()] = app::ShortcutBinding{keys(action.id).toStdString(), active};
    }
    save();
}

void ShortcutsModel::clearKeys(const QString& id) {
    edits_[id.toStdString()] = app::ShortcutBinding{std::string(), isActive(id)};
    save();
}

QString ShortcutsModel::exportFileName() const {
    return QStringLiteral("gsender-shortcuts-%1.json").arg(QDate::currentDate().toString(Qt::ISODate));
}

QString ShortcutsModel::exportTo(const QUrl& file) const {
    QJsonObject shortcuts;
    for (const app::ShortcutAction& action : actions_) {
        shortcuts[action.id] = QJsonObject{{"cmd", action.id},
                                           {"title", action.title},
                                           {"keys", keys(action.id).toLower()},
                                           {"isActive", isActive(action.id)},
                                           {"category", action.category}};
    }
    const QJsonObject root{{"version", "1.0"},
                           {"exportDate", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                           {"shortcuts", shortcuts}};
    QFile out(file.toLocalFile());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return tr("Failed to export shortcuts.");
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return {};
}

QString ShortcutsModel::importFrom(const QUrl& file) {
    QFile in(file.toLocalFile());
    const QJsonDocument document = in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()) : QJsonDocument();
    const QJsonValue shortcuts = document.object().value("shortcuts");
    if (!shortcuts.isObject()) {
        return tr("Failed to import shortcuts. Please check the file format.");
    }
    const QJsonObject list = shortcuts.toObject();
    for (auto it = list.begin(); it != list.end(); ++it) {
        const QString id = it.key();
        if (!app::findShortcutAction(actions_, id) || !it.value().isObject()) {
            continue;
        }
        const QJsonObject entry = it.value().toObject();
        QString text = keys(id);
        if (entry.contains("keys")) {
            const QKeySequence sequence = parse(entry.value("keys").toString());
            text = sequence.isEmpty() ? QString() : QKeySequence(sequence[0]).toString(QKeySequence::PortableText);
        }
        edits_[id.toStdString()] = app::ShortcutBinding{
            text.toStdString(), entry.contains("isActive") ? entry.value("isActive").toBool() : isActive(id)};
    }
    save();
    return {};
}

QString ShortcutsModel::print() const {
    QMap<QString, QStringList> byCategory;
    for (const app::ShortcutAction& action : actions_) {
        const QString text = keys(action.id);
        if (isActive(action.id) && !text.isEmpty()) {
            byCategory[action.category] << QStringLiteral("<div class=\"shortcut\"><span>%1</span><code>%2</code></div>")
                                               .arg(action.title.toHtmlEscaped(), nativeKeys(text).toHtmlEscaped());
        }
    }
    QString body = QStringLiteral("<h1>gSender Keyboard Shortcuts</h1><p>Generated on %1</p>")
                       .arg(QLocale().toString(QDate::currentDate()));
    for (auto it = byCategory.begin(); it != byCategory.end(); ++it) {
        body += QStringLiteral("<h2>%1</h2><div class=\"shortcuts\">%2</div>")
                    .arg(it.key().toHtmlEscaped(), it.value().join(QString()));
    }
    const QString page = QStringLiteral(
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>gSender Keyboard Shortcuts</title><style>"
        "body{font-family:system-ui,Segoe UI,sans-serif;padding:20px;color:#111}h1{font-size:22px}h2{font-size:15px;"
        "background:#f3f4f6;padding:6px 10px;border-radius:4px}.shortcuts{display:grid;grid-template-columns:repeat("
        "auto-fill,minmax(280px,1fr));gap:8px}.shortcut{display:flex;justify-content:space-between;padding:6px 8px;"
        "border:1px solid #eaeaea;border-radius:3px;background:#fafafa;font-size:12px}code{background:#f1f5f9;"
        "padding:2px 6px;border-radius:3px;border:1px solid #e2e8f0;font-weight:600}</style></head><body>%1"
        "<script>window.onload=function(){window.print()}</script></body></html>").arg(body);
    QTemporaryFile temp(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/gsender-shortcuts-XXXXXX.html");
    temp.setAutoRemove(false);
    if (!temp.open()) {
        return tr("Unable to create print document.");
    }
    temp.write(page.toUtf8());
    temp.close();
    return QDesktopServices::openUrl(QUrl::fromLocalFile(temp.fileName())) ? QString()
                                                                           : tr("Failed to print shortcuts.");
}

void ShortcutsModel::save() {
    // Keep only what differs from gSender's defaults.
    std::map<std::string, app::ShortcutBinding> changes;
    for (const auto& [id, binding] : edits_) {
        const QString name = QString::fromStdString(id);
        const app::ShortcutAction* action = app::findShortcutAction(actions_, name);
        const bool defaultActive = !action || action->defaultActive;
        if (binding.active != defaultActive ||
            parse(QString::fromStdString(binding.keys)).toString(QKeySequence::PortableText) != defaultKeys(name)) {
            changes[id] = binding;
        }
    }
    app::AppSettings settings = machine_.settings();
    settings.shortcuts = std::move(changes);
    machine_.setSettings(settings);
    Q_EMIT changed();
}

}  // namespace gs::ui
