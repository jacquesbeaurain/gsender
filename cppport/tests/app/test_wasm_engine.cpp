// The Wasm interpreter against modules clang compiled from
// tests/data/wasm/engine_test.c (tools/build_wasm_fixtures.sh rebuilds them):
// -O0, -O2 and -O2 with every post-MVP feature clang knows (bulk memory,
// saturating conversions, multi-value, reference types, ...).

#include "wasm_engine.hpp"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QString>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace gs::app;

namespace {

std::shared_ptr<WasmModule> loadFixture(const std::string& name) {
    std::string err;
    auto module = WasmModule::loadFromFile(QStringLiteral(GS_SOURCE_DIR "/tests/data/wasm/") + QString::fromStdString(name), &err);
    EXPECT_NE(module, nullptr) << name << ": " << err;
    return module;
}

class WasmEngineFixtureTest : public ::testing::TestWithParam<const char*> {
protected:
    void SetUp() override {
        module_ = loadFixture(GetParam());
        ASSERT_NE(module_, nullptr);
        instance_ = std::make_unique<WasmInstance>(module_);
        instance_->linkHostFunction("env", "host_log", [this](WasmInstance&, const std::vector<WasmVal>& args) -> std::optional<WasmVal> {
            logged_.push_back(std::get<int32_t>(args.at(0)));
            return std::nullopt;
        });
        std::string err;
        ASSERT_TRUE(instance_->instantiate(&err)) << err;
    }

    int32_t callI32(const std::string& name, const std::vector<WasmVal>& args = {}) {
        const auto res = instance_->invoke(name, args);
        EXPECT_TRUE(res.has_value()) << name << ": " << instance_->lastErrorMessage();
        return res ? std::get<int32_t>(*res) : 0;
    }

    std::shared_ptr<WasmModule> module_;
    std::unique_ptr<WasmInstance> instance_;
    std::vector<int32_t> logged_;
};

}  // namespace

TEST_P(WasmEngineFixtureTest, RunsRecursion) {
    EXPECT_EQ(callI32("t_fib", {int32_t(20)}), 6765);
}

TEST_P(WasmEngineFixtureTest, RunsIndirectCallsThroughTheTable) {
    EXPECT_EQ(callI32("t_indirect", {int32_t(0), int32_t(7)}), 49);
    EXPECT_EQ(callI32("t_indirect", {int32_t(1), int32_t(3)}), 27);
}

TEST_P(WasmEngineFixtureTest, RunsFloatingPoint) {
    const auto res = instance_->invoke("t_float", {9.0, 2.0f});
    ASSERT_TRUE(res.has_value()) << instance_->lastErrorMessage();
    EXPECT_DOUBLE_EQ(std::get<double>(*res), 24.0);
    EXPECT_EQ(callI32("t_conv", {2.7f}), 7);
}

TEST_P(WasmEngineFixtureTest, RunsSwitchTables) {
    EXPECT_EQ(callI32("t_switch", {int32_t(0)}), 10);
    EXPECT_EQ(callI32("t_switch", {int32_t(2)}), 30);
    EXPECT_EQ(callI32("t_switch", {int32_t(5)}), 60);
    EXPECT_EQ(callI32("t_switch", {int32_t(3)}), -1);
    EXPECT_EQ(callI32("t_switch", {int32_t(99)}), -1);
}

TEST_P(WasmEngineFixtureTest, Runs64BitArithmetic) {
    const auto res = instance_->invoke("t_i64", {int64_t(123456789012LL), int64_t(98765)});
    ASSERT_TRUE(res.has_value()) << instance_->lastErrorMessage();
    EXPECT_EQ(std::get<int64_t>(*res), 1741887109526263LL);
}

TEST_P(WasmEngineFixtureTest, UsesTheShadowStackAndCallsTheHost) {
    EXPECT_EQ(callI32("t_struct", {int32_t(3)}), 6048);
    ASSERT_EQ(logged_.size(), 1u);
    EXPECT_EQ(logged_[0], 6048);
}

TEST_P(WasmEngineFixtureTest, ReadsAndWritesStaticData) {
    const int32_t ptr = callI32("t_str");
    EXPECT_EQ(instance_->memory().readString(static_cast<uint32_t>(ptr)), "HELLO WASM");
}

TEST_P(WasmEngineFixtureTest, AnEndlessLoopRunsOutOfFuel) {
    instance_->setFuelPerCall(1'000'000);
    EXPECT_FALSE(instance_->invoke("t_loop").has_value());
    EXPECT_EQ(instance_->lastTrap(), WasmTrap::OutOfFuel);
    // The instance stays usable.
    EXPECT_EQ(callI32("t_fib", {int32_t(10)}), 55);
}

INSTANTIATE_TEST_SUITE_P(Clang, WasmEngineFixtureTest,
                         ::testing::Values("engine_test_O0.wasm", "engine_test_O2.wasm", "engine_test_edge.wasm"));

TEST(WasmEngineTest, DeepRecursionTrapsInsteadOfOverflowingTheHostStack) {
    // (func $f (param i32) (result i32) local.get 0 call $f) exported as "f".
    const uint8_t bytes[] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,        //
                             0x01, 0x06, 0x01, 0x60, 0x01, 0x7F, 0x01, 0x7F,        // type (i32)->i32
                             0x03, 0x02, 0x01, 0x00,                                // one function
                             0x07, 0x05, 0x01, 0x01, 0x66, 0x00, 0x00,              // export "f"
                             0x0A, 0x08, 0x01, 0x06, 0x00, 0x20, 0x00, 0x10, 0x00, 0x0B};
    std::string err;
    auto module = WasmModule::loadFromBytes(bytes, sizeof bytes, &err);
    ASSERT_NE(module, nullptr) << err;
    WasmInstance inst(module);
    ASSERT_TRUE(inst.instantiate(&err)) << err;
    EXPECT_FALSE(inst.invoke("f", {int32_t(1)}).has_value());
    EXPECT_EQ(inst.lastTrap(), WasmTrap::CallStackExhausted);
}

TEST(WasmEngineTest, RejectsMalformedModulesWithoutCrashing) {
    std::string err;
    const uint8_t header[] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
    // Every truncation and every single-byte corruption of a real module either loads or fails cleanly.
    QFile f(QStringLiteral(GS_SOURCE_DIR "/tests/data/wasm/engine_test_O2.wasm"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QByteArray good = f.readAll();
    for (qsizetype len = 0; len < good.size(); ++len) {
        WasmModule::loadFromBytes(reinterpret_cast<const uint8_t*>(good.constData()), static_cast<size_t>(len), &err);
    }
    for (qsizetype i = 8; i < good.size(); ++i) {
        QByteArray bad = good;
        bad[i] = static_cast<char>(bad[i] ^ 0x5A);
        auto module = WasmModule::loadFromBytes(reinterpret_cast<const uint8_t*>(bad.constData()), static_cast<size_t>(bad.size()), &err);
        if (!module) continue;
        WasmInstance inst(module);
        inst.linkHostFunction("*", "host_log", [](WasmInstance&, const std::vector<WasmVal>&) -> std::optional<WasmVal> { return std::nullopt; });
        inst.setFuelPerCall(100'000);
        if (!inst.instantiate(&err)) continue;
        for (const WasmExport& exp : module->exports()) {
            if (exp.kind != 0) continue;
            std::vector<WasmVal> args;
            for (WasmValType t : module->functionType(exp.index).params) {
                switch (t) {
                    case WasmValType::I64: args.emplace_back(int64_t(3)); break;
                    case WasmValType::F32: args.emplace_back(3.0f); break;
                    case WasmValType::F64: args.emplace_back(3.0); break;
                    default: args.emplace_back(int32_t(3)); break;
                }
            }
            inst.invoke(exp.name, args);
        }
    }
    EXPECT_EQ(WasmModule::loadFromBytes(header, 4, &err), nullptr);
}

TEST(WasmEngineTest, MemoryIsCappedByTheHostLimit) {
    std::string err;
    auto module = loadFixture("engine_test_O2.wasm");
    ASSERT_NE(module, nullptr);
    WasmInstance inst(module);
    inst.linkHostFunction("env", "host_log", [](WasmInstance&, const std::vector<WasmVal>&) -> std::optional<WasmVal> { return std::nullopt; });
    inst.setMaxMemoryPages(0);
    EXPECT_FALSE(inst.instantiate(&err));
}
