#pragma once

// The machine's history in the config file, as gSender's Stats page keeps
// it: job statistics ("jobStats", upstream's api.jobstats.js and
// controllerSagas' updateJobStats), maintenance tasks with the hours they
// have run ("maintenance", api.maintenance.js / updateMaintenanceTasks) and
// the alarm and error log ("alarmList", api.alarmList.js /
// updateAlarmsErrors). The JSON matches upstream's records; dates are ISO
// 8601 strings (JSON-serialized JavaScript Dates).

#include "gs/config/config_store.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::config {

// A JavaScript Date as JSON: "2026-09-23T06:40:12.345Z" (UTC).
std::string isoTime(std::int64_t msSinceEpoch);
std::optional<std::int64_t> parseIsoTime(std::string_view text);

// ---- jobs ----

struct JobRecord {
    std::string id;           // the number of jobs recorded before it
    std::string file;         // the program's name
    std::string path;
    std::size_t totalLines = 0;
    std::string port;
    std::string controller;   // "Grbl" / "grblHAL"
    std::int64_t startTime = 0;           // ms since the epoch
    std::optional<std::int64_t> endTime;  // none for a stopped job
    std::int64_t duration = 0;            // ms, the job's elapsed time
    bool completed = false;               // jobStatus COMPLETE, else STOPPED
};

struct JobStats {
    double totalRuntime = 0;  // ms spent running (paused time excluded)
    int totalJobs = 0;
    int jobsCompleted = 0;
    int jobsCancelled = 0;
    std::vector<JobRecord> jobs;  // oldest first
};

class JobStatsStore {
public:
    explicit JobStatsStore(ConfigStore& store) : store_(store) {}
    JobStats load() const;
    // A job has ended: counted as completed or cancelled, its running time
    // added, its record kept (the id assigned here).
    void record(JobRecord job, std::int64_t timeRunningMs);
    void clear();

private:
    ConfigStore& store_;
};

// ---- maintenance ----

struct MaintenanceTask {
    int id = 0;
    std::string name;
    std::string description;
    double rangeStart = 0;   // hours: due from...
    double rangeEnd = 0;     // ...to
    double currentTime = 0;  // hours run since last done
};

// How pressing a task is (MaintenancePreview's remainingTimeString): Low,
// Soon (within 10 h of its range), Due (within it), Urgent (past it).
enum class MaintenanceDue { Low, Soon, Due, Urgent };
MaintenanceDue maintenanceDue(const MaintenanceTask& task);
// Hours until the range starts: rangeStart - floor(currentTime).
double hoursUntilDue(const MaintenanceTask& task);

class MaintenanceStore {
public:
    explicit MaintenanceStore(ConfigStore& store) : store_(store) {}
    std::vector<MaintenanceTask> list() const;
    void save(const std::vector<MaintenanceTask>& tasks);
    // updateMaintenanceTasks(): every task has run a job's hours longer.
    void addRunTime(std::int64_t timeRunningMs);
    // A new task gets the next id.
    MaintenanceTask add(MaintenanceTask task);
    bool update(const MaintenanceTask& task);
    bool remove(int id);
    bool markDone(int id);  // its hours start again from 0
    void resetAll();        // MaintenanceList's Reset All: every task's hours

private:
    ConfigStore& store_;
};

// ---- alarms and errors ----

struct AlarmRecord {
    std::string id;         // the list's size when recorded
    bool alarm = false;     // type ALARM, else ERROR
    std::string source;     // where the offending line came from ("Console", a job...)
    std::int64_t time = 0;  // ms since the epoch
    std::string code;
    std::string message;
    std::optional<std::size_t> lineNumber;
    std::string line;
    std::string controller;
};

class AlarmHistory {
public:
    explicit AlarmHistory(ConfigStore& store) : store_(store) {}
    std::vector<AlarmRecord> list() const;  // newest first
    // fetchRecent(): those of the last 21 days.
    std::vector<AlarmRecord> recent(std::int64_t nowMs) const;
    void record(AlarmRecord record);
    void clear();

private:
    ConfigStore& store_;
};

// ---- the Stats page's summaries (features/Stats) ----

// calculateJobStats(): results and cutting times (ms) of some jobs. The
// average is NaN without jobs (0 / 0), as upstream's.
struct JobResults {
    int completeJobs = 0;
    int incompleteJobs = 0;
    double totalCutTime = 0;
    double averageCutTime = 0;
    double longestCutTime = 0;
};
JobResults calculateJobStats(const std::vector<JobRecord>& jobs);
std::vector<JobRecord> filterJobsByPort(const std::vector<JobRecord>& jobs, std::string_view port);
// truncatePort(): a port's last six characters, the charts' labels.
std::string truncatePort(std::string_view port);
// StatTable's getTimeString(): "1h 2m 3s"; "-" for none.
std::string statTimeString(double ms);
// JobPreview's formatDuration(): a Date's ISO time of day, "HH:MM:SS".
std::string previewDuration(double ms);
// JobsPerComPort and RunTimePerComPort: the jobs and their milliseconds per
// port, the ports in the order the jobs meet them.
std::vector<std::pair<std::string, double>> jobsPerPort(const std::vector<JobRecord>& jobs);
std::vector<std::pair<std::string, double>> runTimePerPort(const std::vector<JobRecord>& jobs);

// MaintenancePreview: the `limit` tasks with the fewest hours to the end of
// their range.
std::vector<MaintenanceTask> upcomingMaintenance(std::vector<MaintenanceTask> tasks, std::size_t limit);
// MaintenanceList's order (its time column sorted): the urgent tasks, the
// due ones, then the fewest hours until due.
std::vector<MaintenanceTask> maintenanceListOrder(std::vector<MaintenanceTask> tasks);
// MaintenanceTaskForm's checks: the message, empty when fine.
std::string maintenanceNameProblem(std::string_view name);
std::string maintenanceRangeProblem(double rangeStart, double rangeEnd);

}  // namespace gs::config
