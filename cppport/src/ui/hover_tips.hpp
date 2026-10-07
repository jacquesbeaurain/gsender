#pragma once

// Hover feedback for the mouse, as upstream's browser gives it: the hand over
// a control, the "not allowed" cursor over a disabled one (shadcn's Button
// wraps it in `cursor-not-allowed`), and its Tooltip after 1.5 s
// (TooltipProvider delayDuration) - disabled or not.
//
// Qt Quick delivers no hover to a disabled item and never takes a cursor from
// one, so this watches the window's mouse moves itself: the item under the
// mouse (disabled ones included) and the nearest of its ancestors that is a
// control - a button, or an item that declares a `tooltip` property. Such an
// item may also declare `tooltipSide`, `tooltipDelay`, `clickable` (it acts
// on a tap: the hand) and `blocked` (enabled but not available now: not
// allowed). Main.qml shows the one ToolTip over `target` while `shown`.

#include <QBasicTimer>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace gs::ui {

class HoverTips : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QQuickWindow* window READ window WRITE setWindow NOTIFY windowChanged)
    // The ToolTip showing the text: its own items are not looked through.
    Q_PROPERTY(QObject* popup READ popup WRITE setPopup NOTIFY popupChanged)
    Q_PROPERTY(QQuickItem* target READ target NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    // The target's `tooltipSide`: "top" (the default), "bottom", "left" or
    // "right" (upstream's side).
    Q_PROPERTY(QString side READ side NOTIFY changed)
    Q_PROPERTY(bool shown READ shown NOTIFY changed)
    // The delay before a tooltip shows (ms).
    Q_PROPERTY(int delay READ delay WRITE setDelay NOTIFY delayChanged)

public:
    explicit HoverTips(QObject* parent = nullptr);
    ~HoverTips() override;

    QQuickWindow* window() const { return window_; }
    void setWindow(QQuickWindow* window);
    QObject* popup() const { return popup_; }
    void setPopup(QObject* popup);
    QQuickItem* target() const { return target_; }
    QString text() const;
    QString side() const;
    bool shown() const noexcept { return shown_; }
    int delay() const noexcept { return delay_; }
    void setDelay(int ms);

    // The control under a scene position, as a mouse move there finds it
    // (for tests, which have no real mouse).
    Q_INVOKABLE QQuickItem* controlAt(QPointF scenePos) const;
    // A mouse move to `scenePos` (tests).
    Q_INVOKABLE void hoverAt(QPointF scenePos);

Q_SIGNALS:
    void windowChanged();
    void popupChanged();
    void changed();
    void delayChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void timerEvent(QTimerEvent* event) override;

private:
    QQuickItem* itemAt(QQuickItem* item, QPointF scenePos) const;
    void track(QQuickItem* control);
    void applyCursor();
    void clear();

    QPointer<QQuickWindow> window_;
    QPointer<QObject> popup_;
    QPointer<QQuickItem> target_;
    // The item whose cursor this set (the target, or the content item for a
    // disabled one).
    QPointer<QQuickItem> cursorItem_;
    bool shown_ = false;
    bool pressed_ = false;  // a press hides the tip until the mouse moves on
    int delay_ = 1500;
    QBasicTimer timer_;
};

}  // namespace gs::ui
