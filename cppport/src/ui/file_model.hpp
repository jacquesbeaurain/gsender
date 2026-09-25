#pragma once

// File control (features/FileControl) for QML: the loaded file's
// information as FileInformation shows it (name, size, lines, path; Info -
// estimated time, feeds, speeds, tools - and Size - the extent), the recent
// files and the last job when nothing is loaded, and loading, reloading and
// closing.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class FileModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY changed)
    Q_PROPERTY(QString baseName READ baseName NOTIFY changed)    // without the extension
    Q_PROPERTY(QString extension READ extension NOTIFY changed)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY changed)    // "14 KB"
    Q_PROPERTY(int lines READ lines NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString estimatedTime READ estimatedTime NOTIFY changed)  // "1m 5s"
    Q_PROPERTY(QString feedText READ feedText NOTIFY changed)            // "300-1200 mm/min"
    Q_PROPERTY(QString speedText READ speedText NOTIFY changed)          // "None", "12000-18000 RPM"
    Q_PROPERTY(QString toolsText READ toolsText NOTIFY changed)          // "None", "2 (1,2)"
    // The extent: rows {axis, size, min, max} in the workspace units (A in
    // degrees, when the file turns it).
    Q_PROPERTY(QVariantList extent READ extent NOTIFY changed)
    // Loading and closing: not while a job runs.
    Q_PROPERTY(bool canLoad READ canLoad NOTIFY changed)
    Q_PROPERTY(bool canReload READ canReload NOTIFY changed)
    // Recent files {name, path}, newest first; the last job {file, status
    // ("COMPLETE"/"STOPPED"), duration ("1h 2m 3s")} - empty without one.
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY changed)
    Q_PROPERTY(QVariantMap lastJob READ lastJob NOTIFY changed)

public:
    explicit FileModel(QObject* parent = nullptr);

    bool loaded() const;
    bool analyzing() const;
    QString baseName() const;
    QString extension() const;
    QString sizeText() const;
    int lines() const;
    QString path() const;
    QString estimatedTime() const;
    QString feedText() const;
    QString speedText() const;
    QString toolsText() const;
    QVariantList extent() const;
    bool canLoad() const;
    bool canReload() const;
    QVariantList recentFiles() const;
    QVariantMap lastJob() const;

    // A file from disk (a path or a file:// URL): "" when loaded, else why not.
    Q_INVOKABLE QString load(const QString& file);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void close();

Q_SIGNALS:
    void changed();
    // "G-code File Closed" and the like, for the notifications.
    void notice(const QString& text);

private:
    app::Machine& machine_;
};

}  // namespace gs::ui
