// Ports of src/server/controllers/__tests__/{lineParserResultStatus,
// lineParserRouting,sharedLineParserResults}.test.js and
// Grblhal/__tests__/GrblHalLineParserResultAXS.test.js.

#include "gs/protocol/line_parser.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace gs::protocol;

namespace {

StatusReport grbl(std::string_view line) {
    auto report = parseStatusReport(line, false);
    EXPECT_TRUE(report.has_value()) << line;
    return report.value_or(StatusReport{});
}

StatusReport hal(std::string_view line) {
    auto report = parseStatusReport(line, true);
    EXPECT_TRUE(report.has_value()) << line;
    return report.value_or(StatusReport{});
}

template <typename T>
bool routesTo(const ResponseLine& line) {
    return std::holds_alternative<T>(line);
}

void expectAxes(const AxisValues& v, std::initializer_list<double> expected) {
    ASSERT_EQ(v.count, expected.size());
    std::size_t i = 0;
    for (double e : expected) {
        EXPECT_DOUBLE_EQ(v.values[i++], e);
    }
}

}  // namespace

// ---- status reports ------------------------------------------------------------

TEST(StatusReport, NonStatusLinesAreDeclined) {
    for (std::string_view line : {"ok", "ALARM:1", "[MSG:x]", "<unterminated", ""}) {
        EXPECT_FALSE(parseStatusReport(line, false)) << line;
        EXPECT_FALSE(parseStatusReport(line, true)) << line;
    }
}

TEST(StatusReport, ActiveStateAndSubState) {
    struct Case {
        const char* line;
        const char* state;
        int sub;
    };
    for (const Case& c : {Case{"<Idle>", "Idle", 0}, Case{"<Run|MPos:0,0,0|FS:0,0>", "Run", 0},
                          Case{"<Jog|MPos:0,0,0|FS:0,0>", "Jog", 0}, Case{"<Hold:0|MPos:0,0,0|FS:0,0>", "Hold", 0},
                          Case{"<Hold:1|MPos:0,0,0|FS:0,0>", "Hold", 1}, Case{"<Door:3|MPos:0,0,0|FS:0,0>", "Door", 3}}) {
        EXPECT_EQ(grbl(c.line).activeState, c.state);
        EXPECT_EQ(grbl(c.line).subState, c.sub);
        EXPECT_EQ(hal(c.line).activeState, c.state);
        EXPECT_EQ(hal(c.line).subState, c.sub);
    }
    const StatusReport idle = grbl("<Idle>");
    EXPECT_FALSE(idle.mpos);
    EXPECT_FALSE(idle.wpos);
    EXPECT_EQ(idle.pinState, std::optional<std::string>(""));
}

TEST(StatusReport, Positions) {
    expectAxes(*grbl("<Idle|MPos:3.000,2.000,0.000|FS:0,0>").mpos, {3, 2, 0});
    expectAxes(*grbl("<Idle|MPos:1,2,3,4|FS:0,0>").mpos, {1, 2, 3, 4});
    expectAxes(*hal("<Idle|MPos:1,2,3,4|FS:0,0>").mpos, {1, 2, 3, 4});
    const char* withWco = "<Idle|MPos:5.000,2.000,0.000|FS:0,0|WCO:1.000,2.000,3.000>";
    expectAxes(*grbl(withWco).wco, {1, 2, 3});
    expectAxes(*hal(withWco).wco, {1, 2, 3});
    EXPECT_FALSE(grbl("<Idle|MPos:1,2,3|FS:0,0>").wpos);
    expectAxes(*grbl("<Idle|MPos:-1.5,-2.25,-0.001|FS:0,0>").mpos, {-1.5, -2.25, -0.001});
}

TEST(StatusReport, BufferLineFeedAndSpindle) {
    const StatusReport bf = grbl("<Idle|MPos:0,0,0|Bf:15,128|FS:0,0>");
    ASSERT_TRUE(bf.buf);
    EXPECT_EQ(bf.buf->planner, 15);
    EXPECT_EQ(bf.buf->rx, 128);

    const StatusReport run = grbl("<Run|MPos:0,0,0|FS:500,8000>");
    EXPECT_EQ(run.feedrate, 500);
    EXPECT_EQ(run.spindle, 8000);

    const StatusReport v09 = grbl("<Idle,MPos:0,0,0,Buf:0,RX:0,Ln:0,F:250.>");
    EXPECT_EQ(v09.feedrate, 250);
    EXPECT_FALSE(v09.spindle);

    EXPECT_EQ(grbl("<Jog|MPos:0,0,0|FS:0,0|Ln:42>").lineNumber, 42);
}

TEST(StatusReport, PinsAccessoriesAndOverrides) {
    EXPECT_EQ(grbl("<Idle|MPos:0,0,0|Pn:PZ|FS:0,0>").pinState, "PZ");
    EXPECT_EQ(grbl("<Idle|MPos:0,0,0|FS:0,0>").pinState, "");
    EXPECT_EQ(grbl("<Idle|MPos:0,0,0|A:SF|FS:0,0>").accessoryState, "SF");

    const StatusReport ov = grbl("<Idle|MPos:0,0,0|FS:0,0|Ov:100,90,110>");
    ASSERT_TRUE(ov.overrides);
    EXPECT_EQ(*ov.overrides, (std::array<int, 3>{100, 90, 110}));
    EXPECT_FALSE(grbl("<Idle|MPos:0,0,0|FS:0,0>").overrides);
}

TEST(StatusReport, LegacyLimitPinsAreIgnoredLikeGsender) {
    const StatusReport report = grbl("<Idle,MPos:0.000,0.000,0.000,WPos:0.000,0.000,0.000,Buf:0,RX:0,Lim:007>");
    EXPECT_EQ(report.pinState, "");
}

TEST(StatusReport, CommaDelimitedV09Reports) {
    const char* v09 = "<Idle,MPos:0.000,0.000,0.000,WPos:1.000,2.000,3.000,Buf:0,RX:5,Lim:000>";
    const StatusReport g = grbl(v09);
    expectAxes(*g.mpos, {0, 0, 0});
    expectAxes(*g.wpos, {1, 2, 3});
    EXPECT_EQ(g.buf->rx, 5);

    // grblHAL's matcher swallows "WPos" as a fourth value (as in gSender).
    const StatusReport h = hal(v09);
    ASSERT_EQ(h.mpos->count, 4u);
    EXPECT_TRUE(std::isnan(h.mpos->values[3]));
    EXPECT_FALSE(h.wpos);
}

TEST(StatusReport, GrblHalFields) {
    EXPECT_TRUE(hal("<Idle>").sdProgress);
    EXPECT_EQ(hal("<Idle>").sdProgress->percentage, 0);
    const StatusReport r = hal("<Idle|MPos:0,0,0|FS:0,0|H:1|T:3|P:0,P|ATCI:KL>");
    EXPECT_EQ(r.hasHomed, true);
    EXPECT_EQ(r.currentTool, 3);
    ASSERT_TRUE(r.probe);
    EXPECT_TRUE(r.probe->isProtected);
    ASSERT_TRUE(r.keepoutFlags);
    EXPECT_EQ(r.keepoutFlags->size(), 2u);
}

TEST(StatusReport, BothFirmwaresAgreeOnV11Reports) {
    for (std::string_view line :
         {"<Idle|MPos:3.000,2.000,0.000|FS:0,0>", "<Run|MPos:23.036,1.620,0.000|FS:500,0>",
          "<Hold:0|MPos:5.000,2.000,0.000|FS:0,0>", "<Idle|MPos:5.000,2.000,0.000|FS:0,0|WCO:0.000,0.000,0.000>",
          "<Idle|MPos:1.000,2.000,3.000|Pn:PZ|FS:0,0>", "<Idle|MPos:1.000,2.000,3.000|A:SF|FS:0,0>",
          "<Jog|MPos:1.000,2.000,3.000|FS:1000,0|Ln:42>"}) {
        StatusReport h = hal(line);
        h.sdProgress.reset();
        EXPECT_EQ(h, grbl(line)) << line;
    }
}

TEST(StatusReport, CompleteReport) {
    auto report = parseCompleteStatusReport(
        "<Alarm:11|MPos:0.000,0.000,0.000|Bf:128,1024|FS:0,0|Pn:PXYZHS|WCO:10.000,0.000,0.000|WCS:G54|A:|Sc:|MPG:0|H:0|"
        "T:2|TLR:0|SD:1|FW:grblHAL>");
    ASSERT_TRUE(report);
    EXPECT_EQ(report->activeState, "Alarm");
    EXPECT_EQ(report->subState, 11);
    EXPECT_EQ(report->pinState, "PXYZHS");
    EXPECT_EQ(report->currentTool, 2);
    EXPECT_EQ(report->hasHomed, false);
    EXPECT_EQ(report->sdCard, true);
    expectAxes(*report->wco, {10, 0, 0});
    EXPECT_FALSE(parseCompleteStatusReport("<Idle|MPos:0,0,0|FS:0,0>"));
}

// ---- routing -----------------------------------------------------------------

TEST(LineRouting, LinesBothParsersAgreeOn) {
    for (auto parse : {parseGrblResponse, parseGrblHalResponse}) {
        auto alarm = parse("ALARM:1");
        ASSERT_TRUE(routesTo<AlarmLine>(alarm));
        EXPECT_EQ(std::get<AlarmLine>(alarm).message, "1");
        EXPECT_EQ(std::get<AlarmLine>(parse("ALARM:Hard/soft limit")).message, "Hard/soft limit");

        auto error = parse("error:9");
        ASSERT_TRUE(routesTo<ErrorLine>(error));
        EXPECT_EQ(std::get<ErrorLine>(error).message, "9");
        EXPECT_EQ(std::get<ErrorLine>(parse("error:Modal group violation")).message, "Modal group violation");

        EXPECT_EQ(std::get<EchoLine>(parse("[echo:G1X1]")).message, "G1X1");
        EXPECT_EQ(std::get<HelpLine>(parse("[HLP:$$ $# $G]")).message, "$$ $# $G");
        EXPECT_EQ(std::get<FeedbackLine>(parse("[Caution: Unlocked]")).message, "Caution: Unlocked");

        auto setting = std::get<SettingLine>(parse("$10=511"));
        EXPECT_EQ(setting.name, "$10");
        EXPECT_EQ(setting.value, "511");
        EXPECT_EQ(setting.message, "");

        auto annotated = std::get<SettingLine>(parse("$132=200.000 (z max travel, mm)"));
        EXPECT_EQ(annotated.name, "$132");
        EXPECT_EQ(annotated.value, "200.000");
        EXPECT_EQ(annotated.message, "z max travel, mm");

        EXPECT_TRUE(routesTo<OkLine>(parse("ok")));
        EXPECT_TRUE(routesTo<OtherLine>(parse("   ")));
        EXPECT_TRUE(routesTo<OtherLine>(parse("not a grbl line at all")));
        EXPECT_TRUE(routesTo<FeedbackLine>(parse("[anything else]")));
    }
}

TEST(LineRouting, MsgAndOptDifferBetweenFirmwares) {
    EXPECT_EQ(std::get<FeedbackLine>(parseGrblResponse("[MSG:Pgm End]")).message, "Pgm End");
    auto info = std::get<InfoLine>(parseGrblHalResponse("[MSG:Pgm End]"));
    EXPECT_EQ(info.name, "MSG");
    EXPECT_EQ(info.value.text, "Pgm End");

    EXPECT_EQ(std::get<OptionLine>(parseGrblResponse("[OPT:VL,15,128]")).message, "VL,15,128");
    auto opt = std::get<InfoLine>(parseGrblHalResponse("[OPT:VL,15,128]"));
    EXPECT_EQ(opt.name, "OPT");
    EXPECT_EQ(opt.value.text, "VL,15,128");
}

TEST(LineRouting, SpecificBracketedParsersWinOverFeedback) {
    EXPECT_TRUE(routesTo<EchoLine>(parseGrblResponse("[echo:X]")));
    EXPECT_TRUE(routesTo<HelpLine>(parseGrblResponse("[HLP:X]")));
    EXPECT_TRUE(routesTo<OptionLine>(parseGrblResponse("[OPT:X]")));
    EXPECT_TRUE(routesTo<VersionLine>(parseGrblResponse("[VER:1.1f.20170801]")));
    EXPECT_TRUE(routesTo<EchoLine>(parseGrblHalResponse("[echo:X]")));
    EXPECT_TRUE(routesTo<HelpLine>(parseGrblHalResponse("[HLP:X]")));
}

TEST(LineRouting, GrblOkMatcherIsLoose) {
    for (std::string_view line : {"o", "k", "ooo", "kk", "ookkk"}) {
        EXPECT_TRUE(routesTo<OkLine>(parseGrblResponse(line))) << line;
        EXPECT_FALSE(routesTo<OkLine>(parseGrblHalResponse(line))) << line;
    }
}

TEST(LineRouting, SharedMatchersDecline) {
    for (std::string_view line : {"alarm:1", "ALARM:", "ERROR:9", "error:", "[ECHO:x]", "[echo:]", "[hlp:x]",
                                  "[HLP:]", "[]", "MSG:Pgm End", "10=511"}) {
        const ResponseLine r = parseGrblResponse(line);
        EXPECT_FALSE(routesTo<AlarmLine>(r) || routesTo<ErrorLine>(r) || routesTo<EchoLine>(r) ||
                     routesTo<HelpLine>(r) || routesTo<SettingLine>(r))
            << line;
    }
    EXPECT_EQ(std::get<SettingLine>(parseGrblResponse("$100=250.000 ")).value, "250.000");
    EXPECT_EQ(std::get<AlarmLine>(parseGrblResponse("ALARM: 9")).message, "9");
}

TEST(LineRouting, ParserState) {
    auto state = std::get<ParserStateLine>(parseGrblResponse("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]"));
    EXPECT_EQ(state.modal.motion, "G0");
    EXPECT_EQ(state.modal.wcs, "G54");
    EXPECT_EQ(state.modal.units, "G21");
    EXPECT_EQ(state.modal.spindle, "M5");
    EXPECT_EQ(state.modal.coolant, (std::vector<std::string>{"M9"}));
    EXPECT_EQ(state.tool, "0");
    EXPECT_EQ(state.feedrate, "0");
    EXPECT_EQ(state.spindle, "0");

    auto both = std::get<ParserStateLine>(parseGrblResponse("[GC:G1 G55 G18 G20 G91 G93 M0 M3 M7 M8 T2 F500. S12000.]"));
    EXPECT_EQ(both.modal.coolant, (std::vector<std::string>{"M7", "M8"}));
    EXPECT_EQ(both.modal.distance, "G91");
    EXPECT_EQ(both.modal.program, "M0");
    EXPECT_EQ(both.feedrate, "500.");

    auto halState = std::get<ParserStateLine>(parseGrblHalResponse("[GC:G0 G54 G17 G21 G90 G94 G49 G98 G50 M5 M9 T0 F0 S0.]"));
    EXPECT_EQ(halState.modal.cycle, "G98");
}

TEST(LineRouting, ParametersAndProbe) {
    auto g54 = std::get<ParametersLine>(parseGrblResponse("[G54:10.000,-5.000,1.500]"));
    EXPECT_EQ(g54.name, "G54");
    expectAxes(g54.value.axes, {10, -5, 1.5});

    auto prb = std::get<ParametersLine>(parseGrblResponse("[PRB:0.000,0.000,1.492:1]"));
    expectAxes(prb.value.axes, {0, 0, 1.492});
    EXPECT_EQ(prb.value.result, 1);

    auto tlo = std::get<ParametersLine>(parseGrblHalResponse("[TLO:0.000,0.000,1.000]"));
    EXPECT_EQ(tlo.value.raw, "0.000,0.000,1.000");
    EXPECT_TRUE(routesTo<ParametersLine>(parseGrblHalResponse("[G59.3:1,2,3]")));
    EXPECT_FALSE(routesTo<ParametersLine>(parseGrblResponse("[G59.3:1,2,3]")));
}

TEST(LineRouting, StartupBanner) {
    auto banner = std::get<StartupLine>(parseGrblResponse("Grbl 1.1h ['$' for help]"));
    EXPECT_EQ(banner.firmware, "Grbl");
    EXPECT_EQ(banner.version, "1.1h");
    EXPECT_EQ(banner.message, "['$' for help]");
    auto hal = std::get<StartupLine>(parseGrblHalResponse("GrblHAL 1.1f ['$' or '$HELP' for help]"));
    EXPECT_EQ(hal.firmware, "GrblHAL");
    EXPECT_EQ(hal.version, "1.1f");
}

// ---- grblHAL-only lines ----------------------------------------------------------

TEST(GrblHalLines, Axs) {
    const auto axs = [](std::string_view line) { return std::get<AxsLine>(parseGrblHalResponse(line)); };
    EXPECT_EQ(axs("[AXS:3:XYZ]").count, 3);
    EXPECT_EQ(axs("[AXS:3:XYZ]").letters, "XYZ");
    EXPECT_EQ(axs("[AXS:6:XYZABC]").letters, "XYZABC");
    EXPECT_EQ(axs("[AXS:4:XYZC]").letters, "XYZC");
    EXPECT_EQ(axs("[AXS:3:ZYX]").letters, "ZYX");
    EXPECT_EQ(axs("[AXS:3:xyz]").letters, "XYZ");
    EXPECT_EQ(axs("[AXS:3:XY]").count, 2);
    EXPECT_EQ(axs("[AXS:4]").count, 4);
    EXPECT_EQ(axs("[AXS:4]").letters, "");
    // Unusable AXS lines fall through to later parsers.
    EXPECT_FALSE(routesTo<AxsLine>(parseGrblHalResponse("[AXS:]")));
    EXPECT_FALSE(routesTo<AxsLine>(parseGrblHalResponse("[AXS:abc]")));
}

TEST(GrblHalLines, VersionInfoAndOptions) {
    EXPECT_EQ(std::get<VersionLine>(parseGrblHalResponse("[VER:1.1f.20240512:]")).text, "1.1f.20240512:");
    auto newopt = std::get<InfoLine>(parseGrblHalResponse("[NEWOPT:ENUMS,RT+,ATC=1,SD]"));
    EXPECT_TRUE(newopt.value.isOptionList);
    EXPECT_EQ(newopt.value.option("ATC"), std::optional<std::string>("1"));
    EXPECT_FALSE(newopt.value.option("SD"));  // flag without a value
    auto board = std::get<InfoLine>(parseGrblHalResponse("[BOARD:SLB Lite]"));
    EXPECT_EQ(board.name, "BOARD");
    EXPECT_EQ(board.value.text, "SLB Lite");
}

TEST(GrblHalLines, SettingMetadata) {
    auto description = std::get<SettingDescriptionLine>(
        parseGrblHalResponse("[SETTING:342|9|Tool change probing distance|mm|6|#####0.0|||0|0]"));
    EXPECT_EQ(description.description.id, 342);
    EXPECT_EQ(description.description.group, 9);
    EXPECT_EQ(description.description.description, "Tool change probing distance");
    EXPECT_EQ(description.description.unit, "mm");
    EXPECT_EQ(description.description.dataType, 6);
    EXPECT_EQ(description.description.format, (std::vector<std::string>{"#####0.0"}));

    auto select = std::get<SettingDescriptionLine>(parseGrblHalResponse(
        "[SETTING:341|9|Tool change mode||3|Normal,Manual touch off,Manual touch off @ G59.3|||0|0]"));
    EXPECT_EQ(select.description.format.size(), 3u);

    auto group = std::get<GroupDetailLine>(parseGrblHalResponse("[SETTINGGROUP:3|0|Limits]"));
    EXPECT_EQ(group.group.id, 3);
    EXPECT_EQ(group.group.label, "Location");  // relabelled

    auto alarm = std::get<AlarmDetailLine>(parseGrblHalResponse("[ALARMCODE:16||Power on selftest (POS) failed.]"));
    EXPECT_EQ(alarm.alarm.code, 16);
    EXPECT_EQ(alarm.alarm.description, "Power on selftest (POS) failed.");
    auto error = std::get<ErrorDescriptionLine>(parseGrblHalResponse("[ERRORCODE:62||Directory listing failed.]"));
    EXPECT_EQ(error.error.code, 62);

    auto details = std::get<SettingDetailsLine>(
        parseGrblHalResponse("120\tX-axis acceleration\tmm/sec^2\t\t\tAcceleration. Used for motion planning."));
    EXPECT_EQ(details.id, 120);
    EXPECT_EQ(details.unitString, "mm/sec^2");
    EXPECT_EQ(details.details, "Acceleration. Used for motion planning.");
    EXPECT_FALSE(routesTo<SettingDetailsLine>(parseGrblHalResponse("\"$-Code\"\t\"Setting\"")));
}

TEST(GrblHalLines, ToolsSpindlesAndFiles) {
    auto tool = std::get<ToolLine>(parseGrblHalResponse("[T:1|0.000,0.000,-12.3456,0.000|0.000]"));
    EXPECT_EQ(tool.tool.id, 1);
    EXPECT_DOUBLE_EQ(tool.tool.offsets.values[2], -12.346);

    auto spindle = std::get<SpindleLine>(parseGrblHalResponse("[SPINDLE:0|1|0|*DIRV|PWM|0.0,1000.0]"));
    EXPECT_EQ(spindle.id, 1);
    EXPECT_TRUE(spindle.enabled);
    EXPECT_FALSE(spindle.laser);
    EXPECT_EQ(spindle.capabilities, "DIRV");
    EXPECT_EQ(spindle.label, "PWM");

    auto legacy = std::get<SpindleLine>(parseGrblHalResponse("7 - SLB_LASER, enabled as spindle 0, DLIRV, current"));
    EXPECT_EQ(legacy.order, 7);
    EXPECT_EQ(legacy.label, "SLB_LASER");
    EXPECT_TRUE(legacy.laser);
    EXPECT_TRUE(legacy.enabled);

    auto file = std::get<SdFileLine>(parseGrblHalResponse("[FILE:/test.nc|SIZE:5580]"));
    EXPECT_EQ(file.file.name, "test.nc");
    EXPECT_EQ(file.file.size, 5580);
    EXPECT_TRUE(std::get<SdFileLine>(parseGrblHalResponse("[FILE:/big.nc|SIZE:9|UNUSABLE]")).file.unusable);
    EXPECT_TRUE(routesTo<OtherLine>(parseGrblHalResponse("[FILE:/._hidden.nc|SIZE:4096]")));
}

TEST(GrblHalLines, AtciAndAutoconfig) {
    auto atci = std::get<AtciLine>(parseGrblHalResponse("[MSG:ATCI:3|Title|Description|rack:1|tools:6]"));
    EXPECT_EQ(atci.subtype, std::optional<std::string>("3"));
    EXPECT_EQ(atci.message, std::optional<std::string>("Title"));
    EXPECT_EQ(atci.description, std::optional<std::string>("Description"));
    ASSERT_GE(atci.values.size(), 3u);
    EXPECT_EQ(atci.values[0].first, "rack");
    EXPECT_EQ(atci.values[0].second, std::optional<std::string>("1"));
    EXPECT_EQ(atci.values.back().second, std::optional<std::string>("0"));  // macro_abort

    auto aborted = std::get<AtciLine>(parseGrblHalResponse("[MSG:Error: ATCI|tool:5]"));
    EXPECT_EQ(aborted.values.back().first, "macro_abort");
    EXPECT_EQ(aborted.values.back().second, std::optional<std::string>("1"));

    auto autoconfig = std::get<AutoconfigLine>(parseGrblHalResponse("[MSG:Info: Autoconfig: TLS=1, ATC=0]"));
    ASSERT_EQ(autoconfig.values.size(), 2u);
    EXPECT_EQ(autoconfig.values[1].first, "ATC");
    EXPECT_EQ(autoconfig.values[1].second, "0");
}

TEST(GrblHalLines, Json) {
    EXPECT_EQ(std::get<JsonLine>(parseGrblHalResponse("{\"a\":1}")).code, "{\"a\":1}");
}
