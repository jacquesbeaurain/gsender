#pragma once

// The main window's panels. Each reads the machine through Machine and sends
// commands to its controller; none holds protocol logic of its own.

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QButtonGroup;

namespace gs::app {

class Jogger;
class Machine;

// Port / baud selection and connect/disconnect (gSender's Connection widget).
class ConnectionBar final : public QWidget {
    Q_OBJECT
public:
    explicit ConnectionBar(Machine& machine, QWidget* parent = nullptr);
    void refreshPorts();

private:
    void toggleConnection();
    void updateState();

    Machine& machine_;
    QComboBox* ports_;
    QComboBox* baud_;
    QPushButton* refresh_;
    QPushButton* connect_;
    QLabel* state_;
};

// Work and machine positions with zeroing, homing and unlocking (DRO widget).
class PositionPanel final : public QWidget {
    Q_OBJECT
public:
    explicit PositionPanel(Machine& machine, QWidget* parent = nullptr);

private:
    void refresh();

    Machine& machine_;
    QLabel* work_[4];
    QLabel* machinePos_[4];
    QPushButton* zero_[4];
    QWidget* rowA_[4];
    QPushButton* units_;
    QList<QPushButton*> actions_;
};

// Step jogging on click, continuous jogging while held, with the speed
// presets (Jogging widget). The keyboard shortcuts share the same Jogger.
class JogPanel final : public QWidget {
    Q_OBJECT
public:
    JogPanel(Machine& machine, Jogger& jogger, QWidget* parent = nullptr);

private:
    QPushButton* jogButton(const QString& text, char axis, int direction);
    void showSpeeds();
    void updateEnabled();

    Machine& machine_;
    Jogger& jogger_;
    QButtonGroup* presets_;
    QDoubleSpinBox* xyStep_;
    QDoubleSpinBox* zStep_;
    QDoubleSpinBox* feed_;
    QList<QPushButton*> buttons_;
    bool showing_ = false;
};

// Firmware output and a command line (Console widget).
class ConsolePanel final : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePanel(Machine& machine, QWidget* parent = nullptr);
    void append(const QString& text, bool fromHost);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void submit();

    Machine& machine_;
    QPlainTextEdit* output_;
    QLineEdit* input_;
    QStringList history_;
    int historyIndex_ = 0;
};

// Load/run/pause/stop with progress and the file's statistics (Job panel).
class JobPanel final : public QWidget {
    Q_OBJECT
public:
    explicit JobPanel(Machine& machine, QWidget* parent = nullptr);

private:
    void openFile();
    void refresh();
    void updateProgress();

    Machine& machine_;
    QLabel* info_;
    QPushButton* open_;
    QPushButton* unload_;
    QPushButton* start_;
    QPushButton* fromLine_;
    QPushButton* outline_;
    QPushButton* pause_;
    QPushButton* resume_;
    QPushButton* stop_;
    QProgressBar* progress_;
    QLabel* timing_;
};

}  // namespace gs::app
