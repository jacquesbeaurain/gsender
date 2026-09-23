#pragma once

// Spindle/coolant controls, job overrides and the macro list (gSender's
// Spindle/Laser, Coolant and Macros widgets and the job overrides).

#include <QDialog>
#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSlider;

namespace gs::app {

class Machine;

class SpindlePanel final : public QWidget {
    Q_OBJECT
public:
    explicit SpindlePanel(Machine& machine, QWidget* parent = nullptr);

private:
    void command(const QString& gcode);
    void refresh();

    Machine& machine_;
    QDoubleSpinBox* speed_;
    QLabel* state_;
    QList<QPushButton*> buttons_;
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
