#pragma once

// A toolpath that is not the loaded job - a tool's generated program
// (Surfacing, Rotary Surfacing) - drawn with the visualizer's camera and
// gestures: cutting moves solid, rapids dashed, seen from the top to start.

#include "toolpath_item.hpp"

#include "machine.hpp"

#include <QString>

namespace gs::ui {

class ProgramPreviewItem : public ToolpathItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString program READ program WRITE setProgram NOTIFY programChanged)
    Q_PROPERTY(bool empty READ empty NOTIFY programChanged)

public:
    explicit ProgramPreviewItem(QQuickItem* parent = nullptr) : ToolpathItem(parent) {}

    QString program() const { return program_; }
    void setProgram(const QString& program);
    bool empty() const { return path_.feeds.empty() && path_.rapids.empty(); }

Q_SIGNALS:
    void programChanged();

protected:
    void componentComplete() override;
    void paintContent(QPainter& painter, app::ToolpathCamera& camera) override;
    std::optional<gcode::BoundingBox> contentBounds() const override;

private:
    QString program_;
    app::Toolpath path_;
};

}  // namespace gs::ui
