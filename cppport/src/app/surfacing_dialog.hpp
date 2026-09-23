#pragma once

// gSender's Surfacing tool: the stock rectangle, depths, bit and cutting
// settings in; a surfacing program out, previewed and loaded as the job.

#include "gs/surfacing/surfacing.hpp"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace gs::app {

class Machine;
class ToolpathPreview;

class SurfacingDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SurfacingDialog(Machine& machine, QWidget* parent = nullptr);

    surfacing::Options options() const;
    void setOptions(const surfacing::Options& options);

    // "Generate G-code": fills the preview and the G-code tab.
    void generate();
    const QString& program() const noexcept { return program_; }
    // "Load to Main Visualizer": the program becomes the job
    // (gSender_Surfacing.gcode). False when there is none or the machine is busy.
    bool loadIntoMachine();

    void done(int result) override;  // keeps the settings

private:
    void refresh();
    void save();

    Machine& machine_;
    QDoubleSpinBox* width_;
    QDoubleSpinBox* length_;
    QDoubleSpinBox* skimDepth_;
    QDoubleSpinBox* maxDepth_;
    QLabel* depthWarning_;
    QDoubleSpinBox* bitDiameter_;
    QSpinBox* toolNumber_;
    QDoubleSpinBox* stepover_;
    QDoubleSpinBox* feedrate_;
    QDoubleSpinBox* spindleRPM_;
    QComboBox* spindle_;
    QCheckBox* dwell_;
    QCheckBox* mist_;
    QCheckBox* flood_;
    QComboBox* start_;
    QComboBox* pattern_;
    QCheckBox* flipped_;
    QTabWidget* views_;
    ToolpathPreview* preview_;
    QPlainTextEdit* gcode_;
    QPushButton* generate_;
    QPushButton* load_;
    QString program_;
};

}  // namespace gs::app
