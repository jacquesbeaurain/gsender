#pragma once

// Spindle/coolant controls, job overrides and the macro list (gSender's
// Spindle/Laser, Coolant and Macros widgets and the job overrides).

#include <QDialog>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QTimer;

namespace gs::app {

class Machine;

// The Spindle/Laser tab (gSender's Spindle widget, with the coolant
// buttons): the spindle's speed and direction or - in laser mode - the
// laser's power, focus and test; the mode switch; grblHAL's spindles.
class SpindlePanel final : public QWidget {
    Q_OBJECT
public:
    explicit SpindlePanel(Machine& machine, QWidget* parent = nullptr);

    // The buttons and their shortcuts, as upstream: CW or laser on (focus),
    // CCW or laser test, stop / laser off, and the mode switch.
    void startClockwise();
    void startCounterClockwise();
    void stopSpindle();
    void toggleMode();
    // The speed (rpm) and laser power (%) controls: a change reaches a running
    // spindle or lit laser 300 ms after the last one, as upstream's debounce.
    void setSpeed(double rpm);
    void setLaserPower(double percent);
    bool isSpindleOn() const noexcept { return spindleOn_; }
    bool isLaserOn() const noexcept { return laserOn_; }

private:
    void command(const std::string& gcode);
    void refresh();
    void fillSpindles();
    void applySpeed();
    void applyPower();
    bool canClick() const;  // connected, no job, idle

    Machine& machine_;
    QPushButton* spindleMode_;
    QPushButton* laserMode_;
    QComboBox* spindleSelect_;
    QGroupBox* spindleBox_;
    QSlider* speedSlider_;
    QDoubleSpinBox* speed_;
    QGroupBox* laserBox_;
    QSlider* powerSlider_;
    QDoubleSpinBox* power_;
    QDoubleSpinBox* duration_;
    QLabel* state_;
    QList<QPushButton*> buttons_;  // need an idle machine
    QList<QPushButton*> stops_;    // work whenever connected
    QTimer* speedTimer_;
    QTimer* powerTimer_;
    bool spindleOn_ = false;
    bool laserOn_ = false;
    bool syncing_ = false;
};

// Feed and spindle override sliders (10-200 %) and the rapid presets, sent as
// realtime bytes; the sliders follow the overrides the firmware reports.
class OverridesBar final : public QWidget {
    Q_OBJECT
public:
    explicit OverridesBar(Machine& machine, QWidget* parent = nullptr);

private:
    void refresh();

    Machine& machine_;
    QSlider* feed_;
    QSlider* spindle_;
    QLabel* feedLabel_;
    QLabel* spindleLabel_;
    QList<QPushButton*> rapid_;
    bool dragging_ = false;
};

class MacrosPanel final : public QWidget {
    Q_OBJECT
public:
    explicit MacrosPanel(Machine& machine, QWidget* parent = nullptr);

    // Import/Export (gSender's JSON: [{name, content, description, id}]):
    // false with the reason in `message`, else what was done.
    bool importFrom(const QString& path, QString* message);
    bool exportTo(const QString& path, QString* message);

private:
    void reload();
    void refresh();
    void edit(bool create);

    Machine& machine_;
    QListWidget* list_;
    QPushButton* run_;
    QPushButton* add_;
    QPushButton* editButton_;
    QPushButton* remove_;
};

class MacroDialog final : public QDialog {
    Q_OBJECT
public:
    explicit MacroDialog(QWidget* parent = nullptr);
    void setValues(const QString& name, const QString& content, const QString& description);
    QString name() const;
    QString content() const;
    QString description() const;

private:
    QLineEdit* name_;
    QPlainTextEdit* content_;
    QLineEdit* description_;
};

}  // namespace gs::app
