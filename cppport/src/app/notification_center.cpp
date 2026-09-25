#include "notification_center.hpp"

#include <QDateTime>

#include <algorithm>

namespace gs::app {

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

}  // namespace gs::app
