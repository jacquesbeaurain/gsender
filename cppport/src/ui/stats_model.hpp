#pragma once

// The Stats page (features/Stats) for QML: the machine's record on
// upstream's five pages - Overview (the connected port's job results and
// times, the recent jobs, upcoming maintenance, the configuration, getting
// help, the latest alarms), Jobs (the history, searchable, with the jobs and
// run time per CNC), Maintenance (the tasks, added, edited, reset, deleted),
// Alarms (the log, the diagnostic file, clearing) and About (the version,
// the team, the release notes). What asks first is asked by the QML.

#include "ui_model_base.hpp"
#include "struct_list_model.hpp"

#include "gs/config/history.hpp"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QTimer;

namespace gs::app {
class Machine;
}

namespace gs::ui {

class StatsModel : public UiModelBase {
    Q_OBJECT
    QML_ELEMENT

    // Overview.
    Q_PROPERTY(int completeJobs READ completeJobs NOTIFY changed)
    Q_PROPERTY(int incompleteJobs READ incompleteJobs NOTIFY changed)
    Q_PROPERTY(QVariantList statRows READ statRows NOTIFY changed)          // {label, value}
    Q_PROPERTY(QVariantList recentJobs READ recentJobs NOTIFY changed)      // {file, duration, complete}
    Q_PROPERTY(QString profile READ profile NOTIFY changed)                 // "Sienci LongMill MK2 30x30"
    Q_PROPERTY(QVariantList configuration READ configuration NOTIFY changed)  // {label, value}
    Q_PROPERTY(QVariantList alarmPreview READ alarmPreview NOTIFY changed)  // {alarm, what, when}
    // Jobs: those matching jobSearch, newest first or sorted by jobSort
    // ("file", "duration", "lines", "start", "complete") {file, path,
    // duration, lines, start, complete}.
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
    Q_PROPERTY(QString jobSearch READ jobSearch WRITE setJobSearch NOTIFY changed)
    Q_PROPERTY(QString jobSort READ jobSort NOTIFY changed)
    Q_PROPERTY(bool jobsAscending READ jobsAscending NOTIFY changed)
    Q_PROPERTY(StructListModelBase* jobsListModel READ jobsListModel CONSTANT)
    // Per CNC: {labels, values}.
    Q_PROPERTY(QVariantMap jobsPerCnc READ jobsPerCnc NOTIFY changed)
    Q_PROPERTY(QVariantMap runTimePerCnc READ runTimePerCnc NOTIFY changed)
    // Maintenance: those matching taskSearch, in the list's order {id, name,
    // description, state ("hours"/"due"/"urgent"), hours}; the upcoming ones
    // {name, hours, word, color}.
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY changed)
    Q_PROPERTY(QString taskSearch READ taskSearch WRITE setTaskSearch NOTIFY changed)
    Q_PROPERTY(StructListModelBase* tasksListModel READ tasksListModel CONSTANT)
    Q_PROPERTY(QVariantList upcoming READ upcoming NOTIFY changed)  // three
    Q_PROPERTY(QVariantList upcomingMore READ upcomingMore NOTIFY changed)  // six
    // Alarms: newest first {alarm, title, time, message, line}.
    Q_PROPERTY(QVariantList alarms READ alarms NOTIFY changed)
    Q_PROPERTY(StructListModelBase* alarmsListModel READ alarmsListModel CONSTANT)
    // About.
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QVariantList releases READ releases CONSTANT)  // {heading, notes: []}

public:
    explicit StatsModel(QObject* parent = nullptr);

    struct StatRow { QString label; QString value; };
    struct RecentJobRow { QString file; QString duration; bool complete = false; };
    struct ConfigRow { QString label; QString value; };
    struct AlarmPreviewRow { bool alarm = false; QString what; QString when; };
    struct JobRow {
        QString file;
        QString path;
        QString duration;
        int lines = 0;
        QString start;
        bool complete = false;
    };
    struct TaskRow {
        int id = 0;
        QString name;
        QString description;
        QString state;
        QString hours;
    };
    struct UpcomingRow { QString name; QString hours; QString word; QString color; };
    struct AlarmRow {
        bool alarm = false;
        QString title;
        QString time;
        QString message;
        QString line;
    };

    int completeJobs() const { return completeJobs_; }
    int incompleteJobs() const { return incompleteJobs_; }
    QVariantList statRows() const;
    QVariantList recentJobs() const;
    QString profile() const { return profile_; }
    QVariantList configuration() const;
    QVariantList alarmPreview() const;
    QVariantList jobs() const;
    StructListModelBase* jobsListModel() { return &jobsModel_; }
    QString jobSearch() const { return jobSearch_; }
    void setJobSearch(const QString& search);
    QString jobSort() const { return jobSort_; }
    bool jobsAscending() const { return jobsAscending_; }
    QVariantMap jobsPerCnc() const { return jobsPerCnc_; }
    QVariantMap runTimePerCnc() const { return runTimePerCnc_; }
    QVariantList tasks() const;
    StructListModelBase* tasksListModel() { return &tasksModel_; }
    QString taskSearch() const { return taskSearch_; }
    void setTaskSearch(const QString& search);
    QVariantList upcoming() const;
    QVariantList upcomingMore() const;
    QVariantList alarms() const;
    StructListModelBase* alarmsListModel() { return &alarmsModel_; }
    QString version() const;
    QVariantList releases() const;

    Q_INVOKABLE void reload();
    // A column header tapped: sorted by it ascending, or the other way round
    // when it already was.
    Q_INVOKABLE void sortJobs(const QString& column);
    Q_INVOKABLE void clearJobHistory();
    Q_INVOKABLE void clearAlarms();
    Q_INVOKABLE void resetTask(int id);
    Q_INVOKABLE void resetAllTasks();
    // MaintenanceTaskForm's checks: the message, "" when fine.
    Q_INVOKABLE QString nameProblem(const QString& name) const;
    Q_INVOKABLE QString rangeProblem(const QString& start, const QString& end) const;
    // Add (id < 0) or save; false when the form's checks fail.
    Q_INVOKABLE bool saveTask(int id, const QString& name, const QString& start, const QString& end,
                              const QString& description);
    Q_INVOKABLE void deleteTask(int id);
    Q_INVOKABLE QVariantMap task(int id) const;
    // Download Diagnostic File: a file path or URL; "" when written, else why not.
    Q_INVOKABLE QString writeDiagnostics(const QString& file);
    Q_INVOKABLE QString diagnosticsName() const;


private:
    void listJobs();
    void listTasks();

    QTimer* refresh_;
    std::vector<config::JobRecord> jobRecords_;
    std::vector<config::MaintenanceTask> taskRecords_;
    QString jobSearch_;
    QString jobSort_ = QStringLiteral("start");
    bool jobsAscending_ = false;
    QString taskSearch_;
    int completeJobs_ = 0;
    int incompleteJobs_ = 0;
    QString profile_;
    QVariantMap jobsPerCnc_;
    QVariantMap runTimePerCnc_;
    StructListModel<StatRow> statRowsModel_;
    StructListModel<RecentJobRow> recentJobsModel_;
    StructListModel<ConfigRow> configurationModel_;
    StructListModel<AlarmPreviewRow> alarmPreviewModel_;
    StructListModel<JobRow> jobsModel_;
    StructListModel<TaskRow> tasksModel_;
    StructListModel<UpcomingRow> upcomingModel_;
    StructListModel<UpcomingRow> upcomingMoreModel_;
    StructListModel<AlarmRow> alarmsModel_;
};

}  // namespace gs::ui
