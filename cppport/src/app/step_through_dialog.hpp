#pragma once

// gSender's G-code Step Through (features/GcodeStepper): the loaded file line
// by line - its source, the toolpath up to the line with the cutter where
// the line leaves it, the tools, the position and the modal state - stepped,
// scrubbed or played back.

#include "toolpath_view.hpp"

#include "gs/job/step_through.hpp"

#include <QDialog>
#include <QElapsedTimer>
#include <QPen>
#include <QString>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

class QButtonGroup;
class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSlider;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace gs::app {

class Machine;
class SourceLineDelegate;
class StepSourceModel;

// The loaded job's toolpath as the stepper shows it: the lines before the
// current one drawn as done (grey) or left out, each tool's cuts in its
// colour (hidden tools left out), the cutter where the current line leaves
// it.
class StepThroughView final : public ToolpathCanvas {
    Q_OBJECT
public:
    explicit StepThroughView(Machine& machine, QWidget* parent = nullptr);

    // Sender lines below `done` have run.
    void seekTo(const gcode::Vec4& position, std::size_t done, bool hideProcessed);
    // The tools' sender-line spans [first, end): their colour, or hidden.
    struct ToolSpan {
        std::size_t first = 0;
        std::size_t end = 0;
        QColor color;
        bool hidden = false;
    };
    void setTools(std::vector<ToolSpan> spans);
    void setOverlay(const QString& text);  // "Reading positions... n%"

protected:
    std::optional<gcode::BoundingBox> contentBounds() const override;
    void paintEvent(QPaintEvent* event) override;

private:
    const ToolSpan* spanFor(std::uint32_t line) const;
    bool rotaryJob() const;

    Machine& machine_;
    gcode::Vec4 position_;
    std::size_t done_ = 0;
    bool hideProcessed_ = false;
    std::vector<ToolSpan> spans_;
    std::vector<QPen> spanPens_;
    QString overlay_;
};

class StepThroughDialog final : public QDialog {
    Q_OBJECT
public:
    explicit StepThroughDialog(Machine& machine, QWidget* parent = nullptr);
    ~StepThroughDialog() override;

    // goToLine(): the one piece of navigation state, clamped to the file;
    // any manual move stops playback.
    void goToLine(std::size_t line);
    std::size_t currentLine() const noexcept { return line_; }
    std::size_t totalLines() const noexcept { return lines_.size(); }
    // The line index is built in the background after opening.
    bool indexReady() const noexcept { return indexReady_; }
    const job::StepIndex& index() const noexcept { return index_; }
    const std::vector<job::StepperTool>& tools() const noexcept { return tools_; }
    int activeTool() const;

    // The step buttons (+-100, +-1000 lines), Reset, and playback: from the
    // start again when at the end, at the file's estimated pace times the
    // speed (0.5x, 1x, 10x or 100x).
    void step(int delta);
    void reset();
    void togglePlay();
    bool isPlaying() const noexcept { return playing_; }
    void setPlaybackSpeed(double speed);
    double playbackSpeed() const noexcept { return speed_; }

    // A tool's eye button: its paths hidden or shown.
    void toggleTool(int index);
    bool toolHidden(int index) const;
    // "Hide prior lines" / "Show prior lines": done lines left out or grey.
    void setHideProcessed(bool hide);
    bool hideProcessed() const noexcept { return hideProcessed_; }

    // The source panel's search: matching lines (at most 5000); Enter goes
    // to the next one after the current line, wrapping.
    void setSearch(const QString& query);
    std::size_t matchCount() const noexcept { return matches_.size(); }
    void goToNextMatch();

    // The status readouts at the current line.
    QString positionText() const;
    job::StepModalReadout modalReadout() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;  // tool card clicks

private:
    void load();
    void loadTools();
    void buildIndex();
    void indexBuilt(std::uint64_t generation, std::shared_ptr<job::StepIndex> index);
    void setLine(double line);  // playback and scrubbing: keeps playing
    void stopPlaying();
    void tick();
    void refresh();
    void refreshTools();
    void refreshReadout();
    void updateViewTools();
    void scrollSourceToCurrent();

    Machine& machine_;
    std::string text_;
    std::vector<std::string> lines_;
    job::StepIndex index_;
    bool indexReady_ = false;
    std::uint64_t generation_ = 0;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::vector<job::StepperTool> tools_;
    std::vector<bool> hiddenTools_;
    std::vector<std::size_t> matches_;
    std::size_t line_ = 1;
    bool hideProcessed_ = false;
    bool scrubbing_ = false;
    bool playing_ = false;
    double speed_ = 10;
    double playStartLine_ = 1;
    QElapsedTimer playClock_;
    QTimer* playTimer_;

    // source
    QLabel* sourceTitle_;
    QToolButton* searchToggle_;
    QWidget* searchRow_;
    QLineEdit* search_;
    QLabel* matchCount_;
    QListView* source_;
    SourceLineDelegate* delegate_;
    StepSourceModel* model_;
    // view
    StepThroughView* view_;
    QLabel* lineNumber_;
    QLabel* lineText_;
    QFrame* lineReadout_;
    // tools
    QVBoxLayout* toolList_;
    QWidget* toolsEmpty_;
    std::vector<QFrame*> toolCards_;
    std::vector<QToolButton*> toolEyes_;
    int shownActiveTool_ = -2;
    // scrubber and controls
    QSlider* scrubber_;
    QLabel* quarterLabels_[5];
    QLabel* position_;
    QPushButton* play_;
    QPushButton* reset_;
    QButtonGroup* speeds_;
    QPushButton* steps_[4];
    // status
    QLabel* positionTitle_;
    QLabel* positionValues_;
    QLabel* modalCells_[11];
    QPushButton* hideProcessedButton_;
};

}  // namespace gs::app
