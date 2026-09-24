#pragma once

// Stats (features/Stats): the machine's record on upstream's five pages
// (StatMenu) - Overview: the connected port's job results and times, the
// recent jobs, upcoming maintenance, the configuration, getting help and
// the latest alarms and errors; Jobs: the history, searchable and sortable,
// with the jobs and run time per CNC; Maintenance: the tasks, added,
// edited, reset and deleted; Alarms: the log, the diagnostic file,
// clearing; About: the version, the team and the release notes.

#include "gs/config/history.hpp"

#include <QDialog>
#include <QStringList>

#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QTextBrowser;
class QVBoxLayout;

namespace gs::app {

class Machine;
class PieChart;

// A confirmation (upstream's Confirm and AlertDialog): true for yes. Tests
// answer instead of a message box.
using StatsConfirmer = std::function<bool(const QString& title, const QString& text)>;

// Add New Task / Edit Task (MaintenanceAddTaskDialog, MaintenanceEditTaskDialog
// and their MaintenanceTaskForm).
class MaintenanceTaskDialog final : public QDialog {
    Q_OBJECT
public:
    static constexpr int kDeleted = 2;  // the result when Delete was confirmed

    // `task`: the one edited, none for a new one.
    MaintenanceTaskDialog(const config::MaintenanceTask* task, StatsConfirmer confirmer, QWidget* parent = nullptr);

    void setName(const QString& name);
    void setRange(const QString& start, const QString& end);
    void setDescription(const QString& description);
    // The form's checks, showing their messages; true when all is well.
    bool validate();
    QString nameError() const;
    QString rangeStartError() const;
    QString rangeEndError() const;
    // The task as entered (the edited one's id and hours kept).
    config::MaintenanceTask task() const;
    // Add / Save: accepted when the form is valid.
    void submit();
    void requestDelete();

private:
    bool checkName();   // shows the name's note: the hint, or what is wrong
    bool checkRange();  // shows what is wrong with the range, if anything

    config::MaintenanceTask original_;
    bool editing_;
    StatsConfirmer confirmer_;
    QLineEdit* name_;
    QLabel* nameNote_;
    bool nameShowsError_ = false;
    QLineEdit* start_;
    QLabel* startError_;
    QLineEdit* end_;
    QLabel* endError_;
    QPlainTextEdit* description_;
};

class StatsDialog final : public QDialog {
    Q_OBJECT
public:
    enum class Page { Overview, Jobs, Maintenance, Alarms, About };

    explicit StatsDialog(Machine& machine, QWidget* parent = nullptr);

    void showPage(Page page);
    Page page() const;
    void reload();
    void setConfirmer(StatsConfirmer confirmer) { confirmer_ = std::move(confirmer); }

    // Overview, as shown ("label: value", the cells of a row joined by " | ").
    PieChart* resultsChart() const noexcept { return results_; }
    const QStringList& statRows() const noexcept { return statRows_; }
    const QStringList& recentJobs() const noexcept { return recentJobs_; }
    const QStringList& upcomingTasks() const noexcept { return upcoming_; }
    const QStringList& configurationRows() const noexcept { return configuration_; }
    const QStringList& alarmPreview() const noexcept { return alarmPreview_; }

    // Jobs
    QTableWidget* jobsTable() const noexcept { return jobs_; }
    PieChart* jobsPerCnc() const noexcept { return jobsPerCnc_; }
    PieChart* runTimePerCnc() const noexcept { return runTimePerCnc_; }
    void searchJobs(const QString& text);
    void clearJobHistory();  // confirmed

    // Maintenance
    QTableWidget* tasksTable() const noexcept { return tasks_; }
    void searchTasks(const QString& text);
    void resetTask(int id);  // the check mark, confirmed
    void resetAllTasks();    // confirmed
    void addTask();          // Add New Task
    void editTask(int id);

    // Alarms
    QTableWidget* alarmsTable() const noexcept { return alarms_; }
    void clearAlarms();  // confirmed

    // About: the releases shown, "1.6.4 (September 15, 2026)" and so on.
    const QStringList& releases() const noexcept { return releases_; }

Q_SIGNALS:
    void diagnosticsRequested();    // Download Diagnostic File
    void configurationRequested();  // the Configuration card's Change

private:
    QWidget* overviewPage();
    QWidget* jobsPage();
    QWidget* maintenancePage();
    QWidget* alarmsPage();
    QWidget* aboutPage();
    QWidget* diagnosticCard(bool compact);
    void fillOverview(const config::JobStats& stats, const std::vector<config::MaintenanceTask>& tasks,
                      const std::vector<config::AlarmRecord>& alarms);
    void fillMaintenancePreview(QVBoxLayout* layout, const std::vector<config::MaintenanceTask>& tasks, int limit,
                                QStringList* texts);
    void fillJobs(const config::JobStats& stats);
    void fillTasks(const std::vector<config::MaintenanceTask>& tasks);
    void fillAlarms(const std::vector<config::AlarmRecord>& alarms);
    bool confirm(const QString& title, const QString& text);

    Machine& machine_;
    StatsConfirmer confirmer_;
    QTabWidget* pages_;

    PieChart* results_;
    QLabel* resultsEmpty_;
    QVBoxLayout* statTable_;
    QVBoxLayout* recentList_;
    QVBoxLayout* upcomingList_;
    QLabel* profile_;
    QVBoxLayout* configurationList_;
    QVBoxLayout* alarmList_;
    QStringList statRows_;
    QStringList recentJobs_;
    QStringList upcoming_;
    QStringList configuration_;
    QStringList alarmPreview_;

    QTableWidget* jobs_;
    QLineEdit* jobSearch_;
    PieChart* jobsPerCnc_;
    PieChart* runTimePerCnc_;

    QTableWidget* tasks_;
    QLineEdit* taskSearch_;
    QVBoxLayout* upcomingPage_;
    QStringList upcomingPageTexts_;

    QStackedWidget* alarmsStack_;
    QTableWidget* alarms_;

    QTextBrowser* notes_;
    QStringList releases_;
};

}  // namespace gs::app
