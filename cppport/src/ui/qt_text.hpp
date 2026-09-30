#pragma once

// Conversions the view models share between the core's std::string world
// and Qt's.

#include "gs/util/jsnumber.hpp"

#include <QString>
#include <QUrl>

#include <string>

namespace gs::ui {

inline QString qstr(const std::string& text) {
    return QString::fromStdString(text);
}

// A number as JavaScript prints it (String(n)): what upstream shows.
inline QString jsNumber(double value) {
    return QString::fromStdString(js::numberToString(value));
}

// What a file dialog hands QML (a file: URL), or a plain path, as a path.
inline QString localPath(const QString& file) {
    const QUrl url(file);
    return url.isLocalFile() ? url.toLocalFile() : file;
}

}  // namespace gs::ui
