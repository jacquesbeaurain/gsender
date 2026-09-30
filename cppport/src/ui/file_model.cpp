#include "file_model.hpp"

#include "backend.hpp"
#include "machine.hpp"
#include "qt_text.hpp"

#include "gs/config/history.hpp"
#include "gs/controller/controller.hpp"
#include "gs/job/file_info.hpp"
#include "gs/util/datetime.hpp"
#include "gs/util/jsnumber.hpp"

#include <QFileInfo>
#include <QUrl>

namespace gs::ui {

FileModel::FileModel(QObject* parent) : UiModelBase(parent) {
    recentFilesModel_.setRoles({
        {Qt::UserRole + 1, "name", [](const app::RecentFile& f) { return QString::fromStdString(f.fileName); }},
        {Qt::UserRole + 2, "path", [](const app::RecentFile& f) { return QString::fromStdString(f.filePath); }},
    });
    connectMachineSignals(true, true);
    for (auto signal : {&app::Machine::programChanged, &app::Machine::historyChanged}) {
        connect(&machine_, signal, this, [this] {
            updateRecentFiles();
            Q_EMIT changed();
        });
    }
    connect(&machine_, &app::Machine::appSettingsChanged, this, [this] {
        updateRecentFiles();
        Q_EMIT changed();
    });
    updateRecentFiles();
}

void FileModel::updateRecentFiles() {
    recentFilesModel_.reset(machine_.settings().recentFiles);
}

bool FileModel::loaded() const {
    return machine_.hasProgram();
}

bool FileModel::analyzing() const {
    return machine_.isAnalyzing();
}

QString FileModel::baseName() const {
    const QString name = machine_.programName();
    const qsizetype dot = name.lastIndexOf('.');
    return dot > 0 ? name.left(dot) : name;
}

QString FileModel::extension() const {
    const QString name = machine_.programName();
    const qsizetype dot = name.lastIndexOf('.');
    return dot > 0 ? name.mid(dot + 1) : QString();
}

QString FileModel::sizeText() const {
    return qstr(job::fileSizeText(machine_.programText().size()));
}

int FileModel::lines() const {
    return static_cast<int>(machine_.analysis().totalLines);
}

QString FileModel::path() const {
    return machine_.programPath();
}

QString FileModel::estimatedTime() const {
    return qstr(util::estimatedTimeText(machine_.analysis().estimatedTime));
}

QString FileModel::feedText() const {
    const std::string text = job::feedRangeText(machine_.analysis(), machine_.settings().metric);
    return text.empty() ? tr("None") : qstr(text);
}

QString FileModel::speedText() const {
    const std::string text = job::speedRangeText(machine_.analysis());
    return text.empty() ? tr("None") : qstr(text);
}

QString FileModel::toolsText() const {
    const std::vector<std::string>& tools = machine_.analysis().tools;
    if (tools.empty()) {
        return tr("None");
    }
    QStringList numbers;
    for (const std::string& tool : tools) {
        numbers << QString::fromStdString(tool).remove('T');
    }
    return QString("%1 (%2)").arg(tools.size()).arg(numbers.join(','));
}

QVariantList FileModel::extent() const {
    const gcode::BoundingBox& b = machine_.analysis().bounds;
    const double factor = machine_.settings().metric ? 1.0 : 1.0 / 25.4;
    const auto row = [](const QString& axis, double min, double max, double scale) {
        QVariantMap r;
        r["axis"] = axis;
        r["size"] = QString::fromStdString(js::toFixed((max - min) * scale, 2));
        r["min"] = QString::fromStdString(js::toFixed(min * scale, 2));
        r["max"] = QString::fromStdString(js::toFixed(max * scale, 2));
        return r;
    };
    QVariantList rows{row("X", b.min.x, b.max.x, factor), row("Y", b.min.y, b.max.y, factor),
                      row("Z", b.min.z, b.max.z, factor)};
    if (machine_.analysis().usedAxes.find('A') != std::string::npos) {
        rows.append(row("A", b.min.a, b.max.a, 1.0));
    }
    return rows;
}

bool FileModel::canLoad() const {
    controller::Controller* c = machine_.controller();
    return !c || c->workflow().isIdle();
}

bool FileModel::canReload() const {
    // ReloadFileAlert: a file from disk (not a generated one).
    return canLoad() && loaded() && !path().isEmpty();
}

QVariantList FileModel::recentFiles() const {
    return recentFilesModel_.toVariantList();
}

QVariantMap FileModel::lastJob() const {
    const config::JobStats stats = config::JobStatsStore(machine_.config()).load();
    if (stats.jobs.empty()) {
        return {};
    }
    const config::JobRecord& job = stats.jobs.back();
    return {{"file", QString::fromStdString(job.file)},
            {"status", job.completed ? QStringLiteral("COMPLETE") : QStringLiteral("STOPPED")},
            {"duration", qstr(util::durationText(job.duration))}};
}

QString FileModel::load(const QString& file) {
    if (!canLoad()) {
        return tr("A job is running");
    }
    const QUrl url(file);
    const QString local = url.isLocalFile() ? url.toLocalFile() : file;
    QString error;
    if (!machine_.loadFile(local, &error)) {
        return error.isEmpty() ? tr("Could not load %1").arg(QFileInfo(local).fileName()) : error;
    }
    return {};
}

void FileModel::reload() {
    if (canReload()) {
        load(path());
    }
}

void FileModel::close() {
    if (loaded() && canLoad()) {
        machine_.unloadProgram();
        Q_EMIT notice(tr("G-code File Closed"));
    }
}

}  // namespace gs::ui
