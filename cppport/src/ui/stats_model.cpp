#include "stats_model.hpp"

#include "backend.hpp"
#include "diagnostics.hpp"
#include "machine.hpp"

#include "gs/config/history.hpp"
#include "gs/config/machine_profiles.hpp"
#include "gs/controller/controller.hpp"
#include "gs/controller/locations.hpp"
#include "gs/transport/asio_link.hpp"
#include "gs/util/datetime.hpp"
#include "gs/util/jsnumber.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

#include <cmath>

namespace gs::ui {
namespace {

QString qs(const std::string& text) {
    return QString::fromStdString(text);
}

QString number(double value) {
    return qs(js::numberToString(value));
}

// A Date's toLocaleString('en-US'): "9/23/2026, 2:40:12 AM".
QString enUsDateTime(std::int64_t ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).toString("M/d/yyyy, h:mm:ss AP");
}

QString localPath(const QString& file) {
    const QUrl url(file);
    return url.isLocalFile() ? url.toLocalFile() : file;
}

QVariantMap chart(const std::vector<std::pair<std::string, double>>& perPort) {
    QStringList labels;
    QVariantList values;
    for (const auto& [port, value] : perPort) {
        labels << qs(config::truncatePort(port));
        values << value;
    }
    return {{"labels", labels}, {"values", values}};
}

// MaintenancePreview: the upcoming tasks with their reminder.
std::vector<StatsModel::UpcomingRow> preview(const std::vector<config::MaintenanceTask>& tasks, std::size_t limit) {
    std::vector<StatsModel::UpcomingRow> list;
    for (const config::MaintenanceTask& task : config::upcomingMaintenance(tasks, limit)) {
        QString word;
        QString color;
        switch (config::maintenanceDue(task)) {
            case config::MaintenanceDue::Urgent: word = QObject::tr("Urgent!"); color = "#dc2626"; break;
            case config::MaintenanceDue::Due: word = QObject::tr("Due"); color = "#bb6a0c"; break;
            case config::MaintenanceDue::Soon: word = QObject::tr("Soon"); color = "#689AC9"; break;
            case config::MaintenanceDue::Low: word = QObject::tr("Low"); color = "#059669"; break;
        }
        list.push_back({qs(task.name), QObject::tr("%1 hrs").arg(number(config::hoursUntilDue(task))), word, color});
    }
    return list;
}

}  // namespace

StatsModel::StatsModel(QObject* parent) : UiModelBase(parent) {
    statRowsModel_.setRoles({
        {Qt::UserRole + 1, "label", [](const StatRow& r) { return r.label; }},
        {Qt::UserRole + 2, "value", [](const StatRow& r) { return r.value; }},
    });
    recentJobsModel_.setRoles({
        {Qt::UserRole + 1, "file", [](const RecentJobRow& r) { return r.file; }},
        {Qt::UserRole + 2, "duration", [](const RecentJobRow& r) { return r.duration; }},
        {Qt::UserRole + 3, "complete", [](const RecentJobRow& r) { return r.complete; }},
    });
    configurationModel_.setRoles({
        {Qt::UserRole + 1, "label", [](const ConfigRow& r) { return r.label; }},
        {Qt::UserRole + 2, "value", [](const ConfigRow& r) { return r.value; }},
    });
    alarmPreviewModel_.setRoles({
        {Qt::UserRole + 1, "alarm", [](const AlarmPreviewRow& r) { return r.alarm; }},
        {Qt::UserRole + 2, "what", [](const AlarmPreviewRow& r) { return r.what; }},
        {Qt::UserRole + 3, "when", [](const AlarmPreviewRow& r) { return r.when; }},
    });
    jobsModel_.setRoles({
        {Qt::UserRole + 1, "file", [](const JobRow& r) { return r.file; }},
        {Qt::UserRole + 2, "path", [](const JobRow& r) { return r.path; }},
        {Qt::UserRole + 3, "duration", [](const JobRow& r) { return r.duration; }},
        {Qt::UserRole + 4, "durationMs", [](const JobRow& r) { return r.durationMs; }},
        {Qt::UserRole + 5, "lines", [](const JobRow& r) { return r.lines; }},
        {Qt::UserRole + 6, "start", [](const JobRow& r) { return r.start; }},
        {Qt::UserRole + 7, "complete", [](const JobRow& r) { return r.complete; }},
        {Qt::UserRole + 8, "search", [](const JobRow& r) { return r.search; }},
    });
    tasksModel_.setRoles({
        {Qt::UserRole + 1, "id", [](const TaskRow& r) { return r.id; }},
        {Qt::UserRole + 2, "name", [](const TaskRow& r) { return r.name; }},
        {Qt::UserRole + 3, "description", [](const TaskRow& r) { return r.description; }},
        {Qt::UserRole + 4, "state", [](const TaskRow& r) { return r.state; }},
        {Qt::UserRole + 5, "hours", [](const TaskRow& r) { return r.hours; }},
        {Qt::UserRole + 6, "search", [](const TaskRow& r) { return r.search; }},
    });
    const auto upcomingRoles = std::vector<StructListModel<UpcomingRow>::Role>{
        {Qt::UserRole + 1, "name", [](const UpcomingRow& r) { return r.name; }},
        {Qt::UserRole + 2, "hours", [](const UpcomingRow& r) { return r.hours; }},
        {Qt::UserRole + 3, "word", [](const UpcomingRow& r) { return r.word; }},
        {Qt::UserRole + 4, "color", [](const UpcomingRow& r) { return r.color; }},
    };
    upcomingModel_.setRoles(upcomingRoles);
    upcomingMoreModel_.setRoles(upcomingRoles);
    alarmsModel_.setRoles({
        {Qt::UserRole + 1, "alarm", [](const AlarmRow& r) { return r.alarm; }},
        {Qt::UserRole + 2, "title", [](const AlarmRow& r) { return r.title; }},
        {Qt::UserRole + 3, "time", [](const AlarmRow& r) { return r.time; }},
        {Qt::UserRole + 4, "message", [](const AlarmRow& r) { return r.message; }},
        {Qt::UserRole + 5, "line", [](const AlarmRow& r) { return r.line; }},
    });

    // Bursts (a connection's settings, a job's end) are gathered into one.
    refresh_ = new QTimer(this);
    refresh_->setSingleShot(true);
    refresh_->setInterval(0);
    connect(refresh_, &QTimer::timeout, this, &StatsModel::reload);
    for (const auto signal : {&app::Machine::historyChanged, &app::Machine::connectionChanged,
                              &app::Machine::settingsChanged, &app::Machine::appSettingsChanged}) {
        connect(&machine_, signal, refresh_, qOverload<>(&QTimer::start));
    }
    reload();
}

QVariantList StatsModel::statRows() const { return statRowsModel_.toVariantList(); }
QVariantList StatsModel::recentJobs() const { return recentJobsModel_.toVariantList(); }
QVariantList StatsModel::configuration() const { return configurationModel_.toVariantList(); }
QVariantList StatsModel::alarmPreview() const { return alarmPreviewModel_.toVariantList(); }
QVariantList StatsModel::jobs() const { return jobsModel_.toVariantList(); }
QVariantList StatsModel::tasks() const { return tasksModel_.toVariantList(); }
QVariantList StatsModel::upcoming() const { return upcomingModel_.toVariantList(); }
QVariantList StatsModel::upcomingMore() const { return upcomingMoreModel_.toVariantList(); }
QVariantList StatsModel::alarms() const { return alarmsModel_.toVariantList(); }

void StatsModel::reload() {
    config::ConfigStore& store = machine_.config();
    const config::JobStats stats = config::JobStatsStore(store).load();
    const std::vector<config::MaintenanceTask> tasks = config::MaintenanceStore(store).list();
    const std::vector<config::AlarmRecord> alarms = config::AlarmHistory(store).list();
    controller::Controller* c = machine_.controller();
    const bool isConnected = connected();
    const std::string port = machine_.port().toStdString();

    // The jobs run on the connected port (StatsProvider's filteredJobs).
    const std::vector<config::JobRecord> portJobs =
        isConnected ? config::filterJobsByPort(stats.jobs, port) : std::vector<config::JobRecord>{};
    const config::JobResults results = config::calculateJobStats(portJobs);
    completeJobs_ = results.completeJobs;
    incompleteJobs_ = results.incompleteJobs;
    const auto row = [isConnected](const QString& label, const QString& value) -> ConfigRow {
        return {label, isConnected ? value : QStringLiteral("-")};
    };
    const auto statRow = [isConnected](const QString& label, const QString& value) -> StatRow {
        return {label, isConnected ? value : QStringLiteral("-")};
    };
    statRowsModel_.reset({
        statRow(tr("Total jobs run"), QString::number(results.completeJobs + results.incompleteJobs)),
        statRow(tr("Total cutting time"), qs(config::statTimeString(results.totalCutTime))),
        statRow(tr("Average job time"), qs(config::statTimeString(results.averageCutTime))),
        statRow(tr("Longest job"), qs(config::statTimeString(results.longestCutTime))),
    });

    // Recent Jobs: the last five, whatever the port.
    std::vector<RecentJobRow> recentJobs;
    int shown = 0;
    for (auto it = stats.jobs.rbegin(); it != stats.jobs.rend() && shown < 5; ++it, ++shown) {
        recentJobs.push_back({qs(it->file), qs(config::previewDuration(static_cast<double>(it->duration))), it->completed});
    }
    recentJobsModel_.reset(std::move(recentJobs));

    // Configuration.
    const config::MachineProfile& machineProfile = machine_.machineProfile();
    profile_ = QString("%1 %2 %3").arg(qs(machineProfile.company), qs(machineProfile.name), qs(machineProfile.type)).trimmed();
    const auto setting = [c](const char* key) { return c ? c->runner().setting(key) : std::string(); };
    const auto enabled = [](bool on) { return on ? tr("Enabled") : tr("Disabled"); };
    const QString connection = transport::looksLikeIpAddress(port)
                                   ? qs(port)
                                   : tr("%1 at %2 baud").arg(qs(config::truncatePort(port))).arg(machine_.baudRate());
    QString axes;
    if (c) {
        const std::string letters = c->runner().state().axes.letters;
        QStringList list;
        for (const char letter : letters.empty() ? std::string("XYZ") : letters) {
            list << QString(QChar(letter));
        }
        axes = list.join(", ");
    }
    configurationModel_.reset({
        row(tr("Connection"), connection),
        row(tr("Axes"), axes),
        row(tr("Soft limits"), enabled(setting("$20") == "1")),
        row(tr("Homing"), enabled(js::stringToNumber(setting("$22")) > 0)),
        row(tr("Home location"), qs(controller::homingString(setting("$23")))),
        row(tr("Report inches"), enabled(setting("$13") == "1")),
    });

    // Alarms & Errors: the latest four.
    std::vector<AlarmPreviewRow> alarmPreview;
    for (std::size_t i = 0; i < alarms.size() && i < 4; ++i) {
        const config::AlarmRecord& alarm = alarms[i];
        alarmPreview.push_back({alarm.alarm,
                                QString("%1 %2").arg(alarm.alarm ? "ALARM" : "ERROR", qs(alarm.code)),
                                tr("on %1").arg(qs(config::isoTime(alarm.time)))});
    }
    alarmPreviewModel_.reset(std::move(alarmPreview));

    // Jobs, newest first.
    std::vector<JobRow> jobList;
    for (auto it = stats.jobs.rbegin(); it != stats.jobs.rend(); ++it) {
        const config::JobRecord& job = *it;
        // What the search looks through: the records' values (includesString).
        const QString search = QStringList{qs(job.file), number(static_cast<double>(job.duration)),
                                           QString::number(job.totalLines), qs(config::isoTime(job.startTime)),
                                           job.completed ? "COMPLETE" : "STOPPED"}
                                   .join('\n')
                                   .toLower();
        jobList.push_back({
            qs(job.file),
            qs(job.path),
            qs(util::millisecondsToTimeStamp(static_cast<double>(job.duration))),
            static_cast<double>(job.duration),
            static_cast<int>(job.totalLines),
            enUsDateTime(job.startTime),
            job.completed,
            search,
        });
    }
    jobsModel_.reset(std::move(jobList));

    // Per CNC, the ports as the newest jobs first meet them.
    const std::vector<config::JobRecord> newestFirst(stats.jobs.rbegin(), stats.jobs.rend());
    jobsPerCnc_ = chart(config::jobsPerPort(newestFirst));
    runTimePerCnc_ = chart(config::runTimePerPort(newestFirst));

    // Maintenance.
    std::vector<TaskRow> taskRows;
    for (const config::MaintenanceTask& task : config::maintenanceListOrder(tasks)) {
        // determineTime(): the hours until due, "Due", or urgent.
        QString state = "urgent";
        QString hours;
        if (task.currentTime < task.rangeStart) {
            state = "hours";
            hours = number(config::hoursUntilDue(task));
        } else if (task.currentTime <= task.rangeEnd) {
            state = "due";
        }
        taskRows.push_back({
            task.id,
            qs(task.name),
            qs(task.description),
            state,
            hours,
            QStringList{state == "due" ? tr("Due") : hours, qs(task.name), qs(task.description)}
                .join('\n')
                .toLower(),
        });
    }
    tasksModel_.reset(std::move(taskRows));
    upcomingModel_.reset(preview(tasks, 3));
    upcomingMoreModel_.reset(preview(tasks, 6));

    // Alarms, newest first.
    std::vector<AlarmRow> alarmRows;
    for (const config::AlarmRecord& alarm : alarms) {
        alarmRows.push_back({
            alarm.alarm,
            QString("%1 %2 - %3").arg(alarm.alarm ? "ALARM" : "ERROR", qs(alarm.code), qs(alarm.source)),
            tr("at %1").arg(enUsDateTime(alarm.time)),
            alarm.message.empty() ? tr("No associated message") : qs(alarm.message),
            qs(alarm.line),
        });
    }
    alarmsModel_.reset(std::move(alarmRows));
    Q_EMIT changed();
}

QString StatsModel::version() const {
    return tr("Version %1 (the C++ port)").arg(QCoreApplication::applicationVersion());
}

QVariantList StatsModel::releases() const {
    QFile file(":/about/notes.json");
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QVariantList list;
    for (const QJsonValue& value : QJsonDocument::fromJson(file.readAll()).array()) {
        const QJsonObject release = value.toObject();
        QStringList notes;
        for (const QJsonValue& note : release.value("notes").toArray()) {
            notes << note.toString();
        }
        list.append(QVariantMap{
            {"heading", release.value("version").toString() + " (" + release.value("date").toString() + ")"},
            {"notes", notes}});
    }
    return list;
}

void StatsModel::clearJobHistory() {
    config::JobStatsStore(machine_.config()).clear();
    reload();
}

void StatsModel::clearAlarms() {
    config::AlarmHistory(machine_.config()).clear();
    reload();
}

void StatsModel::resetTask(int id) {
    config::MaintenanceStore(machine_.config()).markDone(id);
    reload();
}

void StatsModel::resetAllTasks() {
    config::MaintenanceStore(machine_.config()).resetAll();
    reload();
}

QString StatsModel::nameProblem(const QString& name) const {
    return qs(config::maintenanceNameProblem(name.toStdString()));
}

QString StatsModel::rangeProblem(const QString& start, const QString& end) const {
    bool okStart = false;
    bool okEnd = false;
    const double first = start.toDouble(&okStart);
    const double last = end.toDouble(&okEnd);
    if (!okStart || !okEnd) {
        return tr("Enter the hours as numbers");
    }
    return qs(config::maintenanceRangeProblem(first, last));
}

bool StatsModel::saveTask(int id, const QString& name, const QString& start, const QString& end,
                          const QString& description) {
    if (!nameProblem(name).isEmpty() || !rangeProblem(start, end).isEmpty()) {
        return false;
    }
    config::MaintenanceStore store(machine_.config());
    config::MaintenanceTask task;
    if (id >= 0) {
        for (const config::MaintenanceTask& existing : store.list()) {
            if (existing.id == id) {
                task = existing;  // its id and hours kept
            }
        }
    }
    task.name = name.trimmed().toStdString();
    task.rangeStart = start.toDouble();
    task.rangeEnd = end.toDouble();
    task.description = description.toStdString();
    if (id >= 0) {
        store.update(task);
    } else {
        store.add(task);
    }
    reload();
    return true;
}

void StatsModel::deleteTask(int id) {
    config::MaintenanceStore(machine_.config()).remove(id);
    reload();
}

QVariantMap StatsModel::task(int id) const {
    for (const config::MaintenanceTask& task : config::MaintenanceStore(machine_.config()).list()) {
        if (task.id == id) {
            return {{"id", task.id},
                    {"name", qs(task.name)},
                    {"rangeStart", number(task.rangeStart)},
                    {"rangeEnd", number(task.rangeEnd)},
                    {"description", qs(task.description)}};
        }
    }
    return {};
}

QString StatsModel::writeDiagnostics(const QString& file) {
    // The console's typed commands, as the widget window gives them.
    QString error;
    if (app::writeDiagnostics(machine_, machine_.consoleLog().inputHistory(), localPath(file), &error)) {
        return {};
    }
    return error.isEmpty() ? tr("Failed to generate diagnostic file") : error;
}

QString StatsModel::diagnosticsName() const {
    return QDir::home().filePath("diagnostics_" + app::diagnosticsStamp(QDateTime::currentDateTime()) + ".zip");
}

}  // namespace gs::ui
