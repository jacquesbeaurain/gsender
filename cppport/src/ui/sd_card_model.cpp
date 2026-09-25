#include "sd_card_model.hpp"

#include "backend.hpp"
#include "machine.hpp"
#include "notification_center.hpp"
#include "sd_card_dialog.hpp"

#include "gs/controller/controller.hpp"

#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace gs::ui {
namespace {

QStringList localPaths(const QVariantList& files) {
    QStringList paths;
    for (const QVariant& file : files) {
        const QUrl url = file.toUrl();
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        } else if (const QString text = file.toString(); !text.isEmpty() && !text.contains("://")) {
            paths << text;
        }
    }
    return paths;
}

}  // namespace

SdCardModel::SdCardModel(QObject* parent)
    : QObject(parent),
      machine_(UiBackend::instance()->machine()),
      notifications_(UiBackend::instance()->notificationCenter()) {
    connect(&machine_, &app::Machine::connectionChanged, this, [this] {
        if (!machine_.isConnected()) {
            deleted_.clear();
            setUploadState("idle");  // the upload went with the connection
        }
        Q_EMIT changed();
    });
    connect(&machine_, &app::Machine::stateChanged, this, [this] {
        // Names deleted here come back only when the card lists them again.
        if (controller::Controller* c = machine_.controller()) {
            QSet<QString> listed;
            for (const protocol::SdFile& file : c->state().sdcard.files) {
                listed.insert(QString::fromStdString(file.name));
            }
            deleted_.intersect(listed);
        }
        Q_EMIT changed();
    });
    for (auto signal : {&app::Machine::settingsChanged, &app::Machine::workflowChanged}) {
        connect(&machine_, signal, this, &SdCardModel::changed);
    }
    connect(&machine_, &app::Machine::sdUploadStarted, this, [this] {
        progress_ = 0;
        setUploadState("uploading");
    });
    connect(&machine_, &app::Machine::sdUploadProgress, this, [this](int percent) {
        progress_ = percent;
        Q_EMIT changed();
    });
    connect(&machine_, &app::Machine::sdUploadCompleted, this, [this] {
        progress_ = 100;
        setUploadState("complete");
        QTimer::singleShot(1000, this, [this] {
            if (uploadState_ == "complete") {
                setUploadState("idle");
            }
        });
    });
    connect(&machine_, &app::Machine::sdUploadFailed, this, [this](const QString& error) {
        setUploadState("idle");
        notifications_.add(tr("Error uploading file - %1.").arg(error), app::NotificationType::Error);
    });
}

bool SdCardModel::toolsAvailable(QString* reason) const {
    controller::Controller* c = machine_.controller();
    if (!machine_.isConnected() || !c) {
        *reason = tr("Must be connected to use SD card functionality.");
        return false;
    }
    if (!c->isGrblHal()) {
        *reason = tr("SD card tools are only available for grblHAL devices.");
        return false;
    }
    const auto& info = c->runner().settings().info;
    const auto newopt = info.find("NEWOPT");
    if (newopt == info.end() || !(newopt->second.hasOption("FTP") || newopt->second.hasOption("YM"))) {
        *reason = tr("Enable FTP or YMODEM in firmware to use SD card tools.");
        return false;
    }
    return true;
}

QString SdCardModel::status() const {
    if (!machine_.isConnected() || !machine_.controller()) {
        return tr("Disconnected");
    }
    return mounted() ? tr("Mounted") : tr("Unmounted");
}

bool SdCardModel::available() const {
    QString reason;
    return toolsAvailable(&reason);
}

bool SdCardModel::mounted() const {
    controller::Controller* c = machine_.controller();
    return machine_.isConnected() && c && c->state().status.sdCard;
}

QString SdCardModel::message() const {
    QString reason;
    if (!toolsAvailable(&reason)) {
        return reason;
    }
    return rows().empty() ? tr("No files found") : QString();
}

std::vector<SdCardModel::Row> SdCardModel::rows() const {
    std::vector<Row> rows;
    controller::Controller* c = machine_.controller();
    QString reason;
    if (!c || !toolsAvailable(&reason)) {
        return rows;
    }
    const protocol::RunnerState& state = c->state();
    // A file running from the card reports its name in the status.
    const bool runningSdFile = state.status.sdProgress.name.has_value();
    const bool idle = c->workflow().isIdle();
    for (const protocol::SdFile& file : state.sdcard.files) {
        const QString name = QString::fromStdString(file.name);
        if (deleted_.contains(name)) {
            continue;
        }
        Row row{name, file.size, file.unusable};
        row.deletable = !runningSdFile && idle;
        row.runnable = row.deletable && !app::isAtciFile(name) && !file.unusable;
        rows.push_back(std::move(row));
    }
    return rows;
}

QVariantList SdCardModel::files() const {
    QVariantList list;
    for (const Row& row : rows()) {
        list.append(QVariantMap{
            {"name", row.name},
            {"size", app::formatSdFileSize(row.size)},
            {"atci", app::isAtciFile(row.name)},
            {"unusable", row.unusable},
            {"problem", app::sdFilenameProblem(row.name).value_or(tr("File flagged as unusable by firmware"))},
            {"runnable", row.runnable},
            {"deletable", row.deletable},
        });
    }
    return list;
}

QVariantList SdCardModel::pending() const {
    QVariantList list;
    for (const QString& path : pending_) {
        const QFileInfo info(path);
        list.append(QVariantMap{
            {"path", path},
            {"name", info.fileName()},
            {"size", app::formatSdFileSize(info.size())},
        });
    }
    return list;
}

QString SdCardModel::fileFilter() const {
    return app::sdFileFilter();
}

void SdCardModel::setUploadState(const QString& state) {
    uploadState_ = state;
    Q_EMIT changed();
}

void SdCardModel::refused(const QStringList& refused) {
    if (!refused.isEmpty()) {
        notifications_.add(app::sdRefusedText(refused), app::NotificationType::Error);
    }
}

void SdCardModel::opened() {
    controller::Controller* c = machine_.controller();
    QString reason;
    if (c && toolsAvailable(&reason) && uploadState_ == "idle") {
        const std::string& state = c->state().status.activeState;
        if (state == "Idle" || state == "Alarm") {
            refreshFiles();
        }
    }
}

void SdCardModel::refreshFiles() {
    if (controller::Controller* c = machine_.controller(); c && c->isGrblHal()) {
        deleted_.clear();
        c->sdList();
        Q_EMIT changed();
    }
}

void SdCardModel::addPending(const QVariantList& files) {
    const QStringList paths = localPaths(files);
    if (paths.isEmpty()) {
        return;
    }
    const app::SdFileCheck check = app::checkSdFiles(paths);
    refused(check.refused);
    pending_ << check.accepted;
    Q_EMIT changed();
}

void SdCardModel::removePending(int index) {
    if (index >= 0 && index < pending_.size()) {
        pending_.removeAt(index);
        Q_EMIT changed();
    }
}

void SdCardModel::clearPending() {
    pending_.clear();
    Q_EMIT changed();
}

QStringList SdCardModel::uploadPending() {
    QVariantList files;
    for (const QString& path : pending_) {
        files << path;
    }
    pending_.clear();
    Q_EMIT changed();
    return upload(files);
}

QStringList SdCardModel::upload(const QVariantList& list) {
    const app::SdFileCheck check = app::checkSdFiles(localPaths(list));
    refused(check.refused);
    controller::Controller* c = machine_.controller();
    if (!c || check.accepted.isEmpty()) {
        return {};
    }
    std::vector<protocol::YModemFile> files;
    QStringList names;
    for (const QString& path : check.accepted) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            notifications_.add(tr("Error uploading file - could not read %1.").arg(path),
                               app::NotificationType::Error);
            continue;
        }
        const QByteArray bytes = file.readAll();
        const QString name = QFileInfo(path).fileName();
        files.push_back({name.toStdString(), std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()))});
        names << name;
    }
    if (!files.empty()) {
        c->sdUpload(std::move(files));
    }
    return names;
}

void SdCardModel::runFile(const QString& name) {
    if (controller::Controller* c = machine_.controller(); c && canRun(name)) {
        c->sdRun(name.toStdString());
    }
}

void SdCardModel::deleteFile(const QString& name) {
    controller::Controller* c = machine_.controller();
    if (!c || !canDelete(name)) {
        return;
    }
    c->sdDelete(name.toStdString());
    deleted_.insert(name);
    Q_EMIT changed();
}

bool SdCardModel::canRun(const QString& name) const {
    const std::vector<Row> all = rows();
    const auto it = std::find_if(all.begin(), all.end(), [&](const Row& row) { return row.name == name; });
    return it != all.end() && it->runnable;
}

bool SdCardModel::canDelete(const QString& name) const {
    const std::vector<Row> all = rows();
    const auto it = std::find_if(all.begin(), all.end(), [&](const Row& row) { return row.name == name; });
    return it != all.end() && it->deletable;
}

}  // namespace gs::ui
