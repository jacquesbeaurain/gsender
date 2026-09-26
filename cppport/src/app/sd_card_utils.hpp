#pragma once

// SD card file utilities (features/SDCard): filename validation, file filters,
// size formatting, and batch checking.

#include <QString>
#include <QStringList>
#include <optional>

namespace gs::app {

// The file dialog's filter, as the upload's accept attribute.
QString sdFileFilter();
// "Some files were rejected:" and each "name: reason".
QString sdRefusedText(const QStringList& refused);
// validateSDFilename(): the firmware's filename_valid() - the reason a name
// is refused, or nullopt.
std::optional<QString> sdFilenameProblem(const QString& name);
// ACCEPTED_EXTENSIONS: the CNC files the card lists, macros and JSON.
bool isAcceptedSdFile(const QString& name);
// formatFileSize(): B, KB, MB or GB, one decimal above bytes.
QString formatSdFileSize(long long bytes);
// isFileATCIRelated(): the tool changer's own macros (ATC templates are not
// ported, so only its two fixed names).
bool isAtciFile(const QString& name);

// The files an upload takes: the accepted ones, and "name: reason" for the
// others.
struct SdFileCheck {
    QStringList accepted;
    QStringList refused;
};
SdFileCheck checkSdFiles(const QStringList& paths);

}  // namespace gs::app
