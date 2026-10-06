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
    for (const char* name : {"macros", "spindle", "coolant", "probe", "rotary", "config"}) {
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
