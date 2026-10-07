#include "hover_tips.hpp"

#include <QColor>
#include <QCursor>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTimerEvent>

#include <algorithm>
#include <cstring>

namespace gs::ui {
namespace {

bool hasProperty(const QObject* object, const char* name) {
    return object->metaObject()->indexOfProperty(name) >= 0;
}

bool isButton(const QQuickItem* item) {
    return item->inherits("QQuickAbstractButton");
}

// A control: a button, or an item declaring `tooltip`.
bool isControl(const QQuickItem* item) {
    return isButton(item) || hasProperty(item, "tooltip");
}

// The item's own TapHandlers: none, some enabled, or all disabled.
enum class Taps { None, Enabled, Disabled };
Taps taps(const QQuickItem* item) {
    Taps found = Taps::None;
    for (const QObject* child : item->children()) {
        if (child->inherits("QQuickTapHandler")) {
            if (child->property("enabled").toBool()) {
                return Taps::Enabled;
            }
            found = Taps::Disabled;
        }
    }
    return found;
}

bool clickable(const QQuickItem* item) {
    if (isButton(item)) {
        return true;
    }
    if (hasProperty(item, "clickable")) {
        return item->property("clickable").toBool();
    }
    return taps(item) != Taps::None;
}

bool blocked(const QQuickItem* item) {
    if (hasProperty(item, "blocked") && item->property("blocked").toBool()) {
        return true;
    }
    return !isButton(item) && taps(item) == Taps::Disabled;
}

// The C++ class a QML type is built on.
const char* baseClass(const QObject* object) {
    const QMetaObject* mo = object->metaObject();
    while (mo->superClass() && std::strstr(mo->className(), "_QML") != nullptr) {
        mo = mo->superClass();
    }
    return mo->className();
}

// What a mouse would be over: an item that paints or takes input - not a
// plain container (an Item, a layout, a clear Rectangle) that only holds
// others, such as the toasts' full-window layer.
bool solid(const QQuickItem* item) {
    if (isControl(item)) {
        return true;
    }
    for (const QObject* child : item->children()) {
        if (child->inherits("QQuickPointerHandler")) {
            return true;
        }
    }
    static const char* const kContainers[] = {
        "QQuickItem", "QQuickRootItem", "QQuickContentItem", "QQuickControl", "QQuickRowLayout",
        "QQuickColumnLayout", "QQuickGridLayout", "QQuickStackLayout", "QQuickRow", "QQuickColumn",
        "QQuickGrid", "QQuickFlow", "QQuickLoader", "QQuickFlickable", "QQuickListView", "QQuickGridView",
        "QQuickPathView", "QQuickScrollView", "QQuickOverlay", "QQuickPopupItem"};
    const char* base = baseClass(item);
    for (const char* name : kContainers) {
        if (std::strcmp(base, name) == 0) {
            return false;
        }
    }
    if (std::strcmp(base, "QQuickRectangle") == 0) {
        const auto color = item->property("color").value<QColor>();
        const auto* border = item->property("border").value<QObject*>();
        return color.alpha() > 0 || (border && border->property("width").toReal() > 0);
    }
    return true;
}

// The dynamic property marking a cursor this set (not the item's own).
constexpr const char* kOwnCursor = "_gsHoverCursor";

}  // namespace

HoverTips::HoverTips(QObject* parent) : QObject(parent) {}

HoverTips::~HoverTips() {
    if (window_) {
        window_->removeEventFilter(this);
    }
}

void HoverTips::setWindow(QQuickWindow* window) {
    if (window == window_) {
        return;
    }
    if (window_) {
        window_->removeEventFilter(this);
    }
    clear();
    window_ = window;
    if (window_) {
        window_->installEventFilter(this);
    }
    Q_EMIT windowChanged();
}

void HoverTips::setPopup(QObject* popup) {
    if (popup == popup_) {
        return;
    }
    popup_ = popup;
    Q_EMIT popupChanged();
}

void HoverTips::setDelay(int ms) {
    if (ms == delay_) {
        return;
    }
    delay_ = ms;
    Q_EMIT delayChanged();
}

QString HoverTips::text() const {
    return target_ ? target_->property("tooltip").toString() : QString();
}

QString HoverTips::side() const {
    const QString side = target_ ? target_->property("tooltipSide").toString() : QString();
    return side.isEmpty() ? QStringLiteral("top") : side;
}

QQuickItem* HoverTips::itemAt(QQuickItem* item, QPointF scenePos) const {
    if (!item->isVisible() || qFuzzyIsNull(item->opacity())) {
        return nullptr;
    }
    // The tooltip's own popup item (the contentItem's parent).
    if (popup_) {
        if (auto* content = qobject_cast<QQuickItem*>(popup_->property("contentItem").value<QObject*>());
            content && item == content->parentItem()) {
            return nullptr;
        }
    }
    const bool inside = item->contains(item->mapFromScene(scenePos));
    if (item->clip() && !inside) {
        return nullptr;
    }
    QList<QQuickItem*> children = item->childItems();
    std::stable_sort(children.begin(), children.end(),
                     [](const QQuickItem* a, const QQuickItem* b) { return a->z() < b->z(); });
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (QQuickItem* hit = itemAt(*it, scenePos)) {
            return hit;
        }
    }
    return inside && solid(item) ? item : nullptr;
}

QQuickItem* HoverTips::controlAt(QPointF scenePos) const {
    if (!window_) {
        return nullptr;
    }
    for (QQuickItem* item = itemAt(window_->contentItem(), scenePos); item; item = item->parentItem()) {
        if (isControl(item)) {
            return item;
        }
    }
    return nullptr;
}

void HoverTips::hoverAt(QPointF scenePos) {
    track(controlAt(scenePos));
}

bool HoverTips::eventFilter(QObject* watched, QEvent* event) {
    if (watched != window_) {
        return false;
    }
    switch (event->type()) {
    case QEvent::MouseMove: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        // A finger's synthesised moves: no hovering.
        if (mouse->pointingDevice() && mouse->pointingDevice()->type() != QInputDevice::DeviceType::Mouse) {
            break;
        }
        track(controlAt(mouse->scenePosition()));
        break;
    }
    case QEvent::MouseButtonPress:
        // Radix closes a tooltip on a press; it stays closed on that control.
        pressed_ = true;
        timer_.stop();
        if (shown_) {
            shown_ = false;
            Q_EMIT changed();
        }
        break;
    case QEvent::Leave:
        clear();
        break;
    default:
        break;
    }
    return false;
}

void HoverTips::timerEvent(QTimerEvent* event) {
    if (event->timerId() != timer_.timerId()) {
        QObject::timerEvent(event);
        return;
    }
    timer_.stop();
    if (target_ && !pressed_ && !text().isEmpty()) {
        shown_ = true;
        Q_EMIT changed();
    }
}

void HoverTips::track(QQuickItem* control) {
    if (control != target_) {
        if (target_) {
            disconnect(target_, nullptr, this, nullptr);
        }
        target_ = control;
        pressed_ = false;
        shown_ = false;
        timer_.stop();
        if (target_) {
            // The QPointer is already null when `destroyed` comes.
            connect(target_, &QObject::destroyed, this, [this] {
                timer_.stop();
                shown_ = false;
                Q_EMIT changed();
            });
            if (!text().isEmpty()) {
                // Radix's own TooltipProvider waits 700 ms; the Tooltip
                // component's, 1.5 s (`delay`).
                const int own = target_->property("tooltipDelay").toInt();
                timer_.start(own > 0 ? own : delay_, this);
            }
        }
        Q_EMIT changed();
    }
    applyCursor();
}

void HoverTips::applyCursor() {
    QQuickItem* want = nullptr;
    Qt::CursorShape shape = Qt::ArrowCursor;
    const bool input = target_ && (target_->inherits("QQuickTextInput") || target_->inherits("QQuickTextEdit"));
    if (target_ && !target_->isEnabled() && input) {
        // A disabled field: shadcn's Input is disabled:cursor-not-allowed.
        want = window_ ? window_->contentItem() : nullptr;
        shape = Qt::ForbiddenCursor;
    } else if (target_ && clickable(target_)) {
        if (!target_->isEnabled()) {
            // Qt takes no cursor from a disabled item: the window's content
            // item gives it.
            want = window_ ? window_->contentItem() : nullptr;
            shape = Qt::ForbiddenCursor;
        } else if (target_->cursor().shape() == Qt::ArrowCursor || target_->property(kOwnCursor).toBool()) {
            // Not where the item has a cursor of its own.
            want = target_;
            shape = blocked(target_) ? Qt::ForbiddenCursor : Qt::PointingHandCursor;
        }
    }
    if (cursorItem_ && cursorItem_ != want) {
        cursorItem_->unsetCursor();
        cursorItem_->setProperty(kOwnCursor, QVariant());
    }
    cursorItem_ = want;
    if (want && want->cursor().shape() != shape) {
        want->setCursor(shape);
        want->setProperty(kOwnCursor, true);
    }
}

void HoverTips::clear() {
    track(nullptr);
}

}  // namespace gs::ui
