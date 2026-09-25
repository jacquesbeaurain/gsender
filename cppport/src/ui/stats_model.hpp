#pragma once

// The Stats page (features/Stats) for QML: the machine's record on
// upstream's five pages - Overview (the connected port's job results and
// times, the recent jobs, upcoming maintenance, the configuration, getting
// help, the latest alarms), Jobs (the history, searchable, with the jobs and
// run time per CNC), Maintenance (the tasks, added, edited, reset, deleted),
// Alarms (the log, the diagnostic file, clearing) and About (the version,
// the team, the release notes). What asks first is asked by the QML.

#include <QObject>
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

class StatsModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    // Overview.
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(int completeJobs READ completeJobs NOTIFY changed)
    Q_PROPERTY(int incompleteJobs READ incompleteJobs NOTIFY changed)
    Q_PROPERTY(QVariantList statRows READ statRows NOTIFY changed)          // {label, value}
    Q_PROPERTY(QVariantList recentJobs READ recentJobs NOTIFY changed)      // {file, duration, complete}
    Q_PROPERTY(QString profile READ profile NOTIFY changed)                 // "Sienci LongMill MK2 30x30"
    Q_PROPERTY(QVariantList configuration READ configuration NOTIFY changed)  // {label, value}
    Q_PROPERTY(QVariantList alarmPreview READ alarmPreview NOTIFY changed)  // {alarm, what, when}
    // Jobs: newest first {file, path, duration, lines, start, complete, search}.
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
    // Per CNC: {labels, values}.
    Q_PROPERTY(QVariantMap jobsPerCnc READ jobsPerCnc NOTIFY changed)
    Q_PROPERTY(QVariantMap runTimePerCnc READ runTimePerCnc NOTIFY changed)
    // Maintenance: in the list's order {id, name, description, rangeStart,
    // rangeEnd, state ("hours"/"due"/"urgent"), hours, search}; the upcoming
    // ones {name, hours, word, color}.
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY changed)
    Q_PROPERTY(QVariantList upcoming READ upcoming NOTIFY changed)  // three
    Q_PROPERTY(QVariantList upcomingMore READ upcomingMore NOTIFY changed)  // six
    // Alarms: newest first {alarm, title, time, message, line}.
    Q_PROPERTY(QVariantList alarms READ alarms NOTIFY changed)
    // About.
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QVariantList releases READ releases CONSTANT)  // {heading, notes: []}

public:
    explicit StatsModel(QObject* parent = nullptr);

    bool connected() const;
    int completeJobs() const { return completeJobs_; }
    int incompleteJobs() const { return incompleteJobs_; }
    QVariantList statRows() const { return statRows_; }
    QVariantList recentJobs() const { return recentJobs_; }
    QString profile() const { return profile_; }
    QVariantList configuration() const { return configuration_; }
    QVariantList alarmPreview() const { return alarmPreview_; }
    QVariantList jobs() const { return jobs_; }
    QVariantMap jobsPerCnc() const { return jobsPerCnc_; }
    QVariantMap runTimePerCnc() const { return runTimePerCnc_; }
    QVariantList tasks() const { return tasks_; }
    QVariantList upcoming() const { return upcoming_; }
    QVariantList upcomingMore() const { return upcomingMore_; }
    QVariantList alarms() const { return alarms_; }
    QString version() const;
    QVariantList releases() const;

    Q_INVOKABLE void reload();
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

Q_SIGNALS:
    void changed();

private:
    app::Machine& machine_;
    QTimer* refresh_;
    int completeJobs_ = 0;
    int incompleteJobs_ = 0;
    QVariantList statRows_;
    QVariantList recentJobs_;
    QString profile_;
    QVariantList configuration_;
    QVariantList alarmPreview_;
    QVariantList jobs_;
    QVariantMap jobsPerCnc_;
    QVariantMap runTimePerCnc_;
    QVariantList tasks_;
    QVariantList upcoming_;
    QVariantList upcomingMore_;
    QVariantList alarms_;
};

}  // namespace gs::ui
