#include "shortcuts_dialog.hpp"

#include "machine.hpp"
#include "shortcuts.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace gs::app {
namespace {

enum Column { kAction, kKeys, kCategory, kActive };

QString defaultKeys(const QString& id) {
    const ShortcutAction* action = findShortcutAction(id);
    return action ? action->defaultKeys : QString();
}

}  // namespace

ShortcutsDialog::ShortcutsDialog(Machine& machine, QWidget* parent)
    : QDialog(parent), machine_(machine), edits_(machine.settings().shortcuts) {
    setWindowTitle(tr("Keyboard Shortcuts"));
    resize(760, 620);
    auto* layout = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    enabled_ = new QCheckBox(tr("Enable keyboard shortcuts"));
    enabled_->setChecked(machine_.settings().shortcutsEnabled);
    category_ = new QComboBox;
    category_->addItem(tr("All"));
    for (const ShortcutAction& action : shortcutActions()) {
        if (category_->findText(action.category) < 0) {
            category_->addItem(action.category);
        }
    }
    top->addWidget(enabled_);
    top->addStretch();
    top->addWidget(new QLabel(tr("Category")));
    top->addWidget(category_);
    layout->addLayout(top);

    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels({tr("Action"), tr("Shortcut"), tr("Category"), tr("Active")});
    table_->horizontalHeader()->setSectionResizeMode(kAction, QHeaderView::Stretch);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    layout->addWidget(table_, 1);
    layout->addWidget(new QLabel(tr("Double-click a shortcut to change it. Jogging shortcuts jog while held.")));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton* reset = buttons->addButton(tr("Reset All to Defaults"), QDialogButtonBox::ResetRole);
    layout->addWidget(buttons);
    connect(reset, &QPushButton::clicked, this, &ShortcutsDialog::resetAll);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(category_, &QComboBox::currentIndexChanged, this, &ShortcutsDialog::fill);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { editRow(row); });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!filling_ && item->column() == kActive) {
            setActive(item->data(Qt::UserRole).toString(), item->checkState() == Qt::Checked);
        }
    });
    fill();
}

QKeySequence ShortcutsDialog::keys(const QString& id) const {
    if (const auto it = edits_.find(id.toStdString()); it != edits_.end()) {
        return QKeySequence::fromString(QString::fromStdString(it->second.keys), QKeySequence::PortableText);
    }
    return QKeySequence::fromString(defaultKeys(id), QKeySequence::PortableText);
}

bool ShortcutsDialog::isActive(const QString& id) const {
    const auto it = edits_.find(id.toStdString());
    return it == edits_.end() || it->second.active;
}

bool ShortcutsDialog::setKeys(const QString& id, const QKeySequence& keys, QString* conflict) {
    const QKeySequence single = keys.isEmpty() ? QKeySequence() : QKeySequence(keys[0]);
    if (!single.isEmpty()) {
        for (const ShortcutAction& other : shortcutActions()) {
            if (other.id != id && isActive(other.id) && this->keys(other.id) == single) {
                if (conflict) {
                    *conflict = other.title;
                }
                return false;
            }
        }
    }
    ShortcutBinding& binding = edits_[id.toStdString()];
    binding.active = isActive(id);
    binding.keys = single.toString(QKeySequence::PortableText).toStdString();
    fill();
    return true;
}

void ShortcutsDialog::setActive(const QString& id, bool active) {
    ShortcutBinding binding{keys(id).toString(QKeySequence::PortableText).toStdString(), active};
    edits_[id.toStdString()] = binding;
    fill();
}

void ShortcutsDialog::resetAll() {
    edits_.clear();
    enabled_->setChecked(true);
    fill();
}

void ShortcutsDialog::save() {
    // Keep only what differs from gSender's defaults.
    std::map<std::string, ShortcutBinding> changes;
    for (const auto& [id, binding] : edits_) {
        const QString defaults = defaultKeys(QString::fromStdString(id));
        const QString keys = QKeySequence::fromString(QString::fromStdString(binding.keys), QKeySequence::PortableText)
                                 .toString(QKeySequence::PortableText);
        const QString normalDefaults =
            QKeySequence::fromString(defaults, QKeySequence::PortableText).toString(QKeySequence::PortableText);
        if (!binding.active || keys != normalDefaults) {
            changes[id] = binding;
        }
    }
    AppSettings settings = machine_.settings();
    settings.shortcuts = std::move(changes);
    settings.shortcutsEnabled = enabled_->isChecked();
    machine_.setSettings(settings);
}

void ShortcutsDialog::fill() {
    filling_ = true;
    const QString category = category_->currentIndex() > 0 ? category_->currentText() : QString();
    table_->setRowCount(0);
    for (const ShortcutAction& action : shortcutActions()) {
        if (!category.isEmpty() && action.category != category) {
            continue;
        }
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* title = new QTableWidgetItem(action.grblHalOnly ? action.title + tr(" (grblHAL)") : action.title);
        title->setData(Qt::UserRole, action.id);
        auto* keysItem = new QTableWidgetItem(keys(action.id).toString(QKeySequence::NativeText));
        auto* categoryItem = new QTableWidgetItem(action.category);
        auto* active = new QTableWidgetItem;
        active->setData(Qt::UserRole, action.id);
        active->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        active->setCheckState(isActive(action.id) ? Qt::Checked : Qt::Unchecked);
        table_->setItem(row, kAction, title);
        table_->setItem(row, kKeys, keysItem);
        table_->setItem(row, kCategory, categoryItem);
        table_->setItem(row, kActive, active);
    }
    table_->resizeColumnToContents(kKeys);
    table_->resizeColumnToContents(kCategory);
    filling_ = false;
}

void ShortcutsDialog::editRow(int row) {
    const QTableWidgetItem* item = table_->item(row, kAction);
    if (!item) {
        return;
    }
    const QString id = item->data(Qt::UserRole).toString();
    QDialog edit(this);
    edit.setWindowTitle(tr("Shortcut - %1").arg(item->text()));
    auto* layout = new QVBoxLayout(&edit);
    layout->addWidget(new QLabel(tr("Press the new keys:")));
    auto* keysEdit = new QKeySequenceEdit(keys(id));
    keysEdit->setMaximumSequenceLength(1);
    layout->addWidget(keysEdit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton* clear = buttons->addButton(tr("None"), QDialogButtonBox::ResetRole);
    QPushButton* restore = buttons->addButton(tr("Default"), QDialogButtonBox::ResetRole);
    layout->addWidget(buttons);
    connect(clear, &QPushButton::clicked, keysEdit, &QKeySequenceEdit::clear);
    connect(restore, &QPushButton::clicked, keysEdit,
            [keysEdit, id] { keysEdit->setKeySequence(QKeySequence::fromString(defaultKeys(id), QKeySequence::PortableText)); });
    connect(buttons, &QDialogButtonBox::accepted, &edit, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &edit, &QDialog::reject);
    if (edit.exec() != QDialog::Accepted) {
        return;
    }
    QString conflict;
    if (!setKeys(id, keysEdit->keySequence(), &conflict)) {
        QMessageBox::warning(this, tr("Shortcut in use"),
                             tr("%1 is already used by \"%2\".")
                                 .arg(keysEdit->keySequence().toString(QKeySequence::NativeText), conflict));
    }
}

}  // namespace gs::app
