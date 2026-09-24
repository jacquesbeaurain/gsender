#include "diagnostics.hpp"

#include "machine.hpp"

#include "gs/config/history.hpp"
#include "gs/config/machine_profiles.hpp"
#include "gs/config/records.hpp"
#include "gs/controller/controller.hpp"
#include "gs/controller/locations.hpp"
#include "gs/transport/port_list.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/zip.hpp"

#include <QApplication>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QPageSize>
#include <QPdfWriter>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTextDocument>

#include <boost/json.hpp>

namespace gs::app {
namespace {

QString esc(const std::string& text) {
    return QString::fromStdString(text).toHtmlEscaped();
}

enum class Tone { Plain, Good, Bad, Warn };

// A "Label:" line and its value, coloured as upstream's status styles.
QString field(const QString& label, const QString& value, Tone tone = Tone::Plain) {
    static const char* const kColours[] = {"#111827", "#15803d", "#b91c1c", "#b45309"};
    return QString("<p><b>%1</b><br><span style='color:%2'>%3</span></p>")
        .arg(label.toHtmlEscaped(), kColours[static_cast<int>(tone)], value);
}

QString heading(const QString& id, const QString& text) {
    return QString("<h2><a name='%1'></a>%2</h2>").arg(id, text.toHtmlEscaped());
}

QString code(const QString& text) {
    return "<pre style='background:#f3f4f6; font-size:8pt'>" + text.toHtmlEscaped() + "</pre>";
}

// Jog presets in the workspace units, "N/A" for none.
QString jogPreset(const QString& name, const controller::JogSpeeds& speeds, bool metric) {
    const auto step = [metric](double mm) {
        return mm > 0 ? (metric ? QString::fromStdString(js::numberToString(mm)) + " mm"
                                : QString::fromStdString(js::toFixed(mm / 25.4, 3)) + " in")
                      : QStringLiteral("N/A");
    };
    const QString feed = speeds.feedrate > 0
                             ? (metric ? QString::fromStdString(js::numberToString(speeds.feedrate)) + " mm/min"
                                       : QString::fromStdString(js::toFixed(speeds.feedrate / 25.4, 1)) + " in/min")
                             : QStringLiteral("N/A");
    return QString("<p><b>%1:</b><br>XY Step: %2<br>Z Step: %3<br>Feedrate: %4<br>A Step: %5&deg;</p>")
        .arg(name, step(speeds.xyStep), step(speeds.zStep), feed,
             QString::fromStdString(js::numberToString(speeds.aStep)));
}

QString axes(const protocol::AxisValues& values, const QString& unit) {
    QStringList lines;
    const char* const names = "XYZABC";
    for (std::size_t i = 0; i < values.count && i < 6; ++i) {
        lines << QString("%1: %2%3").arg(QChar(names[i])).arg(QString::fromStdString(js::toFixed(values[i], 3)))
                     .arg(i < 3 ? unit : QStringLiteral("&deg;"));
    }
    return lines.join("<br>");
}

}  // namespace

QString diagnosticsStamp(const QDateTime& when) {
    // toLocaleDateString() with '-' for '/' (en-US) and it-IT's time.
    return when.date().toString("M-d-yyyy") + "_" + when.time().toString("HH-mm-ss");
}

QString diagnosticsReport(Machine& machine, const QStringList& consoleHistory, const QDateTime& when) {
    const AppSettings& settings = machine.settings();
    controller::Controller* c = machine.controller();
    const bool connected = machine.isConnected() && c;
    const auto eeprom = [c](const char* key) { return c ? c->runner().setting(key) : std::string(); };
    const QString notConnected = QObject::tr("Not Connected");
    const auto onOff = [&](bool on) { return on ? QObject::tr("Enabled") : QObject::tr("Disabled"); };

    QString html = "<html><body style='font-family:sans-serif; font-size:10pt'>";
    html += "<h1>" + QObject::tr("Diagnostics Report") + "</h1>";
    html += "<p style='color:#6b7280'>" +
            QObject::tr("gSender (C++) v%1 &bull; Generated on %2")
                .arg(QApplication::applicationVersion().toHtmlEscaped(),
                     QLocale().toString(when.date(), QLocale::ShortFormat)) +
            "</p>";
    html += "<p><b>" + QObject::tr("Quick Navigation") + "</b><br>";
    for (const auto& [id, text] : std::initializer_list<std::pair<const char*, const char*>>{
             {"environment", "Environment &amp; Machine Profile"},
             {"connection", "Connection &amp; Controller Status"},
             {"preferences", "Preferences &amp; Settings"},
             {"automations", "Automations"},
             {"firmware", "Firmware Settings"},
             {"alerts", "Errors and Alarms"},
             {"terminal", "Terminal History"},
             {"gcode", "G-Code File Status"}}) {
        html += QString("<a href='#%1'>%2</a><br>").arg(id, text);
    }
    html += "</p>";

    // ---- Environment ----
    html += heading("environment", QObject::tr("Environment"));
    html += field(QObject::tr("Operating System:"), QSysInfo::prettyProductName().toHtmlEscaped());
    const double homing = js::stringToNumber(eeprom("$22"));
    const bool homingOn = std::isfinite(homing) && static_cast<long long>(homing) % 2 == 1;
    html += field(QObject::tr("Homing:"), connected ? onOff(homingOn) : notConnected,
                  connected && homingOn ? Tone::Good : Tone::Bad);
    html += field(QObject::tr("Soft Limits:"), connected ? onOff(eeprom("$20") == "1") : notConnected,
                  connected && eeprom("$20") == "1" ? Tone::Good : Tone::Bad);
    html += field(QObject::tr("Home Location:"), connected ? esc(controller::homingString(eeprom("$23"))) : notConnected,
                  connected ? Tone::Plain : Tone::Bad);
    html += field(QObject::tr("Report Inches:"), connected ? onOff(eeprom("$13") == "1") : notConnected,
                  connected && eeprom("$13") == "1" ? Tone::Good : Tone::Bad);
    html += field(QObject::tr("Stepper Motors:"),
                  connected ? (eeprom("$1") == "255" ? QObject::tr("Locked") : QObject::tr("Unlocked")) : notConnected,
                  !connected ? Tone::Bad : eeprom("$1") == "255" ? Tone::Warn : Tone::Good);

    // ---- Machine Profile ----
    const config::MachineProfile& profile = machine.machineProfile();
    html += heading("machine-profile", QObject::tr("Machine Profile"));
    html += field(QObject::tr("ID:"), QString::number(profile.id));
    html += field(QObject::tr("Company:"), esc(profile.company));
    html += field(QObject::tr("Name:"), esc(profile.name));
    html += field(QObject::tr("Type:"), esc(profile.type));
    html += field(QObject::tr("Version:"), esc(profile.version));
    html += field(QObject::tr("Work Area:"), QString("X: %1mm<br>Y: %2mm<br>Z: %3mm")
                                                 .arg(QString::fromStdString(js::numberToString(profile.width)),
                                                      QString::fromStdString(js::numberToString(profile.depth)),
                                                      QString::fromStdString(js::numberToString(profile.height))));
    html += field(QObject::tr("Spindle/Laser:"),
                  settings.spindleFunctions ? QObject::tr("Available") : QObject::tr("Not Available"),
                  settings.spindleFunctions ? Tone::Good : Tone::Bad);
    html += field(QObject::tr("Laser Mode:"), onOff(machine.laserMode()), machine.laserMode() ? Tone::Good : Tone::Bad);

    // ---- Connection ----
    html += heading("connection", QObject::tr("Connection"));
    const QString port = machine.port();
    html += field(QObject::tr("Connected Port:"),
                  connected && !port.isEmpty() ? port.toHtmlEscaped() : QObject::tr("Not connected"));
    html += field(QObject::tr("Baudrate:"), connected ? QString::number(settings.baudRate) : QStringLiteral("N/A"));
    const std::vector<transport::SerialPortInfo> ports = transport::listSerialPorts();
    QStringList available;
    QStringList unrecognized;
    QString manufacturer;
    for (const transport::SerialPortInfo& info : ports) {
        const QString path = QString::fromStdString(info.path);
        available << path.toHtmlEscaped();
        if (!transport::isRecognizedPort(info.vendorId, info.productId)) {
            unrecognized << "&bull; " + path.toHtmlEscaped();
        }
        if (path == port) {
            manufacturer = QString::fromStdString(info.manufacturer);
        }
    }
    if (connected && !port.isEmpty()) {
        html += field(QObject::tr("Manufacturer:"),
                      manufacturer.isEmpty() ? QObject::tr("Unknown") : manufacturer.toHtmlEscaped());
    }
    html += field(QObject::tr("Available Ports:"),
                  available.isEmpty() ? QObject::tr("None detected") : available.join(", "));
    if (!unrecognized.isEmpty()) {
        html += field(QObject::tr("Unrecognized Ports:"), unrecognized.join("<br>"));
    }

    // ---- Controller Status ----
    html += heading("controller-status", QObject::tr("Controller Status"));
    if (connected) {
        const auto& firmware = c->runner().settings();
        const auto board = firmware.info.find("BOARD");
        html += field(QObject::tr("Type:"), QString::fromStdString(std::string(protocol::firmwareName(c->firmware()))));
        html += field(QObject::tr("Board:"), board == firmware.info.end() ? QObject::tr("Unknown") : esc(board->second.text));
        html += field(QObject::tr("Firmware:"), firmware.version.empty() ? QStringLiteral("disconnected")
                                                                         : esc(firmware.version));
        const bool idle = c->workflow().isIdle();
        html += field(QObject::tr("Workflow State:"),
                      idle ? QStringLiteral("Idle") : c->workflow().isRunning() ? QStringLiteral("Running")
                                                                                 : QStringLiteral("Paused"),
                      idle ? Tone::Good : Tone::Warn);
        html += field(QObject::tr("Homing Status:"), c->homingFlag() ? QObject::tr("Homed") : QObject::tr("Not Homed"),
                      c->homingFlag() ? Tone::Good : Tone::Bad);
        html += field(QObject::tr("Machine Position:"), axes(c->runner().machinePosition(), "mm"));
        html += field(QObject::tr("Work Position:"), axes(c->runner().workPosition(), "mm"));
    } else {
        html += field(QString(), notConnected, Tone::Bad);
    }

    // ---- Preferences & Settings ----
    html += heading("preferences", QObject::tr("Preferences & Settings"));
    html += field(QObject::tr("Workspace Units:"),
                  settings.metric ? QObject::tr("Metric (mm)") : QObject::tr("Imperial (inches)"),
                  settings.metric ? Tone::Good : Tone::Warn);
    html += field(QObject::tr("Safeheight:"), QString::fromStdString(js::numberToString(settings.safeRetractHeight)));
    html += field(QObject::tr("Laser Mode:"), onOff(machine.laserMode()), machine.laserMode() ? Tone::Good : Tone::Bad);
    html += field(QObject::tr("Rotary Mode:"), onOff(settings.rotary.rotaryMode),
                  settings.rotary.rotaryMode ? Tone::Good : Tone::Bad);
    if (settings.rotary.rotaryMode) {
        html += field(QObject::tr("Rotary Settings:"),
                      connected ? QString("Travel Resolution: Y=%1%2<br>Max Rate: Y=%3%4")
                                      .arg(esc(eeprom("$101")),
                                           c->isGrblHal() ? "<br>A=" + esc(eeprom("$103")) : QString(),
                                           esc(eeprom("$111")),
                                           c->isGrblHal() ? ", A=" + esc(eeprom("$113")) : QString())
                                : notConnected);
    }
    html += "<p><b>" + QObject::tr("Jog Presets:") + "</b></p>";
    html += jogPreset(QObject::tr("Rapid"), settings.jog.rapid, settings.metric);
    html += jogPreset(QObject::tr("Normal"), settings.jog.normal, settings.metric);
    html += jogPreset(QObject::tr("Precise"), settings.jog.precise, settings.metric);

    // ---- Automations ----
    html += heading("automations", QObject::tr("Automations"));
    const config::EventStore hooks(machine.config());
    for (const auto& [event, label] : std::initializer_list<std::pair<const char*, const char*>>{
             {"gcode:start", "File Start:"},
             {"gcode:pause", "File Pause:"},
             {"gcode:resume", "File Resume:"},
             {"gcode:stop", "File Stop/End:"}}) {
        const auto hook = hooks.find(event);
        const bool on = hook && hook->enabled;
        html += field(QString::fromLatin1(label), onOff(on), on ? Tone::Good : Tone::Warn);
        html += hook && !hook->commands.empty() ? code(QString::fromStdString(hook->commands)) : QString("<p>N/A</p>");
    }

    // ---- Firmware Settings: against the profile's defaults ----
    html += heading("firmware", QObject::tr("Firmware Settings"));
    if (connected) {
        html += "<table border=1 cellspacing=0 cellpadding=3 width='100%'><tr style='background:#e5e7eb'><th>Setting</th>"
                "<th>Current Value</th><th>Default Value</th></tr>";
        const config::BoardContext board = machine.boardContext();
        for (const auto& [key, value] : c->runner().settings().settings.items()) {
            const std::optional<std::string> fallback = config::defaultValue(profile, board, key);
            // Highlighted only against a known default.
            const bool different = fallback && !config::isDefaultValue(value, fallback);
            html += QString("<tr%1><td>%2</td><td>%3</td><td>%4</td></tr>")
                        .arg(different ? " style='background:#fef3c7'" : "", esc(key), esc(value),
                             fallback ? esc(*fallback) : QStringLiteral("-"));
        }
        html += "</table>";
    } else {
        html += field(QString(), notConnected, Tone::Bad);
    }

    // ---- Errors and Alarms ----
    html += heading("alerts", QObject::tr("Errors and Alarms"));
    std::vector<config::AlarmRecord> alarms;
    std::vector<config::AlarmRecord> errors;
    for (config::AlarmRecord& record : config::AlarmHistory(machine.config()).list()) {
        (record.alarm ? alarms : errors).push_back(std::move(record));
    }
    const auto listing = [](const QString& title, const std::vector<config::AlarmRecord>& records,
                            const QString& none, const char* colour) {
        QString out = QString("<p><b>%1 (%2)</b></p>").arg(title).arg(records.size());
        if (records.empty()) {
            return out + "<p>" + none + "</p>";
        }
        for (const config::AlarmRecord& record : records) {
            out += QString("<p style='background:%1'><b>%2</b><br>%3<br>Input: %4<br>Controller: %5</p>")
                       .arg(colour,
                            QDateTime::fromMSecsSinceEpoch(record.time).toString(Qt::TextDate).toHtmlEscaped(),
                            esc(record.message), esc(record.line), esc(record.controller));
        }
        return out;
    };
    html += listing(QObject::tr("All Alarms"), alarms, QObject::tr("No alarms recorded"), "#fee2e2");
    html += listing(QObject::tr("All Errors"), errors, QObject::tr("No errors recorded"), "#fef3c7");

    // ---- Terminal History ----
    html += heading("terminal", QObject::tr("Terminal History"));
    html += connected ? code(consoleHistory.isEmpty() ? QObject::tr("No terminal history available")
                                                      : consoleHistory.join('\n'))
                      : field(QString(), notConnected, Tone::Bad);

    // ---- G-Code File Status ----
    html += heading("gcode", QObject::tr("G-Code File Status"));
    if (machine.hasProgram() && c) {
        const controller::SenderStatus status = c->sender().status();
        const int progress = status.total > 0 ? static_cast<int>(std::lround(100.0 * status.sent / status.total)) : 0;
        html += "<p><b>" + QObject::tr("File Information") + "</b><br>" +
                QObject::tr("Name: %1").arg(machine.programName().toHtmlEscaped()) + "<br>" +
                QObject::tr("Total Lines: %1").arg(status.total) + "<br>" + QObject::tr("Lines Sent: %1").arg(status.sent) +
                "<br>" + QObject::tr("Remaining: %1").arg(status.remainingTime) + "<br>" +
                QObject::tr("Progress: <span style='color:%1'>%2% Complete</span>")
                    .arg(progress == 100 ? "#15803d" : "#b45309")
                    .arg(progress) +
                "</p>";
        const std::string& text = machine.programText();
        html += "<p><b>" + QObject::tr("Full G-Code Content") + "</b></p>" +
                code(QString::fromStdString(text.substr(0, 2000)) +
                     (text.size() > 2000 ? QObject::tr("\n\n... (truncated for file size)") : QString()));
    } else {
        html += "<p>" + QObject::tr("No G-code file loaded or no sender status available") + "</p>";
    }
    return html + "</body></html>";
}

QByteArray diagnosticsPdf(const QString& html) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    {
        QPdfWriter writer(&buffer);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(96);
        writer.setTitle(QObject::tr("Diagnostics Report"));
        writer.setCreator(QApplication::applicationName());
        QTextDocument document;
        document.setHtml(html);
        document.print(&writer);
    }
    return bytes;
}

bool writeDiagnostics(Machine& machine, const QStringList& consoleHistory, const QString& path, QString* error,
                      const QDateTime& when) {
    const QString stamp = diagnosticsStamp(when);
    std::vector<zip::Entry> entries;
    // The loaded file, as it was loaded.
    if (machine.hasProgram() && !machine.programText().empty()) {
        entries.push_back({machine.programName().toStdString(), machine.programText()});
    }
    const QByteArray pdf = diagnosticsPdf(diagnosticsReport(machine, consoleHistory, when));
    entries.push_back({"diagnostics_" + stamp.toStdString() + ".pdf", pdf.toStdString()});
    // The firmware's settings, as Export writes them (JSON.stringify(..., 1)).
    boost::json::object eeprom;
    if (controller::Controller* c = machine.controller()) {
        for (const auto& [key, value] : c->runner().settings().settings.items()) {
            eeprom[key] = value;
        }
    }
    std::string eepromJson = "{";
    bool first = true;
    for (const auto& [key, value] : eeprom) {
        eepromJson += std::string(first ? "\n" : ",\n") + " " + boost::json::serialize(boost::json::string(key)) +
                      ": " + boost::json::serialize(value);
        first = false;
    }
    eepromJson += first ? "}" : "\n}";
    const QString dashed = QString(stamp).replace('_', '-');
    entries.push_back({"gSender-firmware-settings-" + dashed.toStdString() + ".json", eepromJson});
    // The application's settings, as Settings > Export writes them.
    QTemporaryDir scratch;
    const QString settingsFile = scratch.filePath("settings.json");
    QString exportError;
    if (!scratch.isValid() || !machine.exportSettings(settingsFile, &exportError)) {
        if (error) {
            *error = exportError.isEmpty() ? QObject::tr("the settings could not be exported") : exportError;
        }
        return false;
    }
    QFile exported(settingsFile);
    if (!exported.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = exported.errorString();
        }
        return false;
    }
    entries.push_back({"gSenderSettings_" + stamp.toStdString() + ".json", exported.readAll().toStdString()});

    const zip::DosTime modified{when.date().year(), when.date().month(),  when.date().day(),
                                when.time().hour(), when.time().minute(), when.time().second()};
    const std::string archive = zip::archive(entries, modified);
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) ||
        out.write(archive.data(), static_cast<qint64>(archive.size())) != static_cast<qint64>(archive.size())) {
        if (error) {
            *error = out.errorString();
        }
        return false;
    }
    return true;
}

}  // namespace gs::app
