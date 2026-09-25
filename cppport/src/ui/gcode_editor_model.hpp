#pragma once

// The G-code Editor (features/Visualizer/GcodeEditor) for QML: the loaded
// file's lines, each edited in place, selected with its box (Shift for a
// range), deleted, copied, searched, jumped to; saved back as the job or
// reverted. While a job runs it is read-only and follows the line running:
// the lines done, running (it and the two after) and to come.

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <set>
#include <vector>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class GcodeEditorModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY linesChanged)
    Q_PROPERTY(bool hasChanges READ hasChanges NOTIFY linesChanged)
    // "Running", "Paused" or "" (editable).
    Q_PROPERTY(QString jobState READ jobState NOTIFY jobChanged)
    Q_PROPERTY(bool jobRunning READ jobRunning NOTIFY jobChanged)
    // 0-based row of the line running, or -1.
    Q_PROPERTY(int runningRow READ runningRow NOTIFY jobChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY searchChanged)
    Q_PROPERTY(int currentMatch READ currentMatch NOTIFY searchChanged)   // 0-based, -1 for none
    Q_PROPERTY(int currentMatchRow READ currentMatchRow NOTIFY searchChanged)

public:
    enum Role { TextRole = Qt::UserRole + 1, StyledRole, SelectedRole, MatchRole, CurrentMatchRole, StatusRole };

    explicit GcodeEditorModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(lines_.size()); }
    bool hasChanges() const { return lines_ != original_; }
    QString jobState() const;
    bool jobRunning() const;
    int runningRow() const { return runningRow_; }
    int selectedCount() const { return static_cast<int>(selected_.size()); }
    int matchCount() const { return static_cast<int>(matches_.size()); }
    int currentMatch() const { return currentMatch_; }
    int currentMatchRow() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void setLine(int row, const QString& text);
    // The line's box: toggled, or with `range` everything from the last one.
    Q_INVOKABLE void toggleSelected(int row, bool range);
    Q_INVOKABLE void clearSelection();
    // All, or none when all are.
    Q_INVOKABLE void toggleSelectAll();
    Q_INVOKABLE void deleteSelected();
    // The selected lines, else everything; what was copied, for the toast.
    Q_INVOKABLE QString copy();
    Q_INVOKABLE bool revertChanges();
    // The edited text becomes the job (same name); not while a job runs.
    Q_INVOKABLE bool save();
    Q_INVOKABLE void setSearch(const QString& query);
    Q_INVOKABLE void nextMatch();
    Q_INVOKABLE void previousMatch();
    Q_INVOKABLE QString text() const { return lines_.join('\n'); }

Q_SIGNALS:
    void linesChanged();
    void jobChanged();
    void selectionChanged();
    void searchChanged();

private:
    void followJob();
    void findMatches();
    void rowsChanged(const QList<int>& roles);

    app::Machine& machine_;
    QStringList lines_;
    QStringList original_;
    std::vector<std::size_t> senderLines_;  // file line of each sender line
    std::set<int> selected_;
    int lastSelected_ = -1;
    QString query_;
    std::vector<int> matches_;
    std::set<int> matchSet_;
    int currentMatch_ = -1;
    int runningRow_ = -1;
    bool running_ = false;
};

}  // namespace gs::ui
