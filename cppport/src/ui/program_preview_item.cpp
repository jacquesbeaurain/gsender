#include "program_preview_item.hpp"

#include <QPainter>

namespace gs::ui {

void ProgramPreviewItem::componentComplete() {
    ToolpathItem::componentComplete();
    setView(QStringLiteral("top"));
}

void ProgramPreviewItem::setProgram(const QString& program) {
    if (program == program_) {
        return;
    }
    program_ = program;
    path_ = program.isEmpty() ? app::Toolpath{} : app::traceToolpath(program.toStdString());
    Q_EMIT programChanged();
    fit();
}

std::optional<gcode::BoundingBox> ProgramPreviewItem::contentBounds() const {
    return path_.bounded ? std::optional<gcode::BoundingBox>(path_.bounds) : std::nullopt;
}

void ProgramPreviewItem::paintContent(QPainter& painter, app::ToolpathCamera& camera) {
    const app::VisualizerTheme& colors = machine() ? app::mainViewTheme(*machine()) : app::VisualizerTheme{};
    app::scene::paintBackground(painter, camera, colors, contentBounds());
    QColor rapidColor = colors.rapid;
    rapidColor.setAlphaF(0.5f);
    const QPen rapid(rapidColor, 1, Qt::DashLine);
    const QPen cut(colors.cutting, 1.5);
    app::scene::paintSegments(painter, camera, path_.rapids, path_.rapidLines,
                              [&rapid](std::size_t, std::uint32_t) { return &rapid; });
    app::scene::paintSegments(painter, camera, path_.feeds, path_.feedLines,
                              [&cut](std::size_t, std::uint32_t) { return &cut; });
}

}  // namespace gs::ui
