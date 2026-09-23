#include "gs/config/history.hpp"

#include "gs/util/jsnumber.hpp"

#include <boost/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace gs::config {
namespace {

namespace json = boost::json;
using namespace std::chrono;

constexpr std::int64_t kRecentMs = std::int64_t{21} * 24 * 3600 * 1000;  // fetchRecent(): 21 days

std::string text(const json::object& o, std::string_view key) {
    const json::value* v = o.if_contains(key);
    if (!v) {
        return {};
    }
    if (v->is_string()) {
        return std::string(v->as_string());
    }
    if (v->is_number()) {
        return js::numberToString(v->to_number<double>());
    }
    return {};
}

double number(const json::object& o, std::string_view key, double fallback = 0) {
    const json::value* v = o.if_contains(key);
    return v && v->is_number() ? v->to_number<double>() : fallback;
}

// A Date member: an ISO string (or, leniently, a number of ms).
std::optional<std::int64_t> date(const json::object& o, std::string_view key) {
    const json::value* v = o.if_contains(key);
    if (v && v->is_string()) {
        return parseIsoTime(v->as_string());
    }
    if (v && v->is_number()) {
        return static_cast<std::int64_t>(v->to_number<double>());
    }
    return std::nullopt;
}

json::object objectAt(const ConfigStore& store, std::string_view key) {
    const json::value value = store.get(key, json::object());
    return value.is_object() ? value.as_object() : json::object();
}

json::object defaultJobStats() {
    return {{"totalRuntime", 0}, {"totalJobs", 0}, {"jobsCompleted", 0}, {"jobsCancelled", 0},
            {"jobs", json::array()}};
}

json::value jobJson(const JobRecord& job) {
    return json::object{
        {"id", job.id},
        {"type", "JOB"},
        {"file", job.file},
        {"path", job.path},
        {"totalLines", job.totalLines},
        {"port", job.port},
        {"controller", job.controller},
        {"startTime", isoTime(job.startTime)},
        {"endTime", job.endTime ? json::value(isoTime(*job.endTime)) : json::value(nullptr)},
        {"duration", job.duration},
        {"jobStatus", job.completed ? "COMPLETE" : "STOPPED"},
    };
}

MaintenanceTask taskFrom(const json::object& o) {
    MaintenanceTask t;
    t.id = static_cast<int>(number(o, "id"));
    t.name = text(o, "name");
    t.description = text(o, "description");
    t.rangeStart = number(o, "rangeStart");
    t.rangeEnd = number(o, "rangeEnd");
    t.currentTime = number(o, "currentTime");
    return t;
}

json::value taskJson(const MaintenanceTask& t) {
    return json::object{{"id", t.id},
                        {"name", t.name},
                        {"description", t.description},
                        {"rangeStart", t.rangeStart},
                        {"rangeEnd", t.rangeEnd},
                        {"currentTime", t.currentTime}};
}

bool parseInt(std::string_view text, std::size_t at, std::size_t length, int& out) {
    if (at + length > text.size()) {
        return false;
    }
    const char* first = text.data() + at;
    const auto [end, error] = std::from_chars(first, first + length, out);
    return error == std::errc() && end == first + length;
}

}  // namespace

// ---- dates -------------------------------------------------------------------------------

std::string isoTime(std::int64_t msSinceEpoch) {
    const sys_time<milliseconds> t{milliseconds{msSinceEpoch}};
    const sys_days day = floor<days>(t);
    const year_month_day ymd{day};
    const hh_mm_ss<milliseconds> clock{t - day};
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02uT%02d:%02d:%02d.%03dZ", static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()),
                  static_cast<int>(clock.hours().count()), static_cast<int>(clock.minutes().count()),
                  static_cast<int>(clock.seconds().count()), static_cast<int>(clock.subseconds().count()));
    return buffer;
}

std::optional<std::int64_t> parseIsoTime(std::string_view text) {
    // YYYY-MM-DDTHH:MM:SS[.sss]Z
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0, ms = 0;
    if (!parseInt(text, 0, 4, y) || text.size() < 20 || text[4] != '-' || !parseInt(text, 5, 2, mo) ||
        text[7] != '-' || !parseInt(text, 8, 2, d) || text[10] != 'T' || !parseInt(text, 11, 2, h) ||
        text[13] != ':' || !parseInt(text, 14, 2, mi) || text[16] != ':' || !parseInt(text, 17, 2, s)) {
        return std::nullopt;
    }
    std::size_t at = 19;
    if (at < text.size() && text[at] == '.') {
        std::size_t digits = 0;
        ++at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
            if (digits < 3) {
                ms = ms * 10 + (text[at] - '0');
            }
            ++digits;
            ++at;
        }
        for (; digits < 3; ++digits) {
            ms *= 10;
        }
    }
    if (at >= text.size() || text[at] != 'Z') {
        return std::nullopt;
    }
    const year_month_day ymd{year{y}, month{static_cast<unsigned>(mo)}, day{static_cast<unsigned>(d)}};
    if (!ymd.ok()) {
        return std::nullopt;
    }
    const sys_time<milliseconds> t =
        sys_days{ymd} + hours{h} + minutes{mi} + seconds{s} + milliseconds{ms};
    return t.time_since_epoch().count();
}

// ---- jobs --------------------------------------------------------------------------------

JobStats JobStatsStore::load() const {
    const json::object o = objectAt(store_, "jobStats");
    JobStats stats;
    stats.totalRuntime = number(o, "totalRuntime");
    stats.totalJobs = static_cast<int>(number(o, "totalJobs"));
    stats.jobsCompleted = static_cast<int>(number(o, "jobsCompleted"));
    stats.jobsCancelled = static_cast<int>(number(o, "jobsCancelled"));
    if (const json::value* jobs = o.if_contains("jobs"); jobs && jobs->is_array()) {
        for (const json::value& value : jobs->as_array()) {
            if (!value.is_object()) {
                continue;
            }
            const json::object& j = value.as_object();
            JobRecord job;
            job.id = text(j, "id");
            job.file = text(j, "file");
            job.path = text(j, "path");
            job.totalLines = static_cast<std::size_t>(std::max(0.0, number(j, "totalLines")));
            job.port = text(j, "port");
            job.controller = text(j, "controller");
            job.startTime = date(j, "startTime").value_or(0);
            job.endTime = date(j, "endTime");
            job.duration = static_cast<std::int64_t>(number(j, "duration"));
            job.completed = text(j, "jobStatus") == "COMPLETE";
            stats.jobs.push_back(std::move(job));
        }
    }
    return stats;
}

void JobStatsStore::record(JobRecord job, std::int64_t timeRunningMs) {
    json::object o = objectAt(store_, "jobStats");
    if (!o.if_contains("jobs") || !o.at("jobs").is_array()) {
        o["jobs"] = json::array();
    }
    json::array& jobs = o["jobs"].as_array();
    job.id = std::to_string(jobs.size());
    // Whole numbers stay integers in the file (Boost.JSON writes doubles as 3E0).
    const auto whole = [&o](std::string_view key, double add) {
        o[key] = static_cast<std::int64_t>(std::llround(number(o, key) + add));
    };
    whole(job.completed ? "jobsCompleted" : "jobsCancelled", 1);
    whole("totalJobs", 1);
    whole("totalRuntime", static_cast<double>(timeRunningMs));
    jobs.push_back(jobJson(job));
    store_.set("jobStats", std::move(o));
}

void JobStatsStore::clear() {
    store_.set("jobStats", defaultJobStats());
}

// ---- maintenance ---------------------------------------------------------------------------

MaintenanceDue maintenanceDue(const MaintenanceTask& task) {
    if (task.currentTime > task.rangeEnd) {
        return MaintenanceDue::Urgent;
    }
    if (task.currentTime >= task.rangeStart) {
        return MaintenanceDue::Due;
    }
    return task.currentTime >= task.rangeStart - 10 ? MaintenanceDue::Soon : MaintenanceDue::Low;
}

double hoursUntilDue(const MaintenanceTask& task) {
    return task.rangeStart - std::floor(task.currentTime);
}

std::vector<MaintenanceTask> MaintenanceStore::list() const {
    std::vector<MaintenanceTask> tasks;
    const json::value value = store_.get("maintenance", json::array());
    if (value.is_array()) {
        for (const json::value& task : value.as_array()) {
            if (task.is_object()) {
                tasks.push_back(taskFrom(task.as_object()));
            }
        }
    }
    return tasks;
}

void MaintenanceStore::save(const std::vector<MaintenanceTask>& tasks) {
    json::array out;
    for (const MaintenanceTask& task : tasks) {
        out.push_back(taskJson(task));
    }
    store_.set("maintenance", std::move(out));
}

void MaintenanceStore::addRunTime(std::int64_t timeRunningMs) {
    std::vector<MaintenanceTask> tasks = list();
    for (MaintenanceTask& task : tasks) {
        task.currentTime += static_cast<double>(timeRunningMs) / 1000 / 3600;
    }
    save(tasks);
}

MaintenanceTask MaintenanceStore::add(MaintenanceTask task) {
    std::vector<MaintenanceTask> tasks = list();
    int next = 0;
    for (const MaintenanceTask& existing : tasks) {
        next = std::max(next, existing.id + 1);
    }
    task.id = next;
    tasks.push_back(task);
    save(tasks);
    return task;
}

bool MaintenanceStore::update(const MaintenanceTask& task) {
    std::vector<MaintenanceTask> tasks = list();
    for (MaintenanceTask& existing : tasks) {
        if (existing.id == task.id) {
            existing = task;
            save(tasks);
            return true;
        }
    }
    return false;
}

bool MaintenanceStore::remove(int id) {
    std::vector<MaintenanceTask> tasks = list();
    const auto gone = std::remove_if(tasks.begin(), tasks.end(), [id](const auto& t) { return t.id == id; });
    if (gone == tasks.end()) {
        return false;
    }
    tasks.erase(gone, tasks.end());
    save(tasks);
    return true;
}

bool MaintenanceStore::markDone(int id) {
    std::vector<MaintenanceTask> tasks = list();
    for (MaintenanceTask& task : tasks) {
        if (task.id == id) {
            task.currentTime = 0;
            save(tasks);
            return true;
        }
    }
    return false;
}

// ---- alarms and errors ------------------------------------------------------------------------

std::vector<AlarmRecord> AlarmHistory::list() const {
    std::vector<AlarmRecord> records;
    const json::object o = objectAt(store_, "alarmList");
    if (const json::value* list = o.if_contains("list"); list && list->is_array()) {
        for (const json::value& value : list->as_array()) {
            if (!value.is_object()) {
                continue;
            }
            const json::object& a = value.as_object();
            AlarmRecord r;
            r.id = text(a, "id");
            r.alarm = text(a, "type") == "ALARM";
            r.source = text(a, "source");
            r.time = date(a, "time").value_or(0);
            r.code = text(a, "CODE");
            r.message = text(a, "MESSAGE");
            if (const json::value* n = a.if_contains("lineNumber"); n && n->is_number()) {
                r.lineNumber = static_cast<std::size_t>(std::max(0.0, n->to_number<double>()));
            }
            r.line = text(a, "line");
            r.controller = text(a, "controller");
            records.push_back(std::move(r));
        }
    }
    // Newest first, as upstream sorts by date.
    std::stable_sort(records.begin(), records.end(), [](const AlarmRecord& a, const AlarmRecord& b) {
        return a.time > b.time;
    });
    return records;
}

std::vector<AlarmRecord> AlarmHistory::recent(std::int64_t nowMs) const {
    std::vector<AlarmRecord> records = list();
    std::erase_if(records, [nowMs](const AlarmRecord& r) { return r.time <= nowMs - kRecentMs; });
    return records;
}

void AlarmHistory::record(AlarmRecord r) {
    json::object o = objectAt(store_, "alarmList");
    if (!o.if_contains("list") || !o.at("list").is_array()) {
        o["list"] = json::array();
    }
    json::array& list = o["list"].as_array();
    r.id = std::to_string(list.size());
    // Numeric codes stay numbers, as upstream's error events carry them.
    const double numeric = js::stringToNumber(r.code);
    json::value code = json::string(r.code);
    if (!r.code.empty() && std::isfinite(numeric)) {
        code = numeric == std::trunc(numeric) ? json::value(static_cast<std::int64_t>(numeric)) : json::value(numeric);
    }
    list.push_back(json::object{
        {"id", r.id},
        {"type", r.alarm ? "ALARM" : "ERROR"},
        {"source", r.source},
        {"time", isoTime(r.time)},
        {"CODE", code},
        {"MESSAGE", r.message},
        {"lineNumber", r.lineNumber ? json::value(*r.lineNumber) : json::value(nullptr)},
        {"line", r.line},
        {"controller", r.controller},
    });
    store_.set("alarmList", std::move(o));
}

void AlarmHistory::clear() {
    json::object o = objectAt(store_, "alarmList");
    o["list"] = json::array();
    store_.set("alarmList", std::move(o));
}

}  // namespace gs::config
