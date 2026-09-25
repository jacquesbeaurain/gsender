#include "macros_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/config/records.hpp"
#include "gs/controller/controller.hpp"

#include <QDate>
#include <QFile>
#include <QUrl>

#include <boost/json.hpp>

#include <algorithm>

namespace gs::ui {
namespace {

QString localPath(const QString& file) {
    const QUrl url(file);
    return url.isLocalFile() ? url.toLocalFile() : file;
}

// The stored macros in a column, by row (computeColumn).
std::vector<config::MacroRecord> inColumn(std::vector<config::MacroRecord> macros, const std::string& column) {
    std::erase_if(macros, [&](const config::MacroRecord& m) { return m.column != column; });
    std::stable_sort(macros.begin(), macros.end(),
                     [](const config::MacroRecord& a, const config::MacroRecord& b) { return a.rowIndex < b.rowIndex; });
    return macros;
}

}  // namespace

MacrosModel::MacrosModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    connect(&machine_, &app::Machine::macrosChanged, this, &MacrosModel::macrosChanged);
    for (auto signal : {&app::Machine::stateChanged, &app::Machine::workflowChanged, &app::Machine::connectionChanged}) {
        connect(&machine_, signal, this, &MacrosModel::stateChanged);
    }
}

QVariantList MacrosModel::column(const QString& name) const {
    QVariantList list;
    for (const config::MacroRecord& m : inColumn(machine_.macros().list(), name.toStdString())) {
        list.append(QVariantMap{{"id", QString::fromStdString(m.id)},
                                {"name", QString::fromStdString(m.name)},
                                {"description", QString::fromStdString(m.description).trimmed()}});
    }
    return list;
}

int MacrosModel::count() const {
    return static_cast<int>(machine_.macros().list().size());
}

bool MacrosModel::canRun() const {
    controller::Controller* c = machine_.controller();
    if (!c || c->workflow().isRunning()) {
        return false;
    }
    const std::string& state = c->state().status.activeState;
    return state == "Idle" || state == "Run";
}

QVariantList MacrosModel::variables() const {
    static const std::pair<const char*, const char*> kVariables[] = {
        {"%wait", "Wait until the planner queue is empty"},
        {"%global.tool = Number(tool) || 0\n", "User-defined global variables"},
        {"(tool=[global.tool])\n", "Display a global variable using an inline comment"},
        {"%X0=posx,Y0=posy,Z0=posz\n", "Keep a backup of current work position"},
        {"G0 X[X0] Y[Y0]\n", "Go to previous work position"},
        {"G0 Z[Z0]\n", "Go to previous work position"},
        {"%prevTool = Number(global.tool) || 0, global.tool = tool\n", "Tool change"},
        {"%WCS=modal.wcs\n", "Save modal state"},
        {"%PLANE=modal.plane\n", "Save modal state"},
        {"%UNITS=modal.units\n", "Save modal state"},
        {"%DISTANCE=modal.distance\n", "Save modal state"},
        {"%FEEDRATE=modal.feedrate\n", "Save modal state"},
        {"%SPINDLE=modal.spindle\n", "Save modal state"},
        {"%COOLANT=modal.coolant\n", "Save modal state"},
        {"[WCS] [PLANE] [UNITS] [DISTANCE] [FEEDRATE] [SPINDLE] [COOLANT]\n", "Restore modal state"},
        {"[posx]", "Current work position"},
        {"[posy]", "Current work position"},
        {"[posz]", "Current work position"},
        {"[posa]", "Current work position"},
        {"%xmin=0,xmax=100,ymin=0,ymax=100,zmin=0,zmax=50\n", "Set bounding box"},
        {"$#\n%wait", "Request current Parameters - must do before using parameters"},
        {"%PROBE_X=params.PRB.x\n", "Get current Parameters"},
        {"%PROBE_Y=params.PRB.y\n", "Get current Parameters"},
        {"%PROBE_Z=params.PRB.z\n", "Get current Parameters"},
        {"%G54_Z=params.G54.z\n", "Get current Parameters"},
        {"%G57_Z=params.G57.z\n", "Get current Parameters"},
        {"%TOOL_OFFSET=params.TLO\n", "Get current Parameters"},
    };
    QVariantList list;
    for (const auto& [text, group] : kVariables) {
        list.append(QVariantMap{{"text", QString::fromUtf8(text)}, {"group", QString::fromUtf8(group)}});
    }
    return list;
}

QVariantMap MacrosModel::macro(const QString& id) const {
    const std::optional<config::MacroRecord> m = machine_.macros().find(id.toStdString());
    if (!m) {
        return {};
    }
    return {{"id", QString::fromStdString(m->id)},
            {"name", QString::fromStdString(m->name)},
            {"content", QString::fromStdString(m->content)},
            {"description", QString::fromStdString(m->description).trimmed()}};
}

bool MacrosModel::run(const QString& id) {
    controller::Controller* c = machine_.controller();
    if (!c || !canRun()) {
        return false;
    }
    // With the file's box ([xmin] ...), as upstream's macro:run.
    return c->runMacro(id.toStdString(), machine_.fileContext());
}

QString MacrosModel::add(const QString& name, const QString& content, const QString& description) {
    if (name.trimmed().isEmpty() || content.trimmed().isEmpty()) {
        return tr("A macro needs a name and G-code.");
    }
    if (!machine_.macros().create(name.trimmed().left(kMaxCharacters).toStdString(), content.toStdString(),
                                  description.left(kMaxCharacters).toStdString())) {
        return tr("Failed to add macro");
    }
    Q_EMIT machine_.macrosChanged();
    return {};
}

QString MacrosModel::update(const QString& id, const QString& name, const QString& content,
                            const QString& description) {
    if (name.trimmed().isEmpty() || content.trimmed().isEmpty()) {
        return tr("A macro needs a name and G-code.");
    }
    config::MacroStore store = machine_.macros();
    if (!store.update(id.toStdString(), config::MacroChanges{.name = name.trimmed().left(kMaxCharacters).toStdString(),
                                                             .content = content.toStdString(),
                                                             .description = description.left(kMaxCharacters).toStdString()})) {
        return tr("Failed to update macro");
    }
    Q_EMIT machine_.macrosChanged();
    return {};
}

void MacrosModel::remove(const QString& id) {
    if (machine_.macros().remove(id.toStdString())) {
        Q_EMIT machine_.macrosChanged();
    }
}

void MacrosModel::move(const QString& id, const QString& column, int index) {
    const std::vector<config::MacroRecord> all = machine_.macros().list();
    const auto moving = std::find_if(all.begin(), all.end(), [&](const auto& m) { return m.id == id.toStdString(); });
    if (moving == all.end() || (column != "column1" && column != "column2")) {
        return;
    }
    config::MacroRecord moved = *moving;
    const std::string from = moved.column;
    moved.column = column.toStdString();
    // Both columns renumbered (setRowIndices), the moved one at its place.
    std::vector<config::MacroRecord> changed;
    for (const std::string& name : {std::string("column1"), std::string("column2")}) {
        std::vector<config::MacroRecord> items = inColumn(all, name);
        std::erase_if(items, [&](const auto& m) { return m.id == moved.id; });
        if (name == moved.column) {
            const auto at = static_cast<std::ptrdiff_t>(std::clamp<int>(index, 0, static_cast<int>(items.size())));
            items.insert(items.begin() + at, moved);
        }
        for (std::size_t row = 0; row < items.size(); ++row) {
            items[row].rowIndex = static_cast<int>(row);
            changed.push_back(items[row]);
        }
    }
    machine_.macros().bulkUpdate(changed);
    Q_EMIT machine_.macrosChanged();
}

QVariantMap MacrosModel::importFile(const QString& file) {
    QFile in(localPath(file));
    if (!in.open(QIODevice::ReadOnly)) {
        return {{"ok", false}, {"message", tr("Error Importing Macros")}};
    }
    const QByteArray bytes = in.readAll();
    boost::system::error_code error;
    const boost::json::value macros =
        boost::json::parse(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), error);
    if (error || !macros.is_array()) {
        return {{"ok", false}, {"message", tr("Error Importing Macros")}};
    }
    config::MacroStore store = machine_.macros();
    const config::MacroImport result = config::importMacros(store, macros);
    Q_EMIT machine_.macrosChanged();
    QString message;
    if (result.imported > 0) {
        message = tr("Successfully imported %1 macro(s)").arg(result.imported) +
                  (result.updated > 0 ? tr(", updated %1 existing macro(s)").arg(result.updated) : QString());
    } else if (result.updated > 0) {
        message = tr("Updated %1 existing macro(s)").arg(result.updated);
    }
    return {{"ok", true}, {"message", message}};
}

QVariantMap MacrosModel::exportFile(const QString& file) {
    config::MacroStore store = machine_.macros();
    if (store.list().empty()) {
        return {{"ok", false}, {"message", tr("No Macros to Export")}};
    }
    QFile out(localPath(file));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {{"ok", false}, {"message", tr("Cannot write %1: %2").arg(out.fileName(), out.errorString())}};
    }
    const std::string text = boost::json::serialize(config::exportMacros(store));
    out.write(text.data(), static_cast<qint64>(text.size()));
    return {{"ok", true}, {"message", tr("Exported %1 macro(s)").arg(store.list().size())}};
}

QString MacrosModel::exportName() const {
    return QString("gSender-macros-%1.json").arg(QDate::currentDate().toString(Qt::ISODate));
}

}  // namespace gs::ui
