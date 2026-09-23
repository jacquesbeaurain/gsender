#pragma once

// Tools > Statistics (gSender's Stats page): the job history with its
// totals, the maintenance tasks with the hours the machine has run since
// each was last done, and the alarm and error log.

#include <QDialog>

class QLabel;
class QTableWidget;

namespace gs::app {

class Machine;

class StatsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit StatsDialog(Machine& machine, QWidget* parent = nullptr);

    void reload();
    QString totalsText() const;
    QTableWidget* jobsTable() const noexcept { return jobs_; }
    QTableWidget* tasksTable() const noexcept { return tasks_; }
    QTableWidget* alarmsTable() const noexcept { return alarms_; }

private:
    void editTask(int id);  // -1: a new task
    int selectedTaskId() const;

    Machine& machine_;
    QLabel* totals_;
    QTableWidget* jobs_;
    QTableWidget* tasks_;
    QTableWidget* alarms_;
};

}  // namespace gs::app
