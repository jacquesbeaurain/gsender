#include "gcode_editor_dialog.hpp"

#include "machine.hpp"

#include "gs/job/program_analysis.hpp"
#include "gs/util/strings.hpp"

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace gs::app {
namespace {

constexpr int kMaxHighlightedMatches = 5000;

class GutterArea final : public QWidget {
public:
    explicit GutterArea(GcodeTextEdit* editor) : QWidget(editor), editor_(editor) {}
    QSize sizeHint() const override { return {editor_->gutterWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent* event) override { editor_->paintGutter(event); }

private:
    GcodeTextEdit* editor_;
};

QString chip(const QString& text, const char* background, const char* foreground) {
    return QString("<span style='background:%1;color:%2'>&nbsp;%3&nbsp;</span>")
        .arg(background, foreground, text.toHtmlEscaped());
}

}  // namespace

// ---- the text ------------------------------------------------------------------------------

GcodeTextEdit::GcodeTextEdit(QWidget* parent) : QPlainTextEdit(parent) {
    gutter_ = new GutterArea(this);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { setViewportMargins(gutterWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
        if (dy != 0) {
            gutter_->scroll(0, dy);
        } else {
            gutter_->update(0, rect.y(), gutter_->width(), rect.height());
        }
    });
    setViewportMargins(gutterWidth(), 0, 0, 0);
}

int GcodeTextEdit::gutterWidth() const {
    const int digits = static_cast<int>(QString::number(std::max(1, blockCount())).size());
    return 18 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void GcodeTextEdit::setProgress(std::size_t first, std::size_t last) {
    if (first == runningFirst_ && last == runningLast_) {
        return;
    }
    runningFirst_ = first;
    runningLast_ = last;
    gutter_->update();
}

void GcodeTextEdit::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect contents = contentsRect();
    gutter_->setGeometry(QRect(contents.left(), contents.top(), gutterWidth(), contents.height()));
}

void GcodeTextEdit::paintGutter(QPaintEvent* event) {
    QPainter painter(gutter_);
    painter.fillRect(event->rect(), palette().color(QPalette::AlternateBase));
    // While a job runs: done green, running yellow, to come blue.
    const QColor done(34, 197, 94, 70);
    const QColor running(234, 179, 8, 150);
    const QColor coming(59, 130, 246, 40);
    QTextBlock block = firstVisibleBlock();
    std::size_t line = static_cast<std::size_t>(block.blockNumber()) + 1;
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const QColor number = palette().color(QPalette::PlaceholderText);
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            if (runningFirst_ > 0) {
                const QColor& band = line < runningFirst_ ? done : line <= runningLast_ ? running : coming;
                painter.fillRect(0, top, gutter_->width(), bottom - top, band);
            }
            painter.setPen(line >= runningFirst_ && line <= runningLast_ && runningFirst_ > 0
                               ? palette().color(QPalette::Text)
                               : number);
            painter.drawText(0, top, gutter_->width() - 8, fontMetrics().height(), Qt::AlignRight,
                             QString::number(line));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++line;
    }
}

// ---- the dialog ---------------------------------------------------------------------------

GcodeEditorDialog::GcodeEditorDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("G-code Editor"));
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    resize(920, 720);
    auto* layout = new QVBoxLayout(this);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(QString("<b>%1</b>").arg(tr("G-code Editor")));
    status_ = new QLabel;
    selected_ = new QLabel;
    lines_ = new QLabel;
    lines_->setStyleSheet("color:palette(mid)");
    header->addWidget(title);
    header->addWidget(status_);
    header->addWidget(selected_);
    header->addWidget(lines_);
    header->addStretch(1);
    layout->addLayout(header);

    editor_ = new GcodeTextEdit;
    layout->addWidget(editor_, 1);

    searchRow_ = new QWidget;
    auto* searchLayout = new QHBoxLayout(searchRow_);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search..."));
    matchLabel_ = new QLabel(QStringLiteral("0/0"));
    previous_ = new QPushButton(tr("Previous"));
    previous_->setToolTip(tr("Previous match (Shift+Enter)"));
    next_ = new QPushButton(tr("Next"));
    next_->setToolTip(tr("Next match (Enter)"));
    auto* closeSearch = new QToolButton;
    closeSearch->setText(QString::fromUtf8("✕"));
    closeSearch->setToolTip(tr("Close search"));
    searchLayout->addWidget(search_, 1);
    searchLayout->addWidget(matchLabel_);
    searchLayout->addWidget(previous_);
    searchLayout->addWidget(next_);
    searchLayout->addWidget(closeSearch);
    searchRow_->hide();
    layout->addWidget(searchRow_);

    jumpRow_ = new QWidget;
    auto* jumpLayout = new QHBoxLayout(jumpRow_);
    jumpLayout->setContentsMargins(0, 0, 0, 0);
    jump_ = new QSpinBox;
    jump_->setRange(1, 1);
    auto* go = new QPushButton(tr("Go"));
    go->setToolTip(tr("Jump To Line"));
    auto* closeJump = new QToolButton;
    closeJump->setText(QString::fromUtf8("✕"));
    closeJump->setToolTip(tr("Close jump to line"));
    jumpLayout->addWidget(new QLabel(tr("Jump to line:")));
    jumpLayout->addWidget(jump_);
    jumpLayout->addWidget(go);
    jumpLayout->addWidget(closeJump);
    jumpLayout->addStretch(1);
    jumpRow_->hide();
    layout->addWidget(jumpRow_);

    auto* toolbar = new QHBoxLayout;
    jumpToggle_ = new QToolButton;
    jumpToggle_->setText(tr("Jump to Line"));
    jumpToggle_->setCheckable(true);
    searchToggle_ = new QToolButton;
    searchToggle_->setText(tr("Search"));
    searchToggle_->setToolTip(tr("Search (Ctrl+F)"));
    searchToggle_->setCheckable(true);
    selectAll_ = new QPushButton(tr("Select All"));
    copy_ = new QPushButton(tr("Copy"));
    delete_ = new QPushButton(tr("Delete Lines"));
    revert_ = new QPushButton(tr("Revert"));
    revert_->setToolTip(tr("Revert to original content"));
    save_ = new QPushButton(tr("Save"));
    save_->setToolTip(tr("Save changes: the edited G-code becomes the job"));
    save_->setStyleSheet("font-weight:600");
    toolbar->addWidget(jumpToggle_);
    toolbar->addWidget(searchToggle_);
    toolbar->addWidget(selectAll_);
    toolbar->addWidget(copy_);
    toolbar->addWidget(delete_);
    toolbar->addStretch(1);
    toolbar->addWidget(revert_);
    toolbar->addWidget(save_);
    layout->addLayout(toolbar);
    // Enter belongs to the search and jump fields, not a default button.
    for (QPushButton* button : findChildren<QPushButton*>()) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }

    connect(editor_->document(), &QTextDocument::contentsChanged, this, [this] {
        if (!loading_) {
            findMatches();
            refresh();
        }
    });
    connect(editor_->document(), &QTextDocument::modificationChanged, this, &GcodeEditorDialog::refresh);
    connect(editor_, &QPlainTextEdit::selectionChanged, this, &GcodeEditorDialog::refresh);
    connect(search_, &QLineEdit::textChanged, this, &GcodeEditorDialog::setSearch);
    connect(previous_, &QPushButton::clicked, this, &GcodeEditorDialog::previousMatch);
    connect(next_, &QPushButton::clicked, this, &GcodeEditorDialog::nextMatch);
    connect(searchToggle_, &QToolButton::toggled, this, [this](bool open) {
        searchRow_->setVisible(open);
        if (open) {
            search_->setFocus();
            search_->selectAll();
        } else {
            search_->clear();
        }
    });
    connect(closeSearch, &QToolButton::clicked, this, [this] { searchToggle_->setChecked(false); });
    connect(jumpToggle_, &QToolButton::toggled, this, [this](bool open) {
        jumpRow_->setVisible(open);
        if (open) {
            jump_->setFocus();
            jump_->selectAll();
        }
    });
    connect(closeJump, &QToolButton::clicked, this, [this] { jumpToggle_->setChecked(false); });
    connect(go, &QPushButton::clicked, this, [this] { jumpToLine(jump_->value()); });
    connect(selectAll_, &QPushButton::clicked, this, &GcodeEditorDialog::toggleSelectAll);
    connect(copy_, &QPushButton::clicked, this, &GcodeEditorDialog::copy);
    connect(delete_, &QPushButton::clicked, this, &GcodeEditorDialog::deleteSelectedLines);
    connect(revert_, &QPushButton::clicked, this, &GcodeEditorDialog::revert);
    connect(save_, &QPushButton::clicked, this, &GcodeEditorDialog::save);
    auto* find = new QShortcut(QKeySequence::Find, this);
    connect(find, &QShortcut::activated, this, [this] {
        if (searchToggle_->isChecked()) {
            search_->setFocus();
            search_->selectAll();
        } else {
            searchToggle_->setChecked(true);
        }
    });
    // The job: the text follows it while it runs. The file going closes
    // the editor; another file replaces the text (unsaved edits go, as
    // upstream).
    connect(&machine_, &Machine::senderStatusChanged, this, &GcodeEditorDialog::followJob);
    connect(&machine_, &Machine::workflowChanged, this, &GcodeEditorDialog::followJob);
    connect(&machine_, &Machine::programChanged, this, [this] {
        if (!machine_.hasProgram()) {
            done(QDialog::Rejected);
        } else if (machine_.programText() != source_) {
            load();
        }
    });
    load();
}

void GcodeEditorDialog::load() {
    // One line per block: CR LF and lone CR endings become LF.
    const std::string& program = machine_.programText();
    source_ = program;
    QStringList lines;
    for (const std::string_view line : str::splitLines(program)) {
        lines << QString::fromUtf8(line.data(), static_cast<qsizetype>(line.size()));
    }
    original_ = lines.join('\n');
    senderLines_ = job::senderLineNumbers(program);
    loading_ = true;
    editor_->setPlainText(original_);
    loading_ = false;
    editor_->document()->setModified(false);
    findMatches();
    followJob();
    refresh();
}

QString GcodeEditorDialog::text() const {
    return editor_->toPlainText();
}

bool GcodeEditorDialog::hasChanges() const {
    return editor_->document()->isModified();
}

bool GcodeEditorDialog::jobRunning() const {
    controller::Controller* c = machine_.controller();
    return c && (c->workflow().state() == controller::WorkflowState::Running ||
                 c->workflow().state() == controller::WorkflowState::Paused);
}

bool GcodeEditorDialog::save() {
    if (jobRunning()) {
        Q_EMIT machine_.notice(tr("Cannot save while job is running"));
        return false;
    }
    if (!hasChanges()) {
        return false;
    }
    const QString edited = text();
    original_ = edited;
    source_ = edited.toStdString();
    senderLines_ = job::senderLineNumbers(source_);
    editor_->document()->setModified(false);
    machine_.loadProgram(machine_.programName(), source_, machine_.programPath());
    Q_EMIT machine_.successNotice(tr("G-code saved successfully"));
    refresh();
    return true;
}

bool GcodeEditorDialog::revert() {
    if (jobRunning()) {
        Q_EMIT machine_.notice(tr("Cannot revert while job is running"));
        return false;
    }
    if (!hasChanges()) {
        return false;
    }
    loading_ = true;
    editor_->setPlainText(original_);
    loading_ = false;
    editor_->document()->setModified(false);
    findMatches();
    refresh();
    Q_EMIT machine_.notice(tr("Reverted to original content"));
    return true;
}

void GcodeEditorDialog::setSearch(const QString& query) {
    if (search_->text() != query) {
        const QSignalBlocker block(search_);
        search_->setText(query);
    }
    findMatches();
    showMatch();
}

void GcodeEditorDialog::findMatches() {
    matches_.clear();
    const QString query = search_->text().trimmed();
    if (!query.isEmpty()) {
        for (QTextBlock block = editor_->document()->begin(); block.isValid(); block = block.next()) {
            if (block.text().contains(query, Qt::CaseInsensitive)) {
                matches_.push_back(block.blockNumber());
            }
        }
    }
    currentMatch_ = matches_.empty() ? -1 : 0;
    matchLabel_->setText(matches_.empty() ? QStringLiteral("0/0")
                                          : QString("%1/%2").arg(currentMatch_ + 1).arg(matches_.size()));
    previous_->setEnabled(!matches_.empty());
    next_->setEnabled(!matches_.empty());
    updateHighlights();
}

void GcodeEditorDialog::nextMatch() {
    if (matches_.empty()) {
        return;
    }
    currentMatch_ = currentMatch_ < matchCount() - 1 ? currentMatch_ + 1 : 0;
    showMatch();
}

void GcodeEditorDialog::previousMatch() {
    if (matches_.empty()) {
        return;
    }
    currentMatch_ = currentMatch_ > 0 ? currentMatch_ - 1 : matchCount() - 1;
    showMatch();
}

void GcodeEditorDialog::showMatch() {
    matchLabel_->setText(matches_.empty() ? QStringLiteral("0/0")
                                          : QString("%1/%2").arg(currentMatch_ + 1).arg(matches_.size()));
    if (currentMatch_ >= 0) {
        jumpToLine(matches_[static_cast<std::size_t>(currentMatch_)] + 1);
    }
    updateHighlights();
}

void GcodeEditorDialog::jumpToLine(int line) {
    const QTextBlock block = editor_->document()->findBlockByNumber(std::clamp(line, 1, editor_->blockCount()) - 1);
    if (!block.isValid()) {
        return;
    }
    QTextCursor cursor(block);
    editor_->setTextCursor(cursor);
    // Centred when it was out of view.
    if (!editor_->viewport()->rect().contains(editor_->cursorRect())) {
        editor_->centerCursor();
    }
}

int GcodeEditorDialog::selectedLineCount() const {
    const QTextCursor cursor = editor_->textCursor();
    if (!cursor.hasSelection()) {
        return 0;
    }
    const QTextBlock first = editor_->document()->findBlock(cursor.selectionStart());
    QTextBlock last = editor_->document()->findBlock(cursor.selectionEnd());
    // A selection that stops at the start of a line does not take that line.
    if (last.blockNumber() > first.blockNumber() && cursor.selectionEnd() == last.position()) {
        last = last.previous();
    }
    return last.blockNumber() - first.blockNumber() + 1;
}

void GcodeEditorDialog::selectLines(int first, int last) {
    QTextDocument* document = editor_->document();
    const QTextBlock from = document->findBlockByNumber(std::max(first, 1) - 1);
    const QTextBlock to = document->findBlockByNumber(std::clamp(last, first, editor_->blockCount()) - 1);
    if (!from.isValid() || !to.isValid()) {
        return;
    }
    // Whole lines: to the start of the next one (an empty line has nothing
    // else to select), or the end of the text.
    QTextCursor cursor(document);
    cursor.setPosition(from.position());
    cursor.setPosition(to.next().isValid() ? to.next().position() : to.position() + to.length() - 1,
                       QTextCursor::KeepAnchor);
    editor_->setTextCursor(cursor);
}

void GcodeEditorDialog::toggleSelectAll() {
    if (selectedLineCount() == editor_->blockCount()) {
        QTextCursor cursor = editor_->textCursor();
        cursor.clearSelection();
        editor_->setTextCursor(cursor);
    } else {
        editor_->selectAll();
    }
}

void GcodeEditorDialog::deleteSelectedLines() {
    const int count = selectedLineCount();
    if (jobRunning() || count == 0) {
        return;
    }
    QTextDocument* document = editor_->document();
    const QTextBlock first = document->findBlock(editor_->textCursor().selectionStart());
    const QTextBlock last = document->findBlockByNumber(first.blockNumber() + count - 1);
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    if (last.next().isValid()) {
        // The lines and the line break after them.
        cursor.setPosition(first.position());
        cursor.setPosition(last.next().position(), QTextCursor::KeepAnchor);
    } else if (first.previous().isValid()) {
        // The last lines: the break before them.
        cursor.setPosition(first.previous().position() + first.previous().length() - 1);
        cursor.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    } else {
        cursor.select(QTextCursor::Document);
    }
    cursor.removeSelectedText();
    cursor.endEditBlock();
    editor_->setTextCursor(cursor);
}

QString GcodeEditorDialog::copyText() const {
    const int count = selectedLineCount();
    if (count == 0) {
        return text();
    }
    QStringList lines;
    QTextBlock block = editor_->document()->findBlock(editor_->textCursor().selectionStart());
    for (int i = 0; i < count && block.isValid(); ++i, block = block.next()) {
        lines << block.text();
    }
    return lines.join('\n');
}

void GcodeEditorDialog::copy() {
    if (jobRunning()) {
        return;
    }
    const int count = selectedLineCount();
    QGuiApplication::clipboard()->setText(copyText());
    Q_EMIT machine_.notice(count > 0 ? tr("%1 line(s) copied to clipboard").arg(count)
                                     : tr("G-code has been copied to your clipboard"));
}

void GcodeEditorDialog::followJob() {
    std::size_t first = 0;
    std::size_t last = 0;
    controller::Controller* c = machine_.controller();
    if (jobRunning() && c && !senderLines_.empty()) {
        // The sender's running line, and the two after it, as file lines
        // (upstream compared the sender's count with file lines directly,
        // drifting by the blank lines before it).
        const std::int64_t running = c->sender().currentLineRunning();
        if (running >= 0) {
            const std::size_t at = std::min(static_cast<std::size_t>(running), senderLines_.size() - 1);
            first = senderLines_[at];
            last = senderLines_[std::min(at + 2, senderLines_.size() - 1)];
        }
    }
    const bool moved = first != runningLine_;
    runningLine_ = first;
    editor_->setProgress(first, last);
    if (moved && first > 0) {
        const QTextBlock block = editor_->document()->findBlockByNumber(static_cast<int>(first) - 1);
        QTextCursor cursor(block);
        editor_->setTextCursor(cursor);
        if (!editor_->viewport()->rect().contains(editor_->cursorRect())) {
            editor_->centerCursor();
        }
    }
    if (moved) {
        updateHighlights();
    }
    refresh();
}

void GcodeEditorDialog::updateHighlights() {
    QList<QTextEdit::ExtraSelection> selections;
    const auto line = [this, &selections](int block, const QColor& color) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(color);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = QTextCursor(editor_->document()->findBlockByNumber(block));
        selections.append(selection);
    };
    if (runningLine_ > 0) {
        line(static_cast<int>(runningLine_) - 1, QColor(234, 179, 8, 90));
    }
    const int shown = std::min(matchCount(), kMaxHighlightedMatches);
    for (int i = 0; i < shown; ++i) {
        line(matches_[static_cast<std::size_t>(i)], i == currentMatch_ ? QColor(250, 204, 21, 170)
                                                                        : QColor(250, 204, 21, 60));
    }
    if (currentMatch_ >= kMaxHighlightedMatches) {
        line(matches_[static_cast<std::size_t>(currentMatch_)], QColor(250, 204, 21, 170));
    }
    editor_->setExtraSelections(selections);
}

void GcodeEditorDialog::refresh() {
    const bool running = jobRunning();
    editor_->setReadOnly(running);
    controller::Controller* c = machine_.controller();
    const bool paused = c && c->workflow().state() == controller::WorkflowState::Paused;
    status_->setText(running ? (paused ? chip(tr("Paused"), "#fef3c7", "#a16207") : chip(tr("Running"), "#dcfce7", "#15803d"))
                             : QString());
    status_->setVisible(running);
    const int selected = selectedLineCount();
    selected_->setText(chip(tr("%1 selected").arg(selected), "#dbeafe", "#1d4ed8"));
    selected_->setVisible(selected > 0);
    lines_->setText(tr("%1 lines").arg(QLocale().toString(editor_->blockCount())));
    jump_->setRange(1, std::max(1, editor_->blockCount()));
    const bool all = selected > 0 && selected == editor_->blockCount();
    selectAll_->setText(all ? tr("Deselect All") : tr("Select All"));
    selectAll_->setEnabled(!running);
    copy_->setEnabled(!running);
    copy_->setToolTip(running ? tr("Copy disabled while job is running")
                              : selected > 0 ? tr("Copy %1 selected line(s)").arg(selected)
                                             : tr("Copy all lines"));
    delete_->setEnabled(!running && selected > 0);
    delete_->setToolTip(running ? tr("Delete disabled while job is running")
                                : tr("Delete %1 selected line(s)").arg(selected));
    const bool changed = hasChanges();
    revert_->setEnabled(changed && !running);
    save_->setEnabled(changed && !running);
}

void GcodeEditorDialog::keyPressEvent(QKeyEvent* event) {
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (enter && search_->hasFocus()) {
        if (event->modifiers() & Qt::ShiftModifier) {
            previousMatch();
        } else {
            nextMatch();
        }
        return;
    }
    if (enter && jump_->hasFocus()) {
        jumpToLine(jump_->value());
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        // The search first, then the selection, then the editor.
        if (searchToggle_->isChecked()) {
            searchToggle_->setChecked(false);
            return;
        }
        if (editor_->textCursor().hasSelection()) {
            QTextCursor cursor = editor_->textCursor();
            cursor.clearSelection();
            editor_->setTextCursor(cursor);
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

void GcodeEditorDialog::reject() {
    if (hasChanges() && isVisible() &&
        QMessageBox::question(this, tr("G-code Editor"), tr("Discard your changes to the G-code?")) != QMessageBox::Yes) {
        return;
    }
    QDialog::reject();
}

}  // namespace gs::app
