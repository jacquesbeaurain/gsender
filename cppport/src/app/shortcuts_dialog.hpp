#pragma once

// Tools > Keyboard Shortcuts: gSender's shortcut table - every action with
// its keys, category and on/off state, editable, with conflicts refused and
// a reset to the defaults. Changes apply on OK.

#include "app_settings.hpp"

#include <QDialog>
#include <QKeySequence>

#include <map>
#include <string>

class QCheckBox;
class QComboBox;
class QTableWidget;

namespace gs::app {

class Machine;

class ShortcutsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutsDialog(Machine& machine, QWidget* parent = nullptr);

    // Edits, kept until save(). setKeys refuses keys another active action
    // uses (and says which in `conflict`).
    bool setKeys(const QString& id, const QKeySequence& keys, QString* conflict = nullptr);
    void setActive(const QString& id, bool active);
    void resetAll();
    void save();

    QKeySequence keys(const QString& id) const;
    bool isActive(const QString& id) const;

private:
    void fill();
    void editRow(int row);

    Machine& machine_;
    std::map<std::string, ShortcutBinding> edits_;
    QTableWidget* table_;
    QComboBox* category_;
    QCheckBox* enabled_;
    bool filling_ = false;
};

}  // namespace gs::app
