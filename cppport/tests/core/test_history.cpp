// The machine's history in the config file (Stats): jobs
// (api.jobstats.js), maintenance tasks (api.maintenance.js) and alarms and
// errors (api.alarmList.js).

#include "gs/config/config_store.hpp"
#include "gs/config/history.hpp"
#include "gs/config/records.hpp"
#include "gs/controller/actions.hpp"

#include <boost/json.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>

using namespace gs;
using namespace gs::config;
namespace fs = std::filesystem;
namespace json = boost::json;

namespace {

class HistoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir = fs::temp_directory_path() / ("gs_history_test_" + randomUuid());
        fs::create_directories(dir);
        ASSERT_TRUE(store.load(dir / ".sender_rc"));
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    fs::path dir;
    ConfigStore store;
};

constexpr std::int64_t kSeptember23 = 1790145612345;  // 2026-09-23T06:40:12.345Z

}  // namespace

TEST(IsoTime, FormatsAndParsesJavaScriptDates) {
    EXPECT_EQ(isoTime(0), "1970-01-01T00:00:00.000Z");
    EXPECT_EQ(isoTime(kSeptember23), "2026-09-23T06:40:12.345Z");
    EXPECT_EQ(parseIsoTime("2026-09-23T06:40:12.345Z"), kSeptember23);
    EXPECT_EQ(parseIsoTime("2026-09-23T06:40:12Z"), kSeptember23 - 345);
    EXPECT_EQ(parseIsoTime("2026-09-23T06:40:12.3Z"), kSeptember23 - 45);
    EXPECT_FALSE(parseIsoTime("2026-02-30T00:00:00.000Z").has_value());
    EXPECT_FALSE(parseIsoTime("yesterday").has_value());
}

TEST_F(HistoryTest, JobsAreCountedAndKept) {
    JobStatsStore jobs(store);
    EXPECT_EQ(jobs.load().totalJobs, 0);  // the defaults
    JobRecord done;
    done.file = "square.nc";
    done.totalLines = 42;
    done.port = "COM3";
    done.controller = "Grbl";
    done.startTime = kSeptember23;
    done.endTime = kSeptember23 + 60'000;
    done.duration = 60'000;
    done.completed = true;
    jobs.record(done, 55'000);
    JobRecord stopped = done;
    stopped.endTime.reset();
    stopped.completed = false;
    jobs.record(stopped, 5'000);

    const JobStats stats = jobs.load();
    EXPECT_EQ(stats.totalJobs, 2);
    EXPECT_EQ(stats.jobsCompleted, 1);
    EXPECT_EQ(stats.jobsCancelled, 1);
    EXPECT_EQ(stats.totalRuntime, 60'000);
    ASSERT_EQ(stats.jobs.size(), 2u);
    EXPECT_EQ(stats.jobs[0].id, "0");
    EXPECT_EQ(stats.jobs[1].id, "1");
    EXPECT_EQ(stats.jobs[0].file, "square.nc");
    EXPECT_EQ(stats.jobs[0].startTime, kSeptember23);
    EXPECT_EQ(stats.jobs[0].endTime, kSeptember23 + 60'000);
    EXPECT_FALSE(stats.jobs[1].endTime.has_value());
    EXPECT_FALSE(stats.jobs[1].completed);

    // As upstream writes them.
    const json::value storedValue = store.get("jobStats");  // a copy: keep it alive
    const json::object& stored = storedValue.as_object();
    const json::object& first = stored.at("jobs").as_array()[0].as_object();
    EXPECT_EQ(first.at("jobStatus"), "COMPLETE");
    EXPECT_EQ(first.at("type"), "JOB");
    EXPECT_EQ(first.at("startTime"), "2026-09-23T06:40:12.345Z");
    EXPECT_TRUE(stored.at("jobs").as_array()[1].as_object().at("endTime").is_null());
    EXPECT_TRUE(stored.at("totalJobs").is_int64());

    jobs.clear();
    EXPECT_EQ(jobs.load().totalJobs, 0);
    EXPECT_TRUE(jobs.load().jobs.empty());
}

TEST_F(HistoryTest, MaintenanceTasksCountTheHoursRun) {
    MaintenanceStore maintenance(store);
    std::vector<MaintenanceTask> tasks = maintenance.list();
    ASSERT_EQ(tasks.size(), 4u);  // upstream's defaults
    EXPECT_EQ(tasks[0].name, "Clean around your CNC");
    EXPECT_EQ(tasks[0].rangeStart, 15);
    EXPECT_EQ(tasks[3].rangeEnd, 2000);

    maintenance.addRunTime(90 * 60 * 1000);  // a job of 1.5 h
    EXPECT_DOUBLE_EQ(maintenance.list()[1].currentTime, 1.5);
    EXPECT_TRUE(maintenance.markDone(1));
    EXPECT_EQ(maintenance.list()[1].currentTime, 0);
    EXPECT_DOUBLE_EQ(maintenance.list()[2].currentTime, 1.5);

    const MaintenanceTask added = maintenance.add({0, "Oil the rails", "Every week", 5, 10, 0});
    EXPECT_EQ(added.id, 4);
    MaintenanceTask edited = added;
    edited.rangeEnd = 12;
    EXPECT_TRUE(maintenance.update(edited));
    EXPECT_EQ(maintenance.list().back().rangeEnd, 12);
    EXPECT_TRUE(maintenance.remove(0));
    EXPECT_FALSE(maintenance.remove(0));
    EXPECT_EQ(maintenance.list().size(), 4u);

    maintenance.addRunTime(3600 * 1000);
    maintenance.resetAll();
    for (const MaintenanceTask& task : maintenance.list()) {
        EXPECT_EQ(task.currentTime, 0) << task.name;
    }
}

TEST(Maintenance, TasksFallDueWithinTheirRange) {
    MaintenanceTask task{0, "Clean", "", 15, 20, 0};
    EXPECT_EQ(maintenanceDue(task), MaintenanceDue::Low);
    EXPECT_EQ(hoursUntilDue(task), 15);
    task.currentTime = 5.5;
    EXPECT_EQ(maintenanceDue(task), MaintenanceDue::Soon);
    EXPECT_EQ(hoursUntilDue(task), 10);  // floor of the hours run
    task.currentTime = 15;
    EXPECT_EQ(maintenanceDue(task), MaintenanceDue::Due);
    task.currentTime = 20;
    EXPECT_EQ(maintenanceDue(task), MaintenanceDue::Due);
    task.currentTime = 20.1;
    EXPECT_EQ(maintenanceDue(task), MaintenanceDue::Urgent);
}

TEST_F(HistoryTest, AlarmsAndErrorsAreLoggedNewestFirst) {
    AlarmHistory history(store);
    AlarmRecord alarm;
    alarm.alarm = true;
    alarm.code = "1";
    alarm.message = "Hard limit";
    alarm.source = "Jog";
    alarm.time = kSeptember23 - std::int64_t{30} * 24 * 3600 * 1000;  // a month ago
    alarm.controller = "Grbl";
    history.record(alarm);
    AlarmRecord error;
    error.code = "20";
    error.message = "Unsupported command";
    error.source = "square.nc";
    error.line = "G5";
    error.lineNumber = 7;
    error.time = kSeptember23;
    history.record(error);

    const std::vector<AlarmRecord> all = history.list();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].code, "20");  // newest first
    EXPECT_EQ(all[0].id, "1");
    EXPECT_EQ(all[0].lineNumber, 7u);
    EXPECT_FALSE(all[0].alarm);
    EXPECT_TRUE(all[1].alarm);
    EXPECT_EQ(history.recent(kSeptember23).size(), 1u);  // the last 21 days
    const json::value storedValue = store.get("alarmList");
    const json::object& stored = storedValue.as_object().at("list").as_array()[0].as_object();
    EXPECT_EQ(stored.at("CODE"), 1);  // numeric codes stay numbers
    EXPECT_EQ(stored.at("type"), "ALARM");
    history.clear();
    EXPECT_TRUE(history.list().empty());
}

TEST(HomingRequiredAlarm, IsTheConnectPromptNotAFault) {
    using controller::isHomingRequiredAlarm;
    EXPECT_TRUE(isHomingRequiredAlarm(true, "Homing", false));
    EXPECT_TRUE(isHomingRequiredAlarm(true, "Homing", true));
    EXPECT_TRUE(isHomingRequiredAlarm(true, "11", true));
    EXPECT_FALSE(isHomingRequiredAlarm(true, "11", false));  // Grbl's alarms stop at 9
    EXPECT_FALSE(isHomingRequiredAlarm(false, "Homing", false));
    EXPECT_FALSE(isHomingRequiredAlarm(true, "1", false));
}

TEST(StatsPage, JobsAreSummedUpAsCalculateJobStatsDoes) {
    JobRecord a;
    a.port = "COM3";
    a.duration = 60'000;
    a.completed = true;
    JobRecord b = a;
    b.duration = 30'001;
    b.completed = false;
    JobRecord c = a;
    c.port = "/dev/ttyUSB0";
    c.duration = 3'723'000;
    JobRecord d = a;
    d.port = "5";  // an array index: first among an object's keys
    const std::vector<JobRecord> jobs{a, b, c, d};

    const JobResults all = calculateJobStats(jobs);
    EXPECT_EQ(all.completeJobs, 3);
    EXPECT_EQ(all.incompleteJobs, 1);
    EXPECT_EQ(all.totalCutTime, 3'873'001);
    EXPECT_EQ(all.averageCutTime, 968'250);  // 968250.25, toFixed(0)
    EXPECT_EQ(all.longestCutTime, 3'723'000);
    EXPECT_TRUE(std::isnan(calculateJobStats({}).averageCutTime));
    EXPECT_EQ(filterJobsByPort(jobs, "COM3").size(), 2u);
    EXPECT_TRUE(filterJobsByPort(jobs, "COM4").empty());

    EXPECT_EQ(jobsPerPort(jobs), (std::vector<std::pair<std::string, double>>{
                                     {"5", 1}, {"COM3", 2}, {"/dev/ttyUSB0", 1}}));
    EXPECT_EQ(runTimePerPort(jobs), (std::vector<std::pair<std::string, double>>{
                                        {"5", 60'000}, {"COM3", 90'001}, {"/dev/ttyUSB0", 3'723'000}}));
    EXPECT_EQ(truncatePort("/dev/ttyUSB0"), "tyUSB0");
    EXPECT_EQ(truncatePort("COM3"), "COM3");
}

TEST(StatsPage, TimesReadAsUpstreamShowsThem) {
    EXPECT_EQ(statTimeString(3'723'000), "1h 2m 3s");
    EXPECT_EQ(statTimeString(968'250), "0h 16m 8s");
    EXPECT_EQ(statTimeString(59'600), "0h 0m 60s");  // toFixed rounds past the minute
    EXPECT_EQ(statTimeString(0), "-");
    EXPECT_EQ(statTimeString(std::nan("")), "-");
    EXPECT_EQ(previewDuration(3'723'000), "01:02:03");
    EXPECT_EQ(previewDuration(0), "00:00:00");
    EXPECT_EQ(previewDuration(90'000'000), "01:00:00");  // a Date's time of day: 25 h wraps
    EXPECT_EQ(previewDuration(std::nan("")), "-");
}

TEST(StatsPage, MaintenanceIsOrderedByWhatIsPressing) {
    const std::vector<MaintenanceTask> tasks{
        {0, "Low", "", 15, 20, 0},       // 20 h to its end, 15 until due
        {1, "Due", "", 50, 60, 55},      // 5 h
        {2, "Urgent", "", 1, 2, 3},      // -1 h
        {3, "Later", "", 100, 200, 0},   // 200 h, 100 until due
        {4, "Due too", "", 5, 100, 50},  // 50 h
    };
    const auto names = [](const std::vector<MaintenanceTask>& list) {
        std::vector<std::string> out;
        for (const MaintenanceTask& task : list) {
            out.push_back(task.name);
        }
        return out;
    };
    EXPECT_EQ(names(upcomingMaintenance(tasks, 3)), (std::vector<std::string>{"Urgent", "Due", "Low"}));
    EXPECT_EQ(upcomingMaintenance(tasks, 6).size(), 5u);
    EXPECT_EQ(names(maintenanceListOrder(tasks)),
              (std::vector<std::string>{"Urgent", "Due", "Due too", "Low", "Later"}));

    EXPECT_EQ(maintenanceNameProblem(" \t"), "Task name is required");
    EXPECT_EQ(maintenanceNameProblem("Oil the rails"), "");
    EXPECT_EQ(maintenanceRangeProblem(-1, 5), "Start range must be a valid number");
    EXPECT_EQ(maintenanceRangeProblem(1, std::nan("")), "End range must be a valid number");
    EXPECT_EQ(maintenanceRangeProblem(5, 5), "End range must be greater than start range");
    EXPECT_EQ(maintenanceRangeProblem(0, 1), "");
}
