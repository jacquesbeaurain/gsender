#pragma once

// gSender's G-code Editor (Visualizer/GcodeEditor): the loaded file's lines,
// edited in place and saved back as the job. While the job runs the text is
// read-only and follows the line running: the gutter shows the lines done,
// running and to come.

#include <QDialog>
#include <QPlainTextEdit>
#include <QString>

#include <cstddef>
#include <string>
#include <vector>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace gs::app {

class Machine;

// The text with a line-number gutter that shows the job's progress.
class GcodeTextEdit final : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit GcodeTextEdit(QWidget* parent = nullptr);

    // 1-based file lines: before `first` done, `first`..`last` running, the
    // rest to come; 0 when no job runs.
    void setProgress(std::size_t first, std::size_t last);
    int gutterWidth() const;
    void paintGutter(QPaintEvent* event);

    // Syntax colouring (gcode_highlighter): on, the lines are coloured as
    // they come into view; off - while a job runs, as upstream - plain.
    void setHighlighting(bool enabled, bool dark);
    bool highlighting() const noexcept { return highlight_; }
    // Whether the block's colours are current (tests).
    bool isColoured(int block) const;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    // Each coloured block's userState holds its revision when coloured, so
    // an edited block is coloured again; -1 means plain.
    void colourVisibleBlocks();
    void clearColours();

    QWidget* gutter_;
    bool highlight_ = true;
    bool dark_ = false;
    std::size_t runningFirst_ = 0;
    std::size_t runningLast_ = 0;
};

class GcodeEditorDialog final : public QDialog {
    Q_OBJECT
public:
    explicit GcodeEditorDialog(Machine& machine, QWidget* parent = nullptr);

    QString text() const;
    bool hasChanges() const;
    bool jobRunning() const;
    // "Save changes": the edited text becomes the job (same name); not while
    // a job runs or with nothing changed.
    bool save();
    // "Revert to original content".
    bool revert();

    // Search: the lines containing the query, any case; the current match is
    // selected and shown. Enter / Shift+Enter step through them, wrapping.
    void setSearch(const QString& query);
    int matchCount() const noexcept { return static_cast<int>(matches_.size()); }
    int currentMatch() const noexcept { return currentMatch_; }  // 0-based, -1 for none
    void nextMatch();
    void previousMatch();
    // "Jump to line" (1-based).
    void jumpToLine(int line);

    // The lines the selection covers (1-based first/last), or none.
    int selectedLineCount() const;
    void selectLines(int first, int last);
    void toggleSelectAll();
    void deleteSelectedLines();
    // What Copy puts on the clipboard: the selected lines, else everything.
    QString copyText() const;
    void copy();

    // The 1-based file line the running job is on, or 0.
    std::size_t runningLine() const noexcept { return runningLine_; }

    GcodeTextEdit& editor() noexcept { return *editor_; }

    // Closing (Escape, the close button) with unsaved changes asks first.
    void reject() override;

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void load();
    void refresh();
    void findMatches();
    void showMatch();
    void updateHighlights();
    void followJob();

    Machine& machine_;
    std::string source_;  // the job's text the editor holds
    QString original_;
    std::vector<std::size_t> senderLines_;  // file line of each sender line
    std::vector<int> matches_;              // 0-based blocks
    int currentMatch_ = -1;
    std::size_t runningLine_ = 0;
    bool loading_ = false;

    QLabel* status_;
    QLabel* selected_;
    QLabel* lines_;
    GcodeTextEdit* editor_;
    QWidget* searchRow_;
    QLineEdit* search_;
    QLabel* matchLabel_;
    QPushButton* previous_;
    QPushButton* next_;
    QWidget* jumpRow_;
    QSpinBox* jump_;
    QToolButton* jumpToggle_;
    QToolButton* searchToggle_;
    QPushButton* selectAll_;
    QPushButton* copy_;
    QPushButton* delete_;
    QPushButton* revert_;
    QPushButton* save_;
};

}  // namespace gs::app
