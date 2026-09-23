#include "notifications.hpp"

#include "machine.hpp"

#include "gs/util/datetime.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTabBar>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace gs::app {
namespace {

// NotificationItem's colorsMap (Tailwind's 500s).
QString colorOf(NotificationType type) {
    switch (type) {
        case NotificationType::Success: return QStringLiteral("#22c55e");
        case NotificationType::Error: return QStringLiteral("#ef4444");
        case NotificationType::Info: return QStringLiteral("#3b82f6");
        case NotificationType::Warning: return QStringLiteral("#eab308");
    }
    return QStringLiteral("#3b82f6");
}

QPixmap bellIcon(int unread) {
    QPixmap pixmap(30, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(0x9c, 0xa3, 0xaf), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath bell;
    bell.moveTo(8, 19);
    bell.lineTo(8, 12);
    bell.arcTo(QRectF(8, 5, 14, 14), 180, -180);
    bell.lineTo(22, 19);
    bell.lineTo(24, 21);
    bell.lineTo(6, 21);
    bell.closeSubpath();
    painter.drawPath(bell);
    painter.drawArc(QRectF(12.5, 21, 5, 4), 180 * 16, 180 * 16);  // the clapper
    painter.drawPoint(QPointF(15, 4));
    if (unread > 0) {
        const QRectF badge(17, 0, 13, 13);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xef, 0x44, 0x44));
        painter.drawEllipse(badge);
        QFont font = painter.font();
        font.setPixelSize(unread > 9 ? 7 : 9);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, QString::number(unread));
    }
    return pixmap;
}

// The panel tells the button when it closed, so a click on the bell that
// closes it does not open it again.
class PanelWatcher final : public QObject {
public:
    explicit PanelWatcher(QObject* parent) : QObject(parent) { hidden_.invalidate(); }
    bool closedJustNow() const { return hidden_.isValid() && hidden_.elapsed() < 250; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Hide) {
            hidden_.start();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QElapsedTimer hidden_;
};

}  // namespace

// ---- the store ---------------------------------------------------------------------------

NotificationCenter::NotificationCenter(QObject* parent) : QObject(parent) {}

void NotificationCenter::add(const QString& message, NotificationType type) {
    if (list_.size() >= kLimit) {
        list_.pop_front();
    }
    list_.push_back({nextId_++, message, type, false, QDateTime::currentMSecsSinceEpoch()});
    const Notification latest = list_.back();
    Q_EMIT added(latest);
    Q_EMIT changed();
}

int NotificationCenter::unreadErrors() const {
    return static_cast<int>(std::count_if(list_.begin(), list_.end(), [](const Notification& n) {
        return n.type == NotificationType::Error && !n.read;
    }));
}

void NotificationCenter::readAll() {
    bool any = false;
    for (Notification& n : list_) {
        any = any || !n.read;
        n.read = true;
    }
    if (any) {
        Q_EMIT changed();
    }
}

void NotificationCenter::clear() {
    if (list_.empty()) {
        return;
    }
    list_.clear();
    Q_EMIT changed();
}

// ---- pop-ups -----------------------------------------------------------------------------

ToastArea::ToastArea(QWidget* host) : QWidget(host) {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(8);
    host->installEventFilter(this);
    hide();
}

void ToastArea::showToast(const QString& text, NotificationType type, int duration) {
    if (duration == kToastDisabled) {
        return;
    }
    // At most three at a time: the oldest goes.
    while (count() >= 3) {
        QLayoutItem* oldest = layout_->takeAt(0);
        delete oldest->widget();
        delete oldest;
    }
    auto* toast = new QFrame;
    toast->setObjectName("toast");
    toast->setStyleSheet(QString("QFrame#toast { background:palette(base); border:1px solid palette(mid);"
                                 " border-left:5px solid %1; border-radius:6px; }")
                             .arg(colorOf(type)));
    auto* row = new QHBoxLayout(toast);
    row->setContentsMargins(10, 6, 4, 6);
    auto* label = new QLabel(text);
    label->setObjectName("toastText");
    label->setWordWrap(true);
    label->setMinimumWidth(240);
    label->setMaximumWidth(360);
    label->setTextFormat(Qt::PlainText);
    auto* close = new QToolButton;
    close->setText(QString::fromUtf8("✕"));
    close->setAutoRaise(true);
    close->setToolTip(tr("Close"));
    row->addWidget(label, 1);
    row->addWidget(close, 0, Qt::AlignTop);
    layout_->addWidget(toast);
    connect(close, &QToolButton::clicked, toast, &QObject::deleteLater);
    connect(toast, &QObject::destroyed, this, [this] { QTimer::singleShot(0, this, &ToastArea::place); });
    if (duration != kToastUntilClose) {
        QTimer::singleShot(duration == 0 ? kToastDefault : duration, toast, &QObject::deleteLater);
    }
    QWidget::show();
    place();
}

int ToastArea::count() const {
    return layout_->count();
}

QStringList ToastArea::texts() const {
    QStringList out;
    for (int i = 0; i < layout_->count(); ++i) {
        if (QWidget* toast = layout_->itemAt(i)->widget()) {
            if (auto* label = toast->findChild<QLabel*>("toastText")) {
                out << label->text();
            }
        }
    }
    return out;
}

void ToastArea::place() {
    if (layout_->count() == 0) {
        hide();
        return;
    }
    adjustSize();
    const QWidget* host = parentWidget();
    move(host->width() - width() - 16, host->height() - height() - 16);
    raise();
}

bool ToastArea::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
        place();
    }
    return QWidget::eventFilter(watched, event);
}

// ---- the list ----------------------------------------------------------------------------

NotificationPanel::NotificationPanel(NotificationCenter& center, QWidget* parent)
    : QFrame(parent, Qt::Popup), center_(center) {
    setFrameShape(QFrame::StyledPanel);
    setMinimumWidth(400);
    auto* layout = new QVBoxLayout(this);
    auto* header = new QHBoxLayout;
    header->addWidget(new QLabel(QString("<b>%1</b>").arg(tr("Notifications"))));
    header->addStretch(1);
    clear_ = new QToolButton;
    clear_->setText(tr("Clear all"));
    clear_->setToolTip(tr("Clear all notifications"));
    header->addWidget(clear_);
    layout->addLayout(header);
    tabs_ = new QTabBar;
    for (const QString& tab : {tr("All"), tr("Errors"), tr("Info"), tr("Success")}) {
        tabs_->addTab(tab);
    }
    tabs_->setExpanding(true);
    layout->addWidget(tabs_);
    list_ = new QListWidget;
    list_->setFixedHeight(220);
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    empty_ = new QLabel(tr("No notifications"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setFixedHeight(220);
    layout->addWidget(list_);
    layout->addWidget(empty_);
    connect(clear_, &QToolButton::clicked, &center_, &NotificationCenter::clear);
    connect(tabs_, &QTabBar::currentChanged, this, &NotificationPanel::refresh);
    connect(&center_, &NotificationCenter::changed, this, &NotificationPanel::refresh);
    refresh();
}

void NotificationPanel::setTab(int tab) {
    tabs_->setCurrentIndex(tab);
    refresh();
}

int NotificationPanel::shownCount() const {
    return list_->count();
}

void NotificationPanel::refresh() {
    static const NotificationType kFilters[] = {NotificationType::Info /*unused*/, NotificationType::Error,
                                                NotificationType::Info, NotificationType::Success};
    const int tab = tabs_->currentIndex();
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch();
    list_->clear();
    const std::deque<Notification>& all = center_.list();
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        if (tab > 0 && it->type != kFilters[tab]) {
            continue;
        }
        auto* row = new QFrame;
        row->setObjectName("notification");
        row->setStyleSheet(QString("QFrame#notification { border-left:6px solid %1; }").arg(colorOf(it->type)));
        auto* rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(10, 4, 6, 4);
        rowLayout->setSpacing(1);
        auto* message = new QLabel(it->message);
        message->setWordWrap(true);
        message->setTextFormat(Qt::PlainText);
        auto* ago = new QLabel(QString::fromStdString(util::timeAgo(it->time, now)));
        ago->setStyleSheet("color:palette(mid); font-size:8pt");
        rowLayout->addWidget(message);
        rowLayout->addWidget(ago);
        auto* item = new QListWidgetItem(list_);
        row->setFixedWidth(360);
        item->setSizeHint(row->sizeHint());
        list_->setItemWidget(item, row);
    }
    list_->setVisible(list_->count() > 0);
    empty_->setVisible(list_->count() == 0);
    clear_->setEnabled(!all.empty());
}

// ---- the bell ----------------------------------------------------------------------------

NotificationButton::NotificationButton(NotificationCenter& center, QWidget* parent)
    : QToolButton(parent), center_(center) {
    setAutoRaise(true);
    setIconSize(QSize(30, 28));
    panel_ = new NotificationPanel(center_, this);
    auto* watcher = new PanelWatcher(this);
    watcher->setObjectName("panelWatcher");
    panel_->installEventFilter(watcher);
    connect(this, &QToolButton::clicked, this, [this, watcher] {
        if (!watcher->closedJustNow()) {
            togglePanel();
        }
    });
    connect(&center_, &NotificationCenter::changed, this, &NotificationButton::refresh);
    refresh();
}

void NotificationButton::togglePanel() {
    center_.readAll();
    if (panel_->isVisible()) {
        panel_->hide();
        return;
    }
    panel_->refresh();
    panel_->adjustSize();
    const QPoint below = mapToGlobal(QPoint(width(), height()));
    panel_->move(below.x() - panel_->width(), below.y() + 4);
    panel_->show();
}

void NotificationButton::refresh() {
    const int unread = center_.unreadErrors();
    setIcon(QIcon(bellIcon(unread)));
    setToolTip(unread > 0 ? tr("Notifications, %1 unread").arg(unread) : tr("Notifications"));
}

// ---- the job's end -----------------------------------------------------------------------

JobEndDialog::JobEndDialog(bool completed, double durationMs, const QStringList& errors, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Job End"));
    auto* layout = new QVBoxLayout(this);
    const QString status = completed ? QStringLiteral("COMPLETE") : QStringLiteral("STOPPED");
    QString errorList = QStringLiteral("None");
    if (!errors.isEmpty()) {
        QStringList lines;
        for (const QString& error : errors) {
            lines << "- " + error.toHtmlEscaped();
        }
        errorList = lines.join("<br>");
    }
    text_ = new QLabel(QString("<p><b>%1</b> <span style='color:%2'>%3</span></p>"
                               "<p><b>%4</b> %5</p><p><b>%6</b><br>%7</p>")
                           .arg(tr("Status:"), completed ? "#22c55e" : "#ef4444", status, tr("Time:"),
                                QString::fromStdString(util::millisecondsToTimeStamp(durationMs)), tr("Errors:"),
                                errorList));
    text_->setTextFormat(Qt::RichText);
    text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(text_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString JobEndDialog::summary() const {
    QTextDocument document;
    document.setHtml(text_->text());
    return document.toPlainText();
}

MaintenanceAlertDialog::MaintenanceAlertDialog(Machine& machine, std::vector<config::MaintenanceTask> tasks,
                                               QWidget* parent)
    : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("Maintenance Alert"));
    for (config::MaintenanceTask& task : tasks) {
        if (task.currentTime >= task.rangeStart) {
            tasks_.push_back(std::move(task));
        }
    }
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("The following maintenance tasks are due:")));
    QStringList bullets;
    for (const config::MaintenanceTask& task : tasks_) {
        bullets << QString::fromUtf8("• ") + QString::fromStdString(task.name);
    }
    auto* list = new QLabel(bullets.join('\n'));
    list->setTextFormat(Qt::PlainText);
    layout->addWidget(list);
    auto* note = new QLabel(tr("Click 'Reset Timers' to reset the timers on ALL listed tasks. Click 'Close' to close "
                               "the popup and do nothing."));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox;
    QPushButton* reset = buttons->addButton(tr("Reset Timers"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(reset, &QPushButton::clicked, this, [this] {
        resetTimers();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QStringList MaintenanceAlertDialog::taskNames() const {
    QStringList names;
    for (const config::MaintenanceTask& task : tasks_) {
        names << QString::fromStdString(task.name);
    }
    return names;
}

void MaintenanceAlertDialog::resetTimers() {
    std::vector<int> ids;
    for (const config::MaintenanceTask& task : tasks_) {
        ids.push_back(task.id);
    }
    machine_.resetMaintenanceTimers(ids);
    Q_EMIT machine_.notice(tr("Reset Timers successfully"));
}

}  // namespace gs::app
