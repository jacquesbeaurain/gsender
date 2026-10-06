// The phone's tool pages: the desktop's own view models, bound into the
// RemoteService and used through its allow-list (what a pendant sends is
// applied here by RemoteService::handle, as the WebSocket would).

#include "ui_test.hpp"

#include "remote_service.hpp"

#include "gs/remote/pendant.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QJsonObject object(const std::string& json) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
}

}  // namespace

class RemoteModelsTest : public UiTest {
protected:
    void send(const std::string& json) {
        const auto command = remote::parseCommand(json);
        ASSERT_TRUE(command.has_value()) << json;
        backend_->remote().handle(*command, 1);
    }
    QJsonObject properties(const std::string& model) { return object(backend_->remote().modelProperties(model)); }
};

TEST_F(RemoteModelsTest, EveryToolPageHasItsModel) {
    for (const char* name : {"macros", "spindle", "coolant", "probe", "rotary", "config", "connection", "dro", "file", "job",
                             "status", "notifications"}) {
        EXPECT_FALSE(properties(name).isEmpty()) << name;
    }
    EXPECT_TRUE(properties("macros").contains("column1"));
    EXPECT_TRUE(properties("spindle").contains("speedMax"));
    EXPECT_TRUE(properties("probe").contains("commands"));
    EXPECT_TRUE(properties("config").contains("rows"));
    EXPECT_FALSE(properties("config")["rows"].toArray().isEmpty());
    EXPECT_TRUE(backend_->remote().modelProperties("nothing").empty());
}

TEST_F(RemoteModelsTest, ThePhoneRunsCoolantAndTheSpindleLikeTheScreen) {
    connectSimulator();
    const auto coolant = [&] { return machine_->controller()->state().parserState.modal.coolant; };
    send(R"({"type":"call","model":"coolant","method":"mist"})");
    ASSERT_TRUE(waitFor([&] { return !coolant().empty() && coolant().front() == "M7"; }));
    ASSERT_TRUE(waitFor([&] { return properties("coolant")["mistActive"].toBool(); }));
    send(R"({"type":"call","model":"coolant","method":"off"})");
    ASSERT_TRUE(waitFor([&] { return !properties("coolant")["mistActive"].toBool(); }));

    const auto spindle = [&] { return machine_->controller()->state().parserState.modal.spindle; };
    send(R"({"type":"call","model":"spindle","method":"startClockwise"})");
    ASSERT_TRUE(waitFor([&] { return spindle() == "M3"; }));
    ASSERT_TRUE(waitFor([&] { return properties("spindle")["forward"].toBool(); }));
    send(R"({"type":"call","model":"spindle","method":"stop"})");
    ASSERT_TRUE(waitFor([&] { return spindle() == "M5"; }));
}

TEST_F(RemoteModelsTest, OnlyTheListedMembersAreReachable) {
    connectSimulator();
    // Editing a macro, or importing a file, is not on the phone's list.
    send(R"({"type":"call","model":"macros","method":"add","args":["x","G0 X1",""],"id":1})");
    send(R"({"type":"call","model":"macros","method":"importFile","args":["C:/x.json"],"id":2})");
    send(R"({"type":"call","model":"config","method":"exportSettings","args":["C:/x.json"],"id":3})");
    EXPECT_EQ(machine_->macros().list().size(), 0u);
    // Nor are properties outside the writable ones.
    send(R"({"type":"set","model":"config","property":"pendingChanges","value":5})");
    EXPECT_EQ(properties("config")["pendingChanges"].toInt(), 0);
}

TEST_F(RemoteModelsTest, TheConfigPageEditsAndAppliesSettings) {
    // A search narrows the rows the phone is sent.
    const int all = properties("config")["rows"].toArray().size();
    send(R"({"type":"set","model":"config","property":"search","value":"notification"})");
    const int found = properties("config")["rows"].toArray().size();
    EXPECT_LT(found, all);
    EXPECT_GT(found, 0);
    send(R"({"type":"set","model":"config","property":"search","value":""})");
    EXPECT_EQ(properties("config")["rows"].toArray().size(), all);

    // A staged edit counts as pending until applied.
    EXPECT_EQ(properties("config")["pendingChanges"].toInt(), 0);
    send(R"({"type":"call","model":"config","method":"setValue","args":["toastDuration",7000]})");
    EXPECT_EQ(properties("config")["pendingChanges"].toInt(), 1);
    send(R"({"type":"call","model":"config","method":"revert"})");
    EXPECT_EQ(properties("config")["pendingChanges"].toInt(), 0);
}

TEST_F(RemoteModelsTest, ThePhoneLoadsAFileAndRunsTheJobControls) {
    connectSimulator();
    // A file chosen on the phone becomes the job (as File > Load does); a job
    // on its way keeps it from being swapped.
    send(R"({"type":"loadProgram","name":"phone.nc","content":"G21 G90\nG1 X10 F600\nG1 Y10\n"})");
    ASSERT_TRUE(waitFor([&] { return machine_->hasProgram() && !machine_->isAnalyzing(); }));
    EXPECT_EQ(machine_->programName().toStdString(), "phone.nc");
    ASSERT_TRUE(waitFor([&] { return properties("file")["loaded"].toBool(); }));
    EXPECT_EQ(properties("file")["lines"].toInt(), 3);
    EXPECT_TRUE(properties("job")["canStart"].toBool());

    // The overrides, as the job card sets them.
    send(R"({"type":"call","model":"job","method":"setFeedOverride","args":[150]})");
    ASSERT_TRUE(waitFor([&] { return properties("job")["feedOverride"].toInt() == 150; }));

    // Closing it, but not loading one named by path.
    send(R"({"type":"call","model":"file","method":"load","args":["C:/other.nc"],"id":1})");
    EXPECT_EQ(machine_->programName().toStdString(), "phone.nc");
    send(R"({"type":"call","model":"file","method":"close"})");
    ASSERT_TRUE(waitFor([&] { return !machine_->hasProgram(); }));
}

TEST_F(RemoteModelsTest, ThePhoneSwitchesUnitsAndReadsTheNotifications) {
    EXPECT_TRUE(machine_->settings().metric);
    send(R"({"type":"call","model":"dro","method":"toggleUnits"})");
    EXPECT_FALSE(machine_->settings().metric);
    send(R"({"type":"call","model":"dro","method":"toggleUnits"})");
    EXPECT_TRUE(machine_->settings().metric);

    backend_->notify(QStringLiteral("Hello from the shop"), QStringLiteral("warning"));
    ASSERT_TRUE(waitFor([&] { return !properties("notifications")["notifications"].toArray().isEmpty(); }));
    EXPECT_EQ(properties("notifications")["notifications"].toArray()[0].toObject()["message"].toString().toStdString(),
              "Hello from the shop");
    send(R"({"type":"call","model":"notifications","method":"clear"})");
    EXPECT_TRUE(properties("notifications")["notifications"].toArray().isEmpty());

    // The connection list and the machine's information are there to read.
    EXPECT_TRUE(properties("connection").contains("ports"));
    EXPECT_TRUE(properties("status").contains("modals"));
}

TEST_F(RemoteModelsTest, ThePhoneConnectsAndDisconnects) {
    send(R"({"type":"call","model":"connection","method":"connectTo","args":["Simulator"]})");
    ASSERT_TRUE(waitFor([&] { return machine_->isConnected(); }));
    ASSERT_TRUE(waitFor([&] { return properties("connection")["state"].toString() == "connected"; }));
    send(R"({"type":"call","model":"connection","method":"disconnectMachine"})");
    ASSERT_TRUE(waitFor([&] { return !machine_->isConnected(); }));
}
