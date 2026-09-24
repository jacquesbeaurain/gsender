#include "console_panel.hpp"

#include "machine.hpp"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QEvent>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

namespace gs::app {
namespace {

constexpr int kCopyLimit = 50;  // COPY_HISTORY_LIMIT

// MessageIcon and the row text: marks and colours per type, always on the
// console's dark surface. What was sent reads bright, what came back sits
// back, and colour is kept for what needs attention.
struct Style {
    const char* mark;
    const char* markColour;
    const char* textColour;
    bool bold;
};

const Style& styleOf(ConsoleType type) {
    static const Style kStyles[] = {
        {"›", "#93c5fd", "#f3f4f6", false},  // G-code sent
        {"·", "#9ca3af", "#9ca3af", false},  // response
        {"⚙", "#86efac", "#86efac", false},  // system
        {"⚠", "#fdba74", "#fdba74", false},  // warning
        {"⊗", "#facc15", "#facc15", false},  // error
        {"⛔", "#ef4444", "#ef4444", true},   // alarm
    };
    return kStyles[static_cast<int>(type)];
}

}  // namespace

ConsolePanel::ConsolePanel(Machine& machine, bool popout, QWidget* parent)
    : QWidget(parent), machine_(machine), log_(machine.consoleLog()) {
    auto* box = new QGroupBox(tr("Console"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(box);
    auto* layout = new QVBoxLayout(box);

    // The toolbar: the filters, then copy, clear and pop out.
    auto* toolbar = new QHBoxLayout;
    filters_ = new QButtonGroup(this);
    filters_->setExclusive(true);
    static const std::pair<ConsoleFilter, const char*> kFilters[] = {
        {ConsoleFilter::All, QT_TR_NOOP("All")},
        {ConsoleFilter::Gcode, QT_TR_NOOP("G-code")},
        {ConsoleFilter::Response, QT_TR_NOOP("Responses")},
        {ConsoleFilter::System, QT_TR_NOOP("System")},
        {ConsoleFilter::Faults, QT_TR_NOOP("Faults")},
    };
    // The dots echo the rows' marks; Faults takes the most severe colour.
    static const char* const kDots[] = {nullptr, "#3b82f6", "#9ca3af", "#22c55e", "#dc2626"};
    for (const auto& [id, label] : kFilters) {
        auto* button = new QToolButton;
        button->setText(tr(label));
        if (const char* dot = kDots[static_cast<int>(id)]) {
            QPixmap pixmap(10, 10);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(dot));
            painter.drawEllipse(QRectF(1, 1, 8, 8));
            painter.end();
            button->setIcon(QIcon(pixmap));
            button->setIconSize(QSize(10, 10));
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setObjectName(QString("consoleFilter%1").arg(static_cast<int>(id)));
        button->setToolTip(tr("Show %1").arg(tr(label).toLower()));
        filters_->addButton(button, static_cast<int>(id));
        toolbar->addWidget(button);
    }
    filters_->button(static_cast<int>(ConsoleFilter::All))->setChecked(true);
    connect(filters_, &QButtonGroup::idClicked, this, [this](int id) { setFilter(static_cast<ConsoleFilter>(id)); });
    toolbar->addStretch(1);
    const auto action = [this, toolbar](const QString& text, const QString& tip, const char* name) {
        auto* button = new QToolButton;
        button->setText(text);
        button->setToolTip(tip);
        button->setObjectName(name);
        button->setAutoRaise(true);
        toolbar->addWidget(button);
        return button;
    };
    connect(action(tr("Copy"), tr("Copy last 50 messages"), "consoleCopy"), &QToolButton::clicked, this,
            &ConsolePanel::copyLast);
    connect(action(tr("Clear"), tr("Clear console"), "consoleClear"), &QToolButton::clicked, this,
            &ConsolePanel::clearAll);
    if (!popout) {
        connect(action(tr("Pop Out"), tr("Pop out console"), "consolePopOut"), &QToolButton::clicked, this,
                [this] { popOut(); });
    }
    layout->addLayout(toolbar);

    output_ = new QPlainTextEdit;
    output_->setObjectName("consoleOutput");
    output_->setReadOnly(true);
    output_->setMaximumBlockCount(ConsoleLog::kLimit);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    output_->setMinimumHeight(140);
    output_->setStyleSheet("QPlainTextEdit#consoleOutput { background: #111827; color: #e5e7eb; border-radius: 4px; }");
    layout->addWidget(output_, 1);
    latest_ = new QToolButton(output_->viewport());
    latest_->setObjectName("consoleLatest");
    latest_->setText(QStringLiteral("↓"));
    latest_->setToolTip(tr("Scroll to latest message"));
    latest_->hide();
    connect(latest_, &QToolButton::clicked, this, [this] {
        output_->verticalScrollBar()->setValue(output_->verticalScrollBar()->maximum());
    });
    // At the bottom is kept as a state (Virtuoso's followOutput): scrolling
    // sets it, and while it holds the view stays down as lines come and the
    // view resizes.
    QScrollBar* bar = output_->verticalScrollBar();
    connect(bar, &QScrollBar::valueChanged, this, [this, bar](int value) {
        atBottom_ = value >= bar->maximum();
        followOrOffer();
    });
    connect(bar, &QScrollBar::rangeChanged, this, [this, bar](int, int maximum) {
        if (atBottom_) {
            bar->setValue(maximum);
        }
        followOrOffer();
    });
    output_->viewport()->installEventFilter(this);

    input_ = new QLineEdit;
    input_->setPlaceholderText(tr("Enter G-code here..."));
    input_->setAccessibleName(tr("Console command"));
    input_->installEventFilter(this);
    auto* send = new QPushButton(tr("Send"));
    auto* row = new QHBoxLayout;
    row->addWidget(input_, 1);
    row->addWidget(send);
    layout->addLayout(row);
    const auto enter = [this] {
        submit(input_->text());
        input_->clear();
    };
    connect(send, &QPushButton::clicked, this, enter);
    connect(input_, &QLineEdit::returnPressed, this, enter);

    connect(&log_, &ConsoleLog::appended, this, [this](int count) { appendLines(count); });
    connect(&log_, &ConsoleLog::cleared, this, &ConsolePanel::render);
    connect(&machine_, &Machine::connectionChanged, this, &ConsolePanel::refreshConnection);
    refreshConnection();
    render();
}

void ConsolePanel::setFilter(ConsoleFilter filter) {
    filter_ = filter;
    if (QAbstractButton* button = filters_->button(static_cast<int>(filter))) {
        button->setChecked(true);
    }
    render();
}

void ConsolePanel::render() {
    output_->clear();
    for (const ConsoleMessage& message : log_.messages()) {
        if (matchesFilter(message, filter_)) {
            insert(message);
        }
    }
    atBottom_ = true;
    output_->verticalScrollBar()->setValue(output_->verticalScrollBar()->maximum());
}

void ConsolePanel::appendLines(int count) {
    QScrollBar* bar = output_->verticalScrollBar();
    const bool atEnd = atBottom_;
    const auto& messages = log_.messages();
    for (auto it = messages.end() - std::min<std::ptrdiff_t>(count, static_cast<std::ptrdiff_t>(messages.size()));
         it != messages.end(); ++it) {
        if (matchesFilter(*it, filter_)) {
            insert(*it);
        }
    }
    // Follow the output while at the bottom (followOutput).
    if (atEnd) {
        bar->setValue(bar->maximum());
    }
    followOrOffer();
}

void ConsolePanel::insert(const ConsoleMessage& message) {
    const Style& style = styleOf(message.type);
    QTextCursor cursor(output_->document());
    cursor.movePosition(QTextCursor::End);
    if (!output_->document()->isEmpty()) {
        cursor.insertBlock();
    }
    QTextCharFormat mark;
    mark.setForeground(QColor(style.markColour));
    cursor.insertText(QString::fromUtf8(style.mark) + ' ', mark);
    QTextCharFormat text;
    text.setForeground(QColor(style.textColour));
    if (style.bold) {
        text.setFontWeight(QFont::Bold);
    }
    cursor.insertText(message.text, text);
}

QStringList ConsolePanel::shownLines() const {
    QStringList lines;
    for (QTextBlock block = output_->document()->begin(); block.isValid(); block = block.next()) {
        if (!block.text().isEmpty()) {
            lines << block.text().mid(2);  // without the mark
        }
    }
    return lines;
}

void ConsolePanel::copyLast() {
    // The raw stream, not the view: a filtered console still gives the
    // last 50 lines the machine saw.
    const QStringList last = log_.lastTexts(kCopyLimit);
    if (last.isEmpty()) {
        return;
    }
    QApplication::clipboard()->setText(last.join('\n'));
    Q_EMIT notice(tr("Copied last %1 commands to clipboard").arg(last.size()), true);
}

void ConsolePanel::clearAll() {
    log_.clear();
    Q_EMIT notice(tr("Console cleared"), false);
}

ConsolePanel* ConsolePanel::popOut() {
    if (!popoutWindow_) {
        // A window of its own, owned by the main one.
        auto* window = new QWidget(this->window(), Qt::Window);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->setWindowTitle(tr("Console"));
        auto* layout = new QVBoxLayout(window);
        layout->setContentsMargins(6, 6, 6, 6);
        popout_ = new ConsolePanel(machine_, true);
        connect(popout_, &ConsolePanel::notice, this, &ConsolePanel::notice);
        layout->addWidget(popout_);
        window->resize(720, 520);
        popoutWindow_ = window;
    }
    popoutWindow_->show();
    popoutWindow_->raise();
    popoutWindow_->activateWindow();
    return popout_;
}

void ConsolePanel::submit(const QString& text) {
    const QString command = text.trimmed();
    if (command.isEmpty() || !machine_.isConnected()) {
        return;
    }
    machine_.sendConsoleLine(command);
    // The command itself is not echoed back: shown here as sent.
    log_.write(command, ConsoleType::Gcode);
    log_.addInput(command);
    historyIndex_ = -1;
}

const QStringList& ConsolePanel::history() const {
    return log_.inputHistory();
}

void ConsolePanel::followOrOffer() {
    QScrollBar* bar = output_->verticalScrollBar();
    latest_->setVisible(bar->value() < bar->maximum());
    placeLatest();
}

void ConsolePanel::placeLatest() {
    const QSize size = latest_->sizeHint();
    const QWidget* viewport = output_->viewport();
    latest_->setGeometry(viewport->width() - size.width() - 8, viewport->height() - size.height() - 8, size.width(),
                         size.height());
}

void ConsolePanel::refreshConnection() {
    const bool connected = machine_.isConnected();
    input_->setEnabled(connected);
    output_->setPlaceholderText(connected ? QString() : tr("Not connected to a device"));
}

bool ConsolePanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == output_->viewport() && event->type() == QEvent::Resize) {
        placeLatest();
    }
    if (watched == input_ && event->type() == QEvent::KeyPress) {
        const QStringList& history = log_.inputHistory();
        const int key = static_cast<QKeyEvent*>(event)->key();
        if ((key == Qt::Key_Up || key == Qt::Key_Down) && !history.isEmpty()) {
            if (key == Qt::Key_Up) {
                // From nothing chosen: the latest command.
                historyIndex_ = historyIndex_ == -1 ? static_cast<int>(history.size()) - 1 : std::max(0, historyIndex_ - 1);
            } else if (historyIndex_ == -1 || historyIndex_ >= static_cast<int>(history.size()) - 1) {
                // Down past the newest clears the line.
                historyIndex_ = -1;
                input_->clear();
                return true;
            } else {
                ++historyIndex_;
            }
            input_->setText(history[historyIndex_]);
            return true;
        }
        if (key == Qt::Key_Backspace && input_->text().size() <= 1) {
            historyIndex_ = -1;
        }
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace gs::app
