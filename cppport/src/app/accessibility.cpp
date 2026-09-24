#include "accessibility.hpp"

#include "machine.hpp"
#include "shortcuts.hpp"

#include "gs/controller/controller.hpp"

#include <QAccessible>
#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace gs::app {

void playAudioCue(job::AudioCue cue) {
#ifdef _WIN32
    // PlaySound plays from memory that must outlive it: the three files are
    // made once and kept.
    static const std::array<std::string, 3> files{job::wavFile(job::audioCueSamples(job::AudioCue::Success)),
                                                  job::wavFile(job::audioCueSamples(job::AudioCue::Alarm)),
                                                  job::wavFile(job::audioCueSamples(job::AudioCue::Info))};
    const std::string& file = files[static_cast<std::size_t>(cue)];
    PlaySoundA(file.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    (void)cue;
    QApplication::beep();
#endif
}

// ---- announcements and cues ---------------------------------------------------------------

AccessibilityAnnouncer::AccessibilityAnnouncer(Machine& machine, QWidget& window, QObject* parent)
    : QObject(parent), machine_(machine), window_(window), player_(&playAudioCue) {
    connect(&machine_, &Machine::stateChanged, this, &AccessibilityAnnouncer::onState);
    connect(&machine_, &Machine::senderStatusChanged, this, &AccessibilityAnnouncer::onProgress);
    connect(&machine_, &Machine::programChanged, this, &AccessibilityAnnouncer::onProgram);
    connect(&machine_, &Machine::appSettingsChanged, this, &AccessibilityAnnouncer::onProgram);
    // The cues upstream subscribes to ("probe:success", "toolchange:start").
    connect(&machine_, &Machine::probeSucceeded, this, [this] {
        const AccessibilitySettings& a = machine_.settings().accessibility;
        if (a.audioCues && a.cueProbeSuccess) {
            play(job::AudioCue::Success);
        }
    });
    connect(&machine_, &Machine::toolChangeRequired, this, [this] {
        const AccessibilitySettings& a = machine_.settings().accessibility;
        if (a.audioCues && a.cueToolChange) {
            play(job::AudioCue::Info);
        }
    });
    onProgram();
}

void AccessibilityAnnouncer::announce(const QString& text, bool assertive) {
    announced_ << text;
    while (announced_.size() > 50) {
        announced_.removeFirst();
    }
    if (QAccessible::isActive()) {
        QAccessibleAnnouncementEvent event(&window_, text);
        event.setPoliteness(assertive ? QAccessible::AnnouncementPoliteness::Assertive
                                      : QAccessible::AnnouncementPoliteness::Polite);
        QAccessible::updateAccessibility(&event);
    }
}

void AccessibilityAnnouncer::play(job::AudioCue cue) {
    if (player_) {
        player_(cue);
    }
}

void AccessibilityAnnouncer::onState() {
    controller::Controller* c = machine_.controller();
    const std::string state = c ? c->state().status.activeState : std::string();
    if (state.empty() || state == lastState_) {
        return;
    }
    const AccessibilitySettings& a = machine_.settings().accessibility;
    if (a.statusAnnouncements) {
        announce(tr("Machine status changed to %1").arg(QString::fromStdString(state)), true);
    }
    if (a.audioCues) {
        if (state == "Alarm" && a.cueAlarm) {
            play(job::AudioCue::Alarm);
        } else if (lastState_ == "Run" && state == "Idle" && a.cueJobComplete) {
            play(job::AudioCue::Success);
        }
    }
    lastState_ = state;
}

void AccessibilityAnnouncer::onProgress() {
    const AccessibilitySettings& a = machine_.settings().accessibility;
    controller::Controller* c = machine_.controller();
    if (!a.jobProgressAnnouncements || !c || !c->sender().hasProgram()) {
        return;
    }
    // The job's progress bar: the lines the board has taken.
    const controller::SenderStatus status = c->sender().status();
    const double percent = status.total > 0 ? 100.0 * static_cast<double>(status.received) /
                                                  static_cast<double>(status.total)
                                            : 0.0;
    if (const std::optional<std::string> message = progress_.update(percent, a.jobProgressIncrement)) {
        announce(QString::fromStdString(*message), false);
    }
}

void AccessibilityAnnouncer::onProgram() {
    QString text;
    if (machine_.settings().accessibility.gcodeSummary && machine_.hasProgram() && !machine_.isAnalyzing()) {
        // The numbers are in the file's own units.
        const job::ProgramAnalysis& analysis = machine_.analysis();
        text = QString::fromStdString(job::jobSummary(machine_.programName().toStdString(), analysis,
                                                      machine_.programText(),
                                                      analysis.fileModal == "G20" ? "in" : "mm"));
    }
    if (text == summary_) {
        return;
    }
    summary_ = text;
    if (!text.isEmpty()) {
        announce(text, false);
    }
    Q_EMIT summaryChanged(summary_);
}

// ---- the keyboard map ---------------------------------------------------------------------

KeyboardMapOverlay::KeyboardMapOverlay(Machine& machine, ShortcutManager& shortcuts, QWidget* host)
    : QFrame(host), machine_(machine), shortcuts_(shortcuts), host_(host) {
    setObjectName("keyboardMap");
    setStyleSheet("QFrame#keyboardMap { background: rgba(0, 0, 0, 230); border: 1px solid rgba(255, 255, 255, 50);"
                  " border-radius: 12px; }"
                  "QFrame#keyboardMap QLabel { color: rgba(255, 255, 255, 180); background: transparent; }"
                  "QFrame#keyboardMap QLabel#mapTitle { color: white; font-size: 16pt; font-weight: 700; }"
                  "QFrame#keyboardMap QLabel#mapCategory { color: #60a5fa; font-size: 8pt; font-weight: 700; }"
                  "QFrame#keyboardMap QLabel#mapKeys { color: #bfdbfe; background: rgba(255, 255, 255, 25);"
                  " border: 1px solid rgba(255, 255, 255, 50); border-radius: 4px; padding: 0 6px;"
                  " font-family: monospace; }"
                  "QFrame#keyboardMap QScrollArea, QFrame#keyboardMap QScrollArea > QWidget > QWidget"
                  " { background: transparent; }");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    auto* header = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    auto* title = new QLabel(tr("Active Keyboard Shortcuts"));
    title->setObjectName("mapTitle");
    titles->addWidget(title);
    titles->addWidget(new QLabel(tr("Dynamic map of currently available shortcuts")));
    header->addLayout(titles, 1);
    auto* close = new QToolButton;
    close->setObjectName("closeKeyboardMap");
    close->setText(QStringLiteral("✕"));
    close->setAutoRaise(true);
    close->setToolTip(tr("Close keyboard map"));
    close->setStyleSheet("QToolButton { color: rgba(255, 255, 255, 160); font-size: 13pt; }");
    connect(close, &QToolButton::clicked, this, [this] {
        AppSettings settings = machine_.settings();
        settings.accessibility.showKeyboardMap = false;
        machine_.setSettings(settings);
    });
    header->addWidget(close, 0, Qt::AlignTop);
    layout->addLayout(header);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    body_ = new QWidget;
    grid_ = new QGridLayout(body_);
    grid_->setHorizontalSpacing(32);
    grid_->setVerticalSpacing(18);
    scroll->setWidget(body_);
    layout->addWidget(scroll, 1);

    host_->installEventFilter(this);
    // After the shortcut manager has rebuilt from the same signals.
    const auto later = [this] { QTimer::singleShot(0, this, &KeyboardMapOverlay::apply); };
    connect(&machine_, &Machine::appSettingsChanged, this, later);
    connect(&machine_, &Machine::macrosChanged, this, later);
    connect(&machine_, &Machine::connectionChanged, this, later);
    apply();
}

void KeyboardMapOverlay::apply() {
    const bool shown = machine_.settings().accessibility.showKeyboardMap;
    if (shown) {
        refresh();
        place();
        raise();
    }
    setVisible(shown);
}

void KeyboardMapOverlay::refresh() {
    while (QLayoutItem* item = grid_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    entries_.clear();
    // Upstream's order of the categories.
    static const char* const kCategories[] = {"General", "Location",  "Jogging",    "Spindle/Laser",
                                              "Coolant", "Carving",   "Toolbar",    "Visualizer",
                                              "Macros",  "Overrides", "Probing"};
    const std::vector<ShortcutManager::ActiveShortcut> active = shortcuts_.activeShortcuts();
    int block = 0;
    for (const char* category : kCategories) {
        auto* column = new QWidget;
        auto* list = new QGridLayout(column);
        list->setContentsMargins(0, 0, 0, 0);
        list->setHorizontalSpacing(12);
        list->setVerticalSpacing(4);
        auto* heading = new QLabel(QString::fromLatin1(category).toUpper());
        heading->setObjectName("mapCategory");
        list->addWidget(heading, 0, 0, 1, 2);
        int row = 1;
        for (const ShortcutManager::ActiveShortcut& shortcut : active) {
            if (shortcut.category != QLatin1String(category)) {
                continue;
            }
            auto* name = new QLabel(shortcut.title);
            name->setToolTip(shortcut.title);
            auto* keys = new QLabel(shortcut.keys);
            keys->setObjectName("mapKeys");
            list->addWidget(name, row, 0);
            list->addWidget(keys, row, 1, Qt::AlignRight);
            entries_ << QString("%1: %2 = %3").arg(QLatin1String(category), shortcut.title, shortcut.keys);
            ++row;
        }
        if (row == 1) {
            delete column;  // nothing in it
            continue;
        }
        list->setRowStretch(row, 1);
        grid_->addWidget(column, block / 3, block % 3, Qt::AlignTop);
        ++block;
    }
}

void KeyboardMapOverlay::place() {
    // Along the bottom, at most 1000 wide and 70 % of the height.
    const int width = std::min(1000, host_->width() - 64);
    const int height = std::min(static_cast<int>(host_->height() * 0.7), sizeHint().height() + 40);
    setGeometry((host_->width() - width) / 2, host_->height() - height - 32, std::max(width, 200),
                std::max(height, 120));
}

bool KeyboardMapOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == host_ && event->type() == QEvent::Resize && isVisible()) {
        place();
    }
    return QFrame::eventFilter(watched, event);
}

// ---- the focus ring -------------------------------------------------------------------------

FocusRing::FocusRing(QWidget* owner)
    : QWidget(owner, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput |
                         Qt::WindowDoesNotAcceptFocus | Qt::NoDropShadowWindowHint) {
    setObjectName("focusRing");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    // Widgets move without telling anyone (a scrolled page, a splitter):
    // the ring checks where its widget is a few times a second.
    timer_ = new QTimer(this);
    timer_->setInterval(150);
    connect(timer_, &QTimer::timeout, this, &FocusRing::track);
    connect(qApp, &QApplication::focusChanged, this, [this] { track(); });
}

void FocusRing::setActive(bool on) {
    active_ = on;
    if (on) {
        timer_->start();
        track();
    } else {
        timer_->stop();
        follow(nullptr);
    }
}

void FocusRing::track() {
    const bool appActive = QGuiApplication::applicationState() == Qt::ApplicationActive;
    follow(active_ && appActive ? QApplication::focusWidget() : nullptr);
}

void FocusRing::follow(QWidget* widget) {
    if (!active_ || !widget || widget == this || !widget->isVisible()) {
        target_ = nullptr;
        hide();
        return;
    }
    target_ = widget;
    const QRect ring = QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()).adjusted(-4, -4, 4, 4);
    if (geometry() != ring || !isVisible()) {
        setGeometry(ring);
        show();
        raise();
    }
}

void FocusRing::paintEvent(QPaintEvent*) {
    // Amber inside a dark edge: seen on light and dark backgrounds.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF outer = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(QColor(0, 0, 0, 200), 1));
    painter.drawRoundedRect(outer, 5, 5);
    painter.setPen(QPen(QColor(245, 158, 11), 3));
    painter.drawRoundedRect(outer.adjusted(2, 2, -2, -2), 4, 4);
}

}  // namespace gs::app
