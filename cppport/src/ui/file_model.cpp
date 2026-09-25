#include "file_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/config/history.hpp"
#include "gs/controller/controller.hpp"
#include "gs/util/jsnumber.hpp"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace gs::ui {
namespace {

// Info's formatNumber: integers as they are, else 2 decimals without
// trailing zeros.
QString formatNumber(double value) {
    if (value == std::floor(value)) {
        return QString::fromStdString(js::numberToString(value));
    }
    QString text = QString::fromStdString(js::toFixed(value, 2));
    while (text.contains('.') && (text.endsWith('0') || text.endsWith('.'))) {
        text.chop(1);
    }
    return text;
}

// A duration as the stats write it: "1h 2m 3s" (getTimeString).
QString timeString(std::int64_t ms) {
    const std::int64_t seconds = ms / 1000;
    const std::int64_t h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    if (h > 0) {
        return QString("%1h %2m %3s").arg(h).arg(m).arg(s);
    }
    if (m > 0) {
        return QString("%1m %2s").arg(m).arg(s);
    }
    return QString("%1s").arg(s);
}

// The numbers of a set of words ("F300", "S12000").
std::vector<double> wordValues(const std::vector<std::string>& words) {
    std::vector<double> values;
    for (const std::string& word : words) {
        const double value = js::stringToNumber(word.size() > 1 ? word.substr(1) : std::string());
        if (std::isfinite(value)) {
            values.push_back(value);
        }
    }
    return values;
}

}  // namespace

FileModel::FileModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    for (auto signal : {&app::Machine::programChanged, &app::Machine::appSettingsChanged,
                        &app::Machine::workflowChanged, &app::Machine::connectionChanged,
                        &app::Machine::historyChanged}) {
        connect(&machine_, signal, this, &FileModel::changed);
    }
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
    // FileInformation's formatFileSize.
    const auto size = static_cast<double>(machine_.programText().size());
    if (size < 1024) {
        return QString("%1 Bytes").arg(size);
    }
    if (size < 1024 * 1024) {
        return QString("%1 KB").arg(QString::fromStdString(js::toFixed(size / 1024, 0)));
    }
    return QString("%1 MB").arg(QString::fromStdString(js::toFixed(size / (1024 * 1024), 0)));
}

int FileModel::lines() const {
    return static_cast<int>(machine_.analysis().totalLines);
}

QString FileModel::path() const {
    return machine_.programPath();
}

QString FileModel::estimatedTime() const {
    // Info's formatEstimatedTime.
    const double seconds = machine_.analysis().estimatedTime;
    if (seconds < 60) {
        return QString("%1s").arg(std::ceil(seconds));
    }
    if (seconds < 3600) {
        return QString("%1m %2s").arg(std::floor(seconds / 60)).arg(std::ceil(std::fmod(seconds, 60)));
    }
    return QString("%1h %2m").arg(std::floor(seconds / 3600)).arg(std::floor(std::fmod(seconds, 3600) / 60));
}

QString FileModel::feedText() const {
    const job::ProgramAnalysis& a = machine_.analysis();
    std::vector<double> feeds = wordValues(a.feedrates);
    const bool metric = machine_.settings().metric;
    const QString unit = metric ? "mm/min" : "in/min";
    if (feeds.empty()) {
        // Math.min() of nothing is Infinity upstream; say none instead.
        return tr("None");
    }
    // The file's units against the workspace's (convertFeedrate).
    for (double& feed : feeds) {
        if (a.fileModal == "G20" && metric) {
            feed *= 25.4;
        } else if (a.fileModal == "G21" && !metric) {
            feed /= 25.4;
        }
    }
    const auto [low, high] = std::minmax_element(feeds.begin(), feeds.end());
    return *low == *high ? QString("%1 %2").arg(formatNumber(*low), unit)
                         : QString("%1-%2 %3").arg(formatNumber(*low), formatNumber(*high), unit);
}

QString FileModel::speedText() const {
    const std::vector<double> speeds = wordValues(machine_.analysis().spindleSpeeds);
    if (speeds.empty()) {
        return tr("None");
    }
    const auto [low, high] = std::minmax_element(speeds.begin(), speeds.end());
    return QString("%1-%2 RPM").arg(QString::fromStdString(js::numberToString(*low)),
                                    QString::fromStdString(js::numberToString(*high)));
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
    QVariantList files;
    for (const app::RecentFile& file : machine_.settings().recentFiles) {
        QVariantMap entry;
        entry["name"] = QString::fromStdString(file.fileName);
        entry["path"] = QString::fromStdString(file.filePath);
        files.append(entry);
    }
    return files;
}

QVariantMap FileModel::lastJob() const {
    const config::JobStats stats = config::JobStatsStore(machine_.config()).load();
    if (stats.jobs.empty()) {
        return {};
    }
    const config::JobRecord& job = stats.jobs.back();
    return {{"file", QString::fromStdString(job.file)},
            {"status", job.completed ? QStringLiteral("COMPLETE") : QStringLiteral("STOPPED")},
            {"duration", timeString(job.duration)}};
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
