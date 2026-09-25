#pragma once

// gSender's notifications: every pop-up message (lib/toaster) is kept - the
// last 100, read or unread - and listed by the bell (NotificationsArea),
// which counts the unread errors. The pop-ups themselves stay for
// workspace.toastDuration. And the alerts at a job's end
// (workspace/Alerts): the Job End summary and the Maintenance Alert.

#include "notification_center.hpp"

#include "gs/config/history.hpp"

#include <QDialog>
#include <QFrame>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QWidget>

#include <cstdint>
#include <deque>
#include <vector>

class QLabel;
class QListWidget;
class QTabBar;
class QVBoxLayout;

namespace gs::app {

class Machine;

// toastDuration: 0 the default, -1 until closed, -2 no pop-ups.
inline constexpr int kToastDefault = 5000;
inline constexpr int kToastLong = 10000;
inline constexpr int kToastUntilClose = -1;
inline constexpr int kToastDisabled = -2;

// The pop-ups, stacked at the bottom right of `host` (the newest lowest, at
// most three).
class ToastArea final : public QWidget {
    Q_OBJECT
public:
    explicit ToastArea(QWidget* host);

    void showToast(const QString& text, NotificationType type, int duration);
    int count() const;
    QStringList texts() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void place();

    QVBoxLayout* layout_;
};

// The list the bell opens: All, Errors, Info, Success; the newest first,
// with how long ago; Clear all.
class NotificationPanel final : public QFrame {
    Q_OBJECT
public:
    explicit NotificationPanel(NotificationCenter& center, QWidget* parent = nullptr);
    void refresh();
    void setTab(int tab);  // 0 All, 1 Errors, 2 Info, 3 Success
    int shownCount() const;

private:
    NotificationCenter& center_;
    QToolButton* clear_;
    QTabBar* tabs_;
    QListWidget* list_;
    QLabel* empty_;
};

// The bell, with the unread errors' count.
class NotificationButton final : public QToolButton {
    Q_OBJECT
public:
    explicit NotificationButton(NotificationCenter& center, QWidget* parent = nullptr);
    // Opening or closing the list marks everything read (upstream's
    // onOpenChange); DISPLAY_NOTIFICATIONS toggles it.
    void togglePanel();
    NotificationPanel& panel() noexcept { return *panel_; }

private:
    void refresh();

    NotificationCenter& center_;
    NotificationPanel* panel_;
};

// JobEndModal: the job's status, time and errors.
class JobEndDialog final : public QDialog {
    Q_OBJECT
public:
    JobEndDialog(bool completed, double durationMs, const QStringList& errors, QWidget* parent = nullptr);
    QString summary() const;

private:
    QLabel* text_;
};

// MaintenanceAlert: the tasks due; "Reset Timers" starts their hours again.
class MaintenanceAlertDialog final : public QDialog {
    Q_OBJECT
public:
    MaintenanceAlertDialog(Machine& machine, std::vector<config::MaintenanceTask> tasks, QWidget* parent = nullptr);
    QStringList taskNames() const;
    void resetTimers();

private:
    Machine& machine_;
    std::vector<config::MaintenanceTask> tasks_;
};

}  // namespace gs::app
