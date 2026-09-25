#pragma once

// Tools > SD Card (features/SDCard) for QML: whether a grblHAL board's card
// is mounted, its files - run or delete them - and uploads over YMODEM with
// their progress, as the widget dialog does. The Tools page keeps one, so
// an upload is followed to its end after Go Back.

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
class NotificationCenter;
}

namespace gs::ui {

class SdCardModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString status READ status NOTIFY changed)  // Mounted, Unmounted or Disconnected
    Q_PROPERTY(bool available READ available NOTIFY changed)  // the card's tools can be used
    Q_PROPERTY(bool mounted READ mounted NOTIFY changed)
    // Why there are no files: the tools unavailable, or none found (empty
    // while files show).
    Q_PROPERTY(QString message READ message NOTIFY changed)
    // Each {name, size (text), atci, unusable, problem, runnable, deletable}.
    Q_PROPERTY(QVariantList files READ files NOTIFY changed)
    Q_PROPERTY(QString uploadState READ uploadState NOTIFY changed)  // idle, uploading, complete
    Q_PROPERTY(int uploadProgress READ uploadProgress NOTIFY changed)
    // The Upload modal's files: each {path, name, size}.
    Q_PROPERTY(QVariantList pending READ pending NOTIFY changed)
    Q_PROPERTY(QString fileFilter READ fileFilter CONSTANT)

public:
    explicit SdCardModel(QObject* parent = nullptr);

    QString status() const;
    bool available() const;
    bool mounted() const;
    QString message() const;
    QVariantList files() const;
    QString uploadState() const { return uploadState_; }
    int uploadProgress() const { return progress_; }
    QVariantList pending() const;
    QString fileFilter() const;

    // The page opened: the card listed afresh when the board can take it.
    Q_INVOKABLE void opened();
    Q_INVOKABLE void refreshFiles();
    // The modal's files (paths or file: URLs): the refused reported.
    Q_INVOKABLE void addPending(const QVariantList& files);
    Q_INVOKABLE void removePending(int index);
    Q_INVOKABLE void clearPending();
    Q_INVOKABLE QStringList uploadPending();
    // A drop on the list: checked, the refused reported, the rest sent.
    // The names sent.
    Q_INVOKABLE QStringList upload(const QVariantList& files);
    Q_INVOKABLE void runFile(const QString& name);
    Q_INVOKABLE void deleteFile(const QString& name);  // once asked; it leaves the list at once
    Q_INVOKABLE bool canRun(const QString& name) const;
    Q_INVOKABLE bool canDelete(const QString& name) const;

Q_SIGNALS:
    void changed();

private:
    struct Row {
        QString name;
        long long size = 0;
        bool unusable = false;
        bool runnable = false;
        bool deletable = false;
    };
    std::vector<Row> rows() const;
    bool toolsAvailable(QString* reason) const;
    void setUploadState(const QString& state);
    void refused(const QStringList& refused);

    app::Machine& machine_;
    app::NotificationCenter& notifications_;
    QString uploadState_ = QStringLiteral("idle");
    int progress_ = 0;
    QSet<QString> deleted_;  // gone from the list until the card is listed again
    QStringList pending_;
};

}  // namespace gs::ui
