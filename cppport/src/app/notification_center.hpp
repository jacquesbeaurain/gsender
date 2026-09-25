#pragma once

// gSender's notifications (lib/toaster, NotificationsArea): every message
// the application pops up is kept - the last 100, read or unread - for the
// bell's list, which counts the unread errors. Shared by both UIs.

#include <QObject>
#include <QString>

#include <cstdint>
#include <deque>

namespace gs::app {

enum class NotificationType { Success, Error, Info, Warning };

struct Notification {
    std::uint64_t id = 0;
    QString message;
    NotificationType type = NotificationType::Info;
    bool read = false;
    std::int64_t time = 0;  // ms since the epoch
};

class NotificationCenter final : public QObject {
    Q_OBJECT
public:
    static constexpr std::size_t kLimit = 100;  // NOTIFICATIONS_LIST_LIMIT

    explicit NotificationCenter(QObject* parent = nullptr);

    // saveNotificationToStore(): kept unread, the oldest dropped past 100.
    void add(const QString& message, NotificationType type);
    const std::deque<Notification>& list() const noexcept { return list_; }
    int unreadErrors() const;
    void readAll();
    void clear();

Q_SIGNALS:
    void added(const gs::app::Notification& notification);
    void changed();

private:
    std::deque<Notification> list_;
    std::uint64_t nextId_ = 1;
};

}  // namespace gs::app
