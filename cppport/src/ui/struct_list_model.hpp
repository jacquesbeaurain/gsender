#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace gs::ui {

// Abstract base class providing QObject, QML invokables, and properties for StructListModel.
class StructListModelBase : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit StructListModelBase(QObject* parent = nullptr);
    ~StructListModelBase() override = default;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override = 0;
    virtual int count() const { return rowCount(); }

    Q_INVOKABLE virtual QVariantMap get(int index) const = 0;
    Q_INVOKABLE virtual QVariant getRole(int index, const QString& role) const = 0;
    Q_INVOKABLE virtual QVariantList toList() const = 0;

Q_SIGNALS:
    void countChanged();
};

// Generic, zero-allocation role-based model wrapping std::vector<T>.
// Exposes custom role mappings for direct QML delegate bindings and lazily
// caches QVariantList conversions to avoid repeated heap allocation churn.
template <typename T>
class StructListModel : public StructListModelBase {
public:
    struct Role {
        int id;
        QByteArray name;
        std::function<QVariant(const T&)> getter;
    };

    explicit StructListModel(QObject* parent = nullptr) : StructListModelBase(parent) {}

    explicit StructListModel(std::vector<Role> roles, QObject* parent = nullptr)
        : StructListModelBase(parent), roles_(std::move(roles)) {
        initRoleNames();
    }

    void setRoles(std::vector<Role> roles) {
        roles_ = std::move(roles);
        initRoleNames();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        if (parent.isValid()) {
            return 0;
        }
        return static_cast<int>(items_.size());
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(items_.size())) {
            return {};
        }
        const T& item = items_[index.row()];
        for (const auto& r : roles_) {
            if (r.id == role) {
                return r.getter(item);
            }
        }
        return {};
    }

    QHash<int, QByteArray> roleNames() const override {
        return roleNames_;
    }

    void reset(std::vector<T> items) {
        const bool countDiff = (items_.size() != items.size());
        beginResetModel();
        items_ = std::move(items);
        cachedVariantList_.reset();
        endResetModel();
        if (countDiff) {
            Q_EMIT countChanged();
        }
    }

    void reset(std::initializer_list<T> items) {
        reset(std::vector<T>(items));
    }

    void clear() {
        reset({});
    }

    const std::vector<T>& items() const { return items_; }
    std::size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }

    const T& at(std::size_t index) const { return items_.at(index); }

    QVariantMap get(int index) const override {
        if (index < 0 || index >= static_cast<int>(items_.size())) {
            return {};
        }
        const T& item = items_[index];
        QVariantMap map;
        for (const auto& r : roles_) {
            map.insert(QString::fromUtf8(r.name), r.getter(item));
        }
        return map;
    }

    QVariant getRole(int index, const QString& role) const override {
        if (index < 0 || index >= static_cast<int>(items_.size())) {
            return {};
        }
        const QByteArray roleUtf8 = role.toUtf8();
        const T& item = items_[index];
        for (const auto& r : roles_) {
            if (r.name == roleUtf8) {
                return r.getter(item);
            }
        }
        return {};
    }

    QVariantList toList() const override {
        return toVariantList();
    }

    const QVariantList& toVariantList() const {
        if (!cachedVariantList_) {
            QVariantList list;
            list.reserve(static_cast<qsizetype>(items_.size()));
            for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
                list.append(get(i));
            }
            cachedVariantList_ = std::move(list);
        }
        return *cachedVariantList_;
    }

private:
    void initRoleNames() {
        roleNames_.clear();
        for (const auto& r : roles_) {
            roleNames_[r.id] = r.name;
        }
    }

    std::vector<T> items_;
    std::vector<Role> roles_;
    QHash<int, QByteArray> roleNames_;
    mutable std::optional<QVariantList> cachedVariantList_;
};

}  // namespace gs::ui
