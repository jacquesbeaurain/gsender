#pragma once

// Macros (features/Macros) for QML: the two columns of macros, running one
// (with the loaded file's box, as macro:run), adding, editing, deleting and
// moving them, and importing and exporting gSender's macro files.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class MacrosModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Each column's macros {id, name, description}, in their order.
    Q_PROPERTY(QVariantList column1 READ column1 NOTIFY macrosChanged)
    Q_PROPERTY(QVariantList column2 READ column2 NOTIFY macrosChanged)
    Q_PROPERTY(int count READ count NOTIFY macrosChanged)
    // canRunMacro: connected, idle or running a macro, no job - or a
    // paused one.
    Q_PROPERTY(bool canRun READ canRun NOTIFY stateChanged)
    // MACRO_VARIABLES: {text, group} for the form's Variables list.
    Q_PROPERTY(QVariantList variables READ variables CONSTANT)

public:
    static constexpr int kMaxCharacters = 128;  // the name's and description's

    explicit MacrosModel(QObject* parent = nullptr);

    QVariantList column1() const { return column(QStringLiteral("column1")); }
    QVariantList column2() const { return column(QStringLiteral("column2")); }
    int count() const;
    bool canRun() const;
    QVariantList variables() const;

    // {id, name, content, description}; empty when there is none.
    Q_INVOKABLE QVariantMap macro(const QString& id) const;
    // False when it cannot run.
    Q_INVOKABLE bool run(const QString& id);
    // Name and content must not be empty. "" when done, else why not.
    Q_INVOKABLE QString add(const QString& name, const QString& content, const QString& description);
    Q_INVOKABLE QString update(const QString& id, const QString& name, const QString& content,
                               const QString& description);
    Q_INVOKABLE void remove(const QString& id);
    // Dragged to `column` at `index` among its macros.
    Q_INVOKABLE void move(const QString& id, const QString& column, int index);
    // A file path or file:// URL. What was done, for the notifications;
    // `ok` false on failure.
    Q_INVOKABLE QVariantMap importFile(const QString& file);
    Q_INVOKABLE QVariantMap exportFile(const QString& file);
    // gSender-macros-<date>.json, the export's suggested name.
    Q_INVOKABLE QString exportName() const;

Q_SIGNALS:
    void macrosChanged();
    void stateChanged();

private:
    QVariantList column(const QString& name) const;

    app::Machine& machine_;
};

}  // namespace gs::ui
