#include "visualizer_theme.hpp"

#include <array>
#include <utility>

namespace gs::app {
namespace {

struct Preset {
    const char* name;
    const char* background;
    const char* rapid;
    const char* cutting;
    const char* processed;
    const char* machineBedKeepout;
    const char* gridMajor;
    const char* gridMinor;
    const char* axisX;
    const char* axisY;
    const char* axisZ;
    bool light;
};

// gviewer's gCodeViewerThemePresets (the "dark" one with gSender's Workshop
// colours).
constexpr std::array<Preset, 7> kPresets{{
    {"Dark", "#090D12", "#059669", "#3F85C7", "#59687B", "#df3b3b", "#72849D", "#3F4B59", "#dc2626", "#059669",
     "#3F85C7", false},
    {"Light", "#e5e7eb", "#295d8d", "#111827", "#9ca3af", "#df3b3b", "#295d8d", "#5191cc", "#df3b3b", "#06b881",
     "#295d8d", true},
    {"Flexoki Dark", "#100f0f", "#DA702C", "#3AA99F", "#6f6e69", "#d14d41", "#a89984", "#6f6e69", "#d14d41",
     "#879a39", "#4385be", false},
    {"Tokyo Night", "#1a1b26", "#ff9e64", "#7dcfff", "#565f89", "#f7768e", "#7aa2f7", "#414868", "#f7768e", "#9ece6a",
     "#7aa2f7", false},
    {"Gruvbox Light", "#fbf1c7", "#d65d0e", "#3c3836", "#928374", "#cc241d", "#7c6f64", "#a89984", "#cc241d",
     "#98971a", "#458588", true},
    {"Ayu Dark", "#0b0e14", "#FFA659", "#73D0FF", "#6E7C8F", "#F28779", "#77a9d7", "#3e6590", "#F28779", "#D5FF80",
     "#5CCFE6", false},
    {"Ayu Light", "#fafafa", "#FA8D3E", "#22A4E6", "#ADAEB1", "#F07171", "#5c6773", "#9aacba", "#F07171", "#86B300",
     "#55B4D4", true},
}};

VisualizerTheme build(const Preset& p) {
    VisualizerTheme t;
    t.background = QColor(p.background);
    t.gridMajor = QColor(p.gridMajor);
    t.gridMinor = QColor(p.gridMinor);
    t.axisX = QColor(p.axisX);
    t.axisY = QColor(p.axisY);
    t.axisZ = QColor(p.axisZ);
    t.rapid = QColor(p.rapid);
    t.cutting = QColor(p.cutting);
    t.processed = QColor(p.processed);
    t.keepout = QColor(p.machineBedKeepout);
    t.light = p.light;
    if (QStringLiteral("Dark") == QString::fromLatin1(p.name)) {
        // WORKSHOP_VISUALIZER_COLORS
        t.boundingBox = QColor("#659dd2");
        t.machineBed = QColor("#c27924");
        t.tool = QColor("#79aad8");
    } else {
        t.boundingBox = QColor(p.light ? "#1d4ed8" : "#93c5fd");
        t.machineBed = QColor(p.light ? "#b45309" : "#fbbf24");
        t.tool = t.boundingBox;
    }
    t.text = QColor(p.light ? "#374151" : "#a8b0b8");
    return t;
}

const std::array<VisualizerTheme, kPresets.size()>& themes() {
    static const std::array<VisualizerTheme, kPresets.size()> all = [] {
        std::array<VisualizerTheme, kPresets.size()> built{};
        for (std::size_t i = 0; i < kPresets.size(); ++i) {
            built[i] = build(kPresets[i]);
        }
        return built;
    }();
    return all;
}

}  // namespace

QStringList visualizerThemeNames() {
    QStringList names;
    for (const Preset& preset : kPresets) {
        names << QString::fromLatin1(preset.name);
    }
    return names;
}

const VisualizerTheme& visualizerTheme(const QString& name) {
    for (std::size_t i = 0; i < kPresets.size(); ++i) {
        if (name == QString::fromLatin1(kPresets[i].name)) {
            return themes()[i];
        }
    }
    return themes()[0];
}

}  // namespace gs::app
