#include "shortcuts_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include <QKeyEvent>
#include <QKeySequence>

namespace gs::ui {
namespace {

QKeySequence parse(const QString& portable) {
    return QKeySequence::fromString(portable, QKeySequence::PortableText);
}

}  // namespace

ShortcutsModel::ShortcutsModel(QObject* parent)
    : QObject(parent),
      machine_(UiBackend::instance()->machine()),
      actions_(app::shortcutActions(machine_)),
      edits_(machine_.settings().shortcuts) {
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

QVariantList ShortcutsModel::rows() const {
    QVariantList list;
    for (const app::ShortcutAction& action : actions_) {
        if (!category_.isEmpty() && action.category != category_) {
            continue;
        }
        const QString title = action.grblHalOnly ? action.title + tr(" (grblHAL)") : action.title;
        if (!search_.isEmpty() && !title.contains(search_, Qt::CaseInsensitive) &&
            !action.category.contains(search_, Qt::CaseInsensitive)) {
            continue;
        }
        list.append(QVariantMap{
            {"id", action.id},
            {"title", title},
            {"keys", nativeKeys(keys(action.id))},
            {"category", action.category},
            {"active", isActive(action.id)},
        });
    }
    return list;
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
