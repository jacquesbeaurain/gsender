// The machine's history in the config file (Stats): jobs
// (api.jobstats.js), maintenance tasks (api.maintenance.js) and alarms and
// errors (api.alarmList.js).

#include "gs/config/config_store.hpp"
#include "gs/config/history.hpp"
#include "gs/config/records.hpp"
#include "gs/controller/actions.hpp"

#include <boost/json.hpp>
#include <gtest/gtest.h>

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
