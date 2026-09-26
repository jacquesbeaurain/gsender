#include "sd_card_utils.hpp"

#include "gs/util/jsnumber.hpp"

#include <QFileInfo>
#include <QObject>

namespace gs::app {

namespace {

const QStringList kAcceptedExtensions{".gcode", ".nc",   ".ncc", ".ngc",   ".cnc",
                                      ".txt",   ".text", ".tap", ".macro", ".json"};

}  // namespace

QString sdRefusedText(const QStringList& refused) {
    return QObject::tr("Some files were rejected:\n%1").arg(refused.join('\n'));
}

QString sdFileFilter() {
    QStringList patterns;
    for (const QString& extension : kAcceptedExtensions) {
        patterns << "*" + extension;
    }
    return QObject::tr("SD card files (%1)").arg(patterns.join(' '));
}

std::optional<QString> sdFilenameProblem(const QString& name) {
    if (name.size() > 40) {
        return QObject::tr("Filename too long (max 40 characters)");
    }
    for (const QChar c : {QChar('?'), QChar('~'), QChar('!')}) {
        if (name.contains(c)) {
            return QObject::tr("Filename contains invalid character: %1").arg(c);
        }
    }
    return std::nullopt;
}

bool isAcceptedSdFile(const QString& name) {
    // '.' + the text after the last dot, lower case.
    return kAcceptedExtensions.contains("." + name.section('.', -1).toLower());
}

QString formatSdFileSize(long long bytes) {
    static const char* kUnits[] = {"B", "KB", "MB", "GB"};
    double size = static_cast<double>(bytes);
    int unit = 0;
    while (size >= 1024 && unit < 3) {
        size /= 1024;
        ++unit;
    }
    return QString::fromStdString(js::toFixed(size, unit == 0 ? 0 : 1)) + " " + kUnits[unit];
}

bool isAtciFile(const QString& name) {
    return name == "ATCI.macro" || name == "P100.macro";
}

SdFileCheck checkSdFiles(const QStringList& paths) {
    SdFileCheck check;
    for (const QString& path : paths) {
        const QString name = QFileInfo(path).fileName();
        if (!isAcceptedSdFile(name)) {
            check.refused << QObject::tr("%1: Invalid file type").arg(name);
        } else if (const std::optional<QString> problem = sdFilenameProblem(name)) {
            check.refused << name + ": " + *problem;
        } else {
            check.accepted << path;
        }
    }
    return check;
}

}  // namespace gs::app
