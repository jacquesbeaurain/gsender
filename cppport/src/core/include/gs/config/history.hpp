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

}  // namespace gs::config
