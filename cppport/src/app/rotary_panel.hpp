#pragma once

// gSender's Rotary widget (features/Rotary): the rotary mode switch, the
// Rotary Surfacing and Mounting Setup tools and the rotary probing
// routines. Its tab shows while the Rotary controls are on (Settings).

#include "gs/rotary/rotary.hpp"

#include <QDialog>
#include <QString>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabWidget;

namespace gs::app {

class Machine;
class ToolpathPreview;

// Rotary Surfacing (RotarySurfacing.tsx): the stock's length and diameters
// and the cut in; a turning program out, previewed wrapped around X and
// loaded as the job.
class RotarySurfacingDialog final : public QDialog {
    Q_OBJECT
public:
    explicit RotarySurfacingDialog(Machine& machine, QWidget* parent = nullptr);

    // In the workspace units (stored in mm).
    rotary::StockTurningOptions options() const;
    void setOptions(const rotary::StockTurningOptions& options);

    // "Generate G-Code": fills the preview and the G-code tab.
    void generate();
    const QString& program() const noexcept { return program_; }
    // "Load to Main Visualizer": the program becomes the job
    // (gSender_Rotary_Surfacing). False when there is none or the machine
    // is busy.
    bool loadIntoMachine();

    void done(int result) override;  // keeps the settings

private:
    void refresh();
    void save();

    Machine& machine_;
    QDoubleSpinBox* stockLength_;
    QDoubleSpinBox* startHeight_;
    QDoubleSpinBox* finalHeight_;
    QDoubleSpinBox* stepdown_;
    QDoubleSpinBox* bitDiameter_;
    QSpinBox* toolNumber_;
    QDoubleSpinBox* stepover_;
    QDoubleSpinBox* feedrate_;
    QDoubleSpinBox* spindleRPM_;
    QCheckBox* dwell_;
    QCheckBox* rehoming_;
    QTabWidget* views_;
    QStackedWidget* previewPage_;
    ToolpathPreview* preview_;
    QPlainTextEdit* gcode_;
    QPushButton* generate_;
    QPushButton* load_;
    QString program_;
};

// Rotary Mounting Setup (MountingSetup.tsx): the track and the end mill in;
// loads the program that bores the rotary track's mounting holes.
class MountingSetupDialog final : public QDialog {
    Q_OBJECT
public:
    explicit MountingSetupDialog(Machine& machine, QWidget* parent = nullptr);

    rotary::MountingSetup setup() const;
    void setSetup(const rotary::MountingSetup& setup);
    // "Load G-Code to Visualizer": the program becomes the job
    // (gSender_Rotary_Mounting_Setup). False while a job runs.
    bool loadIntoMachine();

private:
    void refresh();  // the illustration follows the choices

    Machine& machine_;
    QButtonGroup* linesUp_;
    QButtonGroup* bit_;
    QButtonGroup* holes_;
    QButtonGroup* extension_;
    QLabel* illustration_;
};

class RotaryPanel final : public QWidget {
    Q_OBJECT
public:
    explicit RotaryPanel(Machine& machine, QWidget* parent = nullptr);

    // The questions the panel asks (title, text, confirm button): a message
    // box by default; replaceable for tests.
    using Confirmer = std::function<bool(const QString& title, const QString& text, const QString& confirm)>;
    void setConfirmer(Confirmer confirmer) { confirmer_ = std::move(confirmer); }

    // The Rotary switch: entering rotary mode first says what it will do
    // and asks; leaving does not ask. False when nothing changed.
    bool setRotaryMode(bool rotary);
    bool toggleRotaryMode();
    // "Probe Rotary Z-Axis" / "Y-Axis Alignment": asked ("Run"), then run.
    // False when not asked (the button is disabled) or declined.
    bool probeRotaryZ();
    bool alignYAxis();
    // The tools, as their buttons open them (modal).
    void openSurfacing();
    void openMountingSetup();

private:
    void refresh();
    bool confirm(const QString& title, const QString& text, const QString& confirmLabel);
    bool runProbe(bool yAlignment);

    Machine& machine_;
    QLabel* fourAxis_;
    QCheckBox* mode_;
    QPushButton* surfacing_;
    QPushButton* mounting_;
    QPushButton* probeZ_;
    QPushButton* alignY_;
    Confirmer confirmer_;
};

}  // namespace gs::app
