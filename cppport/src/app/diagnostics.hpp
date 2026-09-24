#pragma once

// The diagnostics support file (lib/diagnostics.tsx, the Stats page's
// "Download Diagnostic File"): a ZIP of a PDF report - environment, machine
// profile, connection, controller, preferences, automations, the firmware
// settings against the profile's defaults, the recorded alarms and
// errors, the console's history, the loaded file - with the firmware
// settings and the application settings as JSON, and the loaded file.

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace gs::app {

class Machine;

// The report, as HTML (the PDF is printed from it).
QString diagnosticsReport(Machine& machine, const QStringList& consoleHistory, const QDateTime& when);
// The report printed to a PDF.
QByteArray diagnosticsPdf(const QString& html);
// "diagnostics_<M-d-yyyy>_<HH-mm-ss>.zip" and the like.
QString diagnosticsStamp(const QDateTime& when);
// Writes the ZIP to `path`; false with the reason in `error`.
bool writeDiagnostics(Machine& machine, const QStringList& consoleHistory, const QString& path, QString* error,
                      const QDateTime& when = QDateTime::currentDateTime());

}  // namespace gs::app
