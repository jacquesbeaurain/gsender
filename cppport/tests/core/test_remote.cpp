// Remote mode's rules: the address picker (network-interfaces.js), the saved
// settings (api.remote.js, the dialog's checks) and the pendant's messages.

#include "gs/remote/network.hpp"
#include "gs/remote/pendant.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>

using namespace gs;
using namespace gs::remote;
namespace json = boost::json;

TEST(RemoteNetwork, ClassifiesInterfacesByName) {
    EXPECT_EQ(classify("lo", true), AddressKind::Loopback);
    EXPECT_EQ(classify("Wi-Fi", false), AddressKind::Wifi);
    EXPECT_EQ(classify("wlan0", false), AddressKind::Wifi);
    EXPECT_EQ(classify("WiFi 2", false), AddressKind::Wifi);
    EXPECT_EQ(classify("Ethernet 2", false), AddressKind::Ethernet);
    EXPECT_EQ(classify("en0", false), AddressKind::Ethernet);
    EXPECT_EQ(classify("eth0", false), AddressKind::Ethernet);
    EXPECT_EQ(classify("vEthernet (WSL)", false), AddressKind::Virtual);  // virtual wins over "ethernet"
    EXPECT_EQ(classify("docker0", false), AddressKind::Virtual);
    EXPECT_EQ(classify("tailscale0", false), AddressKind::Virtual);
    EXPECT_EQ(classify("tun0", false), AddressKind::Virtual);
    EXPECT_EQ(classify("VirtualBox Host-Only Network", false), AddressKind::Virtual);
    EXPECT_EQ(classify("enp3s0", false), AddressKind::Unknown);  // ^en\d only
    EXPECT_EQ(kindLabel(AddressKind::Unknown), "Network");
    EXPECT_EQ(kindLabel(AddressKind::Loopback), "This computer only");
}

TEST(RemoteNetwork, RecommendsTheDefaultRouteAndSortsUsableFirst) {
    const std::vector<InterfaceAddress> interfaces{
        {"lo", "127.0.0.1", true},
        {"docker0", "172.17.0.1", false},
        {"eth0", "10.0.0.5", false},
        {"wlan0", "192.168.1.20", false},
        {"eth1", "169.254.3.4", false},  // APIPA: up, but no network
    };
    const std::vector<NetworkAddress> ranked = rankAddresses(interfaces, std::string("10.0.0.5"));
    ASSERT_EQ(ranked.size(), 5u);
    EXPECT_EQ(ranked[0].address, "10.0.0.5");
    EXPECT_TRUE(ranked[0].recommended);
    EXPECT_EQ(ranked[0].label, "Ethernet");
    EXPECT_EQ(ranked[1].address, "192.168.1.20");  // usable, Wi-Fi
    EXPECT_FALSE(ranked[1].recommended);
    // Unusable ones by kind: Ethernet (APIPA), virtual, loopback.
    EXPECT_EQ(ranked[2].address, "169.254.3.4");
    EXPECT_FALSE(ranked[2].usable);
    EXPECT_EQ(ranked[3].address, "172.17.0.1");
    EXPECT_EQ(ranked[4].address, "127.0.0.1");
}

TEST(RemoteNetwork, FallsBackToTheBestPrivateAddressWithoutARoute) {
    const std::vector<InterfaceAddress> interfaces{
        {"eth0", "8.8.4.4", false},        // public: +10
        {"eth1", "10.1.2.3", false},       // 10.x: +1
        {"Ethernet 3", "192.168.9.9", false},
        {"wlan0", "172.20.0.2", false},    // Wi-Fi ranks 0, 172.16/12: +2
    };
    // Scores: 11, 2, 1, 2 -> 192.168.9.9. A route to an unusable address is ignored.
    const std::vector<NetworkAddress> ranked = rankAddresses(interfaces, std::string("127.0.0.1"));
    EXPECT_EQ(ranked[0].address, "192.168.9.9");
    EXPECT_TRUE(ranked[0].recommended);
    EXPECT_EQ(ranked[1].address, "172.20.0.2");  // Wi-Fi before Ethernet
    EXPECT_EQ(ranked[2].address, "10.1.2.3");
    EXPECT_EQ(ranked[3].address, "8.8.4.4");
    EXPECT_TRUE(rankAddresses({{"lo", "127.0.0.1", true}}, std::nullopt)[0].recommended == false);
}

TEST(RemoteNetwork, KeepsTheBestClassifiedInterfaceForADuplicateAddress) {
    const std::vector<NetworkAddress> ranked =
        rankAddresses({{"vEthernet", "192.168.0.7", false}, {"Wi-Fi", "192.168.0.7", false}}, std::nullopt);
    ASSERT_EQ(ranked.size(), 1u);
    EXPECT_EQ(ranked[0].iface, "Wi-Fi");
    EXPECT_TRUE(ranked[0].usable);
    EXPECT_TRUE(ranked[0].recommended);
}

TEST(RemoteSettings, ValidatesAsTheDialogDoes) {
    EXPECT_TRUE(isIPv4("192.168.0.10"));
    EXPECT_TRUE(isIPv4("0.0.0.0"));
    EXPECT_TRUE(isIPv4("001.02.3.255"));  // the regex allows leading zeros
    EXPECT_FALSE(isIPv4("256.1.1.1"));
    EXPECT_FALSE(isIPv4("1.2.3"));
    EXPECT_FALSE(isIPv4("1.2.3.4 "));
    EXPECT_EQ(validateSettings("192.168.0.10", 8000), "");
    EXPECT_EQ(validateSettings("192.168.0.10", 1024), "Invalid Port Number - Must be between 1025 and 65535");
    EXPECT_EQ(validateSettings("192.168.0.10", 65536), "Invalid Port Number - Must be between 1025 and 65535");
    EXPECT_EQ(validateSettings("my-pc", 8000),
              "Invalid IP Address - my-pc does not look like a valid V4 IP address");
    EXPECT_EQ(pendantUrl("192.168.0.10", 8000), "http://192.168.0.10:8000/#/remote");
}

TEST(RemoteSettings, RoundTripsTheStoredObject) {
    const RemoteSettings defaults = settingsFromJson(nullptr);
    EXPECT_EQ(defaults.ip, "");
    EXPECT_EQ(defaults.port, 8000);
    EXPECT_FALSE(defaults.headlessStatus);

    const RemoteSettings read =
        settingsFromJson(json::parse(R"({"ip":"10.0.0.2","port":"8080","headlessStatus":true,"error":true})"));
    EXPECT_EQ(read.ip, "10.0.0.2");
    EXPECT_EQ(read.port, 8080);
    EXPECT_TRUE(read.headlessStatus);
    EXPECT_TRUE(read.error);

    EXPECT_EQ(json::serialize(settingsToJson({"10.0.0.2", 8001, true, false})),
              R"({"ip":"10.0.0.2","port":8001,"headlessStatus":true})");
}

TEST(RemotePendant, ParsesCommands) {
    using Kind = PendantCommand::Kind;
    const auto jog = parseCommand(R"({"type":"jogPress","x":1,"y":-1})");
    ASSERT_TRUE(jog);
    EXPECT_EQ(jog->kind, Kind::JogPress);
    EXPECT_EQ(jog->directions, (std::array<int, 4>{1, -1, 0, 0}));
    EXPECT_EQ(parseCommand(R"({"type":"jogPress","a":-0.5})")->directions, (std::array<int, 4>{0, 0, 0, -1}));
    EXPECT_FALSE(parseCommand(R"({"type":"jogPress"})"));  // no direction

    EXPECT_EQ(parseCommand(R"({"type":"start"})")->kind, Kind::Start);
    EXPECT_EQ(parseCommand(R"({"type":"pause"})")->kind, Kind::Pause);
    EXPECT_EQ(parseCommand(R"({"type":"stop"})")->kind, Kind::Stop);
    EXPECT_EQ(parseCommand(R"({"type":"jogRelease"})")->kind, Kind::JogRelease);
    EXPECT_EQ(parseCommand(R"({"type":"unlock"})")->kind, Kind::Unlock);

    const auto zero = parseCommand(R"({"type":"zeroAxis","axis":"z"})");
    ASSERT_TRUE(zero);
    EXPECT_EQ(zero->text, "Z");
    EXPECT_FALSE(parseCommand(R"({"type":"zeroAxis","axis":"B"})"));
    EXPECT_EQ(parseCommand(R"({"type":"goToZero","axes":"xy"})")->text, "XY");
    EXPECT_FALSE(parseCommand(R"({"type":"goToZero","axes":"XZ"})"));
    EXPECT_EQ(parseCommand(R"({"type":"workspace","wcs":"g55"})")->text, "G55");
    EXPECT_FALSE(parseCommand(R"({"type":"workspace","wcs":"G53"})"));
    EXPECT_FALSE(parseCommand(R"({"type":"workspace","wcs":"G54.1"})"));
    EXPECT_EQ(parseCommand(R"({"type":"preset","preset":"Rapid"})")->text, "Rapid");
    EXPECT_FALSE(parseCommand(R"({"type":"preset","preset":"Ludicrous"})"));

    EXPECT_FALSE(parseCommand("not json"));
    EXPECT_FALSE(parseCommand("[1,2]"));
    EXPECT_FALSE(parseCommand(R"({"type":"$H"})"));
    EXPECT_FALSE(parseCommand(R"({"type":7})"));
}

TEST(RemotePendant, ParsesTheToolPageCommands) {
    using Kind = PendantCommand::Kind;
    const auto subscribe = parseCommand(R"({"type":"subscribe","model":"spindle"})");
    ASSERT_TRUE(subscribe);
    EXPECT_EQ(subscribe->kind, Kind::Subscribe);
    EXPECT_EQ(subscribe->model, "spindle");
    EXPECT_EQ(parseCommand(R"({"type":"unsubscribe","model":"config"})")->kind, Kind::Unsubscribe);

    const auto call = parseCommand(R"({"type":"call","model":"spindle","method":"setSpeed","args":[12000.5],"id":7})");
    ASSERT_TRUE(call);
    EXPECT_EQ(call->kind, Kind::Call);
    EXPECT_EQ(call->member, "setSpeed");
    EXPECT_EQ(call->callId, 7);
    EXPECT_EQ(json::parse(call->argsJson), json::parse("[12000.5]"));
    // No arguments, no id: an empty list, no reply wanted.
    const auto bare = parseCommand(R"({"type":"call","model":"coolant","method":"off"})");
    ASSERT_TRUE(bare);
    EXPECT_EQ(bare->argsJson, "[]");
    EXPECT_EQ(bare->callId, 0);

    const auto set = parseCommand(R"({"type":"set","model":"config","property":"search","value":"baud"})");
    ASSERT_TRUE(set);
    EXPECT_EQ(set->kind, Kind::SetProperty);
    EXPECT_EQ(set->member, "search");
    EXPECT_EQ(set->argsJson, R"("baud")");

    // Anything that is not a plain model, member or argument list is refused.
    EXPECT_FALSE(parseCommand(R"({"type":"subscribe"})"));
    EXPECT_FALSE(parseCommand(R"({"type":"subscribe","model":"Spindle"})"));
    EXPECT_FALSE(parseCommand(R"({"type":"subscribe","model":"../x"})"));
    EXPECT_FALSE(parseCommand(R"({"type":"subscribe","model":""})"));
    EXPECT_FALSE(parseCommand(R"({"type":"call","model":"spindle"})"));
    EXPECT_FALSE(parseCommand(R"({"type":"call","model":"spindle","method":"a b"})"));
    EXPECT_FALSE(parseCommand(R"({"type":"call","model":"spindle","method":"1stop"})"));
    EXPECT_FALSE(parseCommand(R"({"type":"call","model":"spindle","method":"stop","args":5})"));
    EXPECT_FALSE(parseCommand(R"({"type":"call","model":"spindle","method":"stop","args":[1,2,3,4,5,6,7,8,9]})"));
    EXPECT_FALSE(parseCommand(R"({"type":"set","model":"config","property":"search"})"));
    EXPECT_FALSE(parseCommand(R"({"type":"set","model":"config","property":"search","value":[1]})"));
}

TEST(RemotePendant, ModelAndResultMessages) {
    const json::object model = json::parse(modelMessage("spindle", R"({"speed":1000,"forward":true})")).as_object();
    EXPECT_EQ(model.at("type").as_string(), "model");
    EXPECT_EQ(model.at("model").as_string(), "spindle");
    EXPECT_EQ(model.at("properties").as_object().at("speed").as_int64(), 1000);
    // Properties that are not a JSON object are sent as an empty one.
    EXPECT_TRUE(json::parse(modelMessage("x", "nonsense")).as_object().at("properties").as_object().empty());

    const json::object ok = json::parse(resultMessage(7, R"({"a":[1,2]})")).as_object();
    EXPECT_EQ(ok.at("type").as_string(), "result");
    EXPECT_EQ(ok.at("id").as_int64(), 7);
    EXPECT_TRUE(ok.at("ok").as_bool());
    EXPECT_EQ(ok.at("value").as_object().at("a").as_array().size(), 2u);
    const json::object failed = json::parse(resultMessage(8, "null", "not allowed")).as_object();
    EXPECT_FALSE(failed.at("ok").as_bool());
    EXPECT_EQ(failed.at("error").as_string(), "not allowed");
}

TEST(RemotePendant, StateMessageCarriesTheDroAndTheButtons) {
    PendantState s;
    s.connected = true;
    s.activeState = "Idle";
    s.statusLabel = "Idle";
    s.wpos = {1.5, -2, 0.25, std::nan("")};
    s.canJog = true;
    s.canRun = true;
    s.fileName = "part.nc";
    const json::object o = json::parse(stateMessage(s)).as_object();
    EXPECT_EQ(o.at("type").as_string(), "state");
    EXPECT_TRUE(o.at("connected").as_bool());
    EXPECT_EQ(o.at("wpos").as_array()[0].to_number<double>(), 1.5);
    EXPECT_EQ(o.at("wpos").as_array()[3].to_number<double>(), 0.0);  // NaN is not JSON
    EXPECT_TRUE(o.at("canRun").as_bool());
    EXPECT_FALSE(o.at("canStop").as_bool());
    EXPECT_EQ(o.at("fileName").as_string(), "part.nc");
    EXPECT_EQ(o.at("workflow").as_string(), "idle");
}
