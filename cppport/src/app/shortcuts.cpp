#include "shortcuts.hpp"

#include "machine.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace gs::app {
namespace {

const QString kGeneral = QStringLiteral("General");
const QString kLocation = QStringLiteral("Location");
const QString kCarving = QStringLiteral("Carving");
const QString kJogging = QStringLiteral("Jogging");
const QString kVisualizer = QStringLiteral("Visualizer");
const QString kOverrides = QStringLiteral("Overrides");
const QString kCoolant = QStringLiteral("Coolant");
const QString kSpindle = QStringLiteral("Spindle/Laser");
const QString kProbing = QStringLiteral("Probing");
const QString kToolbar = QStringLiteral("Toolbar");

}  // namespace

const std::vector<ShortcutAction>& shortcutActions() {
    // Upstream's defaults: workspace/index.tsx, DRO (and its Parking), JobControl, FileControl,
    // Jogging (and its SpeedSelector), Visualizer, Probe.
    static const std::vector<ShortcutAction> actions{
        {"CONTROLLER_COMMAND_UNLOCK", "Unlock", kGeneral, "$"},
        {"CONTROLLER_COMMAND_RESET", "Soft reset", kGeneral, "%"},
        {"CONTROLLER_COMMAND_REALTIME_REPORT", "Realtime report", kGeneral, "`", false, true},
        {"CONTROLLER_COMMAND_ERROR_CLEAR", "Clear error", kGeneral, "*", false, true},
        {"CONTROLLER_COMMAND_TOOLCHANGE_ACKNOWLEDGEMENT", "Acknowledge tool change", kGeneral, "Ctrl+Alt+Meta+A",
         false, true},
        {"CONTROLLER_COMMAND_VIRTUAL_STOP_TOGGLE", "Feed hold", kGeneral, "Ctrl+8", false, true},
        {"CONTROLLER_COMMAND_HOMING", "Home machine", kLocation, "Ctrl+Alt+Meta+H"},
        {"ZERO_X_AXIS", "Zero X-axis", kLocation, "Shift+W"},
        {"ZERO_Y_AXIS", "Zero Y-axis", kLocation, "Shift+E"},
        {"ZERO_Z_AXIS", "Zero Z-axis", kLocation, "Shift+R"},
        {"ZERO_A_AXIS", "Zero A-axis", kLocation, "Shift+Y"},
        {"ZERO_ALL_AXIS", "Zero all axes", kLocation, "Shift+Q"},
        {"GO_TO_A_AXIS_ZERO", "Go to A zero", kLocation, "Shift+T"},
        {"GO_TO_X_AXIS_ZERO", "Go to X zero", kLocation, "Shift+S"},
        {"GO_TO_Y_AXIS_ZERO", "Go to Y zero", kLocation, "Shift+D"},
        {"GO_TO_Z_AXIS_ZERO", "Go to Z zero", kLocation, "Shift+F"},
        {"GO_TO_XY_AXIS_ZERO", "Go to XY zero", kLocation, "Shift+A"},
        {"HOMING_GO_TO_BACK_LEFT_CORNER", "Go to Back Left corner", kLocation, ""},
        {"HOMING_GO_TO_BACK_RIGHT_CORNER", "Go to Back Right corner", kLocation, ""},
        {"HOMING_GO_TO_FRONT_LEFT_CORNER", "Go to Front Left corner", kLocation, ""},
        {"HOMING_GO_TO_FRONT_RIGHT_CORNER", "Go to Front Right corner", kLocation, ""},
        {"HOMING_PARK", "Park", kLocation, ""},
        {"START_JOB", "Start job", kCarving, "~"},
        {"PAUSE_JOB", "Pause job", kCarving, "!"},
        {"STOP_JOB", "Global Stop", kCarving, "@"},
        {"RUN_OUTLINE", "Run outline", kCarving, ""},
        {"LOAD_FILE", "Load file", kCarving, "Shift+L"},
        {"UNLOAD_FILE", "Unload file", kCarving, "Shift+K"},
        {"JOG_X_P", "Jog X+ (right)", kJogging, "Shift+Right", true},
        {"JOG_X_M", "Jog X- (left)", kJogging, "Shift+Left", true},
        {"JOG_Y_P", "Jog Y+ (back)", kJogging, "Shift+Up", true},
        {"JOG_Y_M", "Jog Y- (fwd)", kJogging, "Shift+Down", true},
        {"JOG_Z_P", "Jog Z+ (up)", kJogging, "Shift+PgUp", true},
        {"JOG_Z_M", "Jog Z- (down)", kJogging, "Shift+PgDown", true},
        {"JOG_X_P_Y_M", "Jog X+ Y-", kJogging, "", true},
        {"JOG_X_M_Y_P", "Jog X- Y+", kJogging, "", true},
        {"JOG_X_Y_P", "Jog X+ Y+", kJogging, "", true},
        {"JOG_X_Y_M", "Jog X- Y-", kJogging, "", true},
        {"JOG_A_PLUS", "Jog A+ (CCW)", kJogging, "Ctrl+6", true},
        {"JOG_A_MINUS", "Jog A- (CW)", kJogging, "Ctrl+4", true},
        {"STOP_CONT_JOG", "Stop Continuous Jog", kJogging, ""},
        {"SET_R_JOG_PRESET", "Set to Rapid Preset", kJogging, "Shift+V"},
        {"SET_N_JOG_PRESET", "Set to Normal Preset", kJogging, "Shift+C"},
        {"SET_P_JOG_PRESET", "Set to Precise Preset", kJogging, "Shift+X"},
        {"CYCLE_JOG_PRESETS", "Switch between Presets", kJogging, "Shift+Z"},
        {"FEEDRATE_OVERRIDE_P", "Feed +", kOverrides, ""},
        {"FEEDRATE_OVERRIDE_PP", "Feed ++", kOverrides, ""},
        {"FEEDRATE_OVERRIDE_M", "Feed -", kOverrides, ""},
        {"FEEDRATE_OVERRIDE_MM", "Feed --", kOverrides, ""},
        {"FEEDRATE_OVERRIDE_RESET", "Feed reset", kOverrides, ""},
        {"SPINDLE_OVERRIDE_P", "Spindle/Laser +", kOverrides, ""},
        {"SPINDLE_OVERRIDE_PP", "Spindle/Laser ++", kOverrides, ""},
        {"SPINDLE_OVERRIDE_M", "Spindle/Laser -", kOverrides, ""},
        {"SPINDLE_OVERRIDE_MM", "Spindle/Laser --", kOverrides, ""},
        {"SPINDLE_OVERRIDE_RESET", "Spindle/Laser reset", kOverrides, ""},
        {"VISUALIZER_VIEW_3D", "See 3D view", kVisualizer, ""},
        {"VISUALIZER_VIEW_TOP", "See Top view", kVisualizer, ""},
        {"VISUALIZER_VIEW_FRONT", "See Front view", kVisualizer, ""},
        {"VISUALIZER_VIEW_RIGHT", "See Right view", kVisualizer, ""},
        {"VISUALIZER_VIEW_LEFT", "See Left view", kVisualizer, ""},
        {"VISUALIZER_VIEW_RESET", "Reset view", kVisualizer, "Shift+N"},
        {"VISUALIZER_VIEW_CYCLE", "Switch between views", kVisualizer, "Shift+B"},
        {"VISUALIZER_ZOOM_IN", "Zoom in", kVisualizer, "Shift+P"},
        {"VISUALIZER_ZOOM_OUT", "Zoom out", kVisualizer, "Shift+O"},
        {"VISUALIZER_ZOOM_FIT", "Zoom fit", kVisualizer, "Shift+I"},
        {"TOGGLE_SHORTCUTS", "Toggle on/off shortcuts", kVisualizer, "^"},
        {"MIST_COOLANT", "Mist coolant (M7)", kCoolant, ""},
        {"FLOOD_COOLANT", "Flood coolant (M8)", kCoolant, ""},
        {"STOP_COOLANT", "Stop coolant (M9)", kCoolant, ""},
        {"CW_LASER_ON", "CW / Laser On", kSpindle, ""},
        {"CCW_LASER_TEST", "CCW / Laser Test", kSpindle, ""},
        {"STOP_LASER_OFF", "Stop / Laser Off", kSpindle, ""},
        {"OPEN_PROBE", "Display probe popup", kProbing, ""},
        {"PROBE_ROUTINE_SCROLL_RIGHT", "Probe Routine scroll right", kProbing, ""},
        {"PROBE_ROUTINE_SCROLL_LEFT", "Probe Routine scroll left", kProbing, ""},
        {"DISPLAY_MACHINE_INFO", "Display Machine Info", kToolbar, ""},
    };
    return actions;
}

const ShortcutAction* findShortcutAction(const QString& id) {
    for (const ShortcutAction& action : shortcutActions()) {
        if (action.id == id) {
            return &action;
        }
    }
    return nullptr;
}

QKeyCombination shortcutKey(const QKeyEvent& event) {
    Qt::KeyboardModifiers modifiers =
        event.modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const int key = event.key();
    const bool symbol = key > 0x20 && key < 0x7f && !(key >= 'A' && key <= 'Z') && !(key >= '0' && key <= '9');
    if (symbol) {
        modifiers &= ~Qt::ShiftModifier;
    }
    return QKeyCombination(modifiers, static_cast<Qt::Key>(key));
}

ShortcutManager::ShortcutManager(Machine& machine, QWidget& window, QObject* parent)
    : QObject(parent), machine_(machine), window_(window) {
    rebuild();
    connect(&machine_, &Machine::appSettingsChanged, this, &ShortcutManager::rebuild);
    qApp->installEventFilter(this);
}

ShortcutManager::~ShortcutManager() {
    release();
    if (qApp) {
        qApp->removeEventFilter(this);
    }
}

QKeySequence ShortcutManager::keys(const QString& id) const {
    const auto& user = machine_.settings().shortcuts;
    if (const auto it = user.find(id.toStdString()); it != user.end()) {
        return QKeySequence::fromString(QString::fromStdString(it->second.keys), QKeySequence::PortableText);
    }
    const ShortcutAction* action = findShortcutAction(id);
    return action ? QKeySequence::fromString(action->defaultKeys, QKeySequence::PortableText) : QKeySequence();
}

bool ShortcutManager::isActive(const QString& id) const {
    const auto& user = machine_.settings().shortcuts;
    const auto it = user.find(id.toStdString());
    return it == user.end() || it->second.active;
}

bool ShortcutManager::enabled() const {
    return machine_.settings().shortcutsEnabled;
}

void ShortcutManager::setHandler(const QString& id, std::function<void()> press, std::function<void()> release) {
    handlers_[id] = Handler{std::move(press), std::move(release)};
}

void ShortcutManager::rebuild() {
    bindings_.clear();
    for (const ShortcutAction& action : shortcutActions()) {
        const QKeySequence sequence = keys(action.id);
        if (!sequence.isEmpty() && isActive(action.id)) {
            bindings_.emplace(sequence[0].toCombined(), action.id);  // the first one keeps a key
        }
    }
}

QString ShortcutManager::actionFor(QKeyCombination key) const {
    const auto it = bindings_.find(key.toCombined());
    return it == bindings_.end() ? QString() : it->second;
}

bool ShortcutManager::trigger(const QString& id) {
    if (!enabled() && id != QLatin1String("TOGGLE_SHORTCUTS")) {
        return false;
    }
    const auto it = handlers_.find(id);
    if (it == handlers_.end() || !it->second.press) {
        return false;
    }
    const ShortcutAction* action = findShortcutAction(id);
    if (action && action->hold) {
        release();
        held_ = id;
    }
    it->second.press();
    return true;
}

void ShortcutManager::release() {
    if (held_.isEmpty()) {
        return;
    }
    const auto it = handlers_.find(held_);
    held_.clear();
    heldKey_ = 0;
    if (it != handlers_.end() && it->second.release) {
        it->second.release();
    }
}

bool ShortcutManager::typingInto(QObject*) const {
    // Mousetrap's rule: keys typed into inputs, text areas and selects are
    // theirs.
    QWidget* focus = QApplication::focusWidget();
    if (!focus) {
        return false;
    }
    if (const auto* line = qobject_cast<QLineEdit*>(focus)) {
        return !line->isReadOnly();
    }
    if (const auto* text = qobject_cast<QPlainTextEdit*>(focus)) {
        return !text->isReadOnly();
    }
    if (const auto* text = qobject_cast<QTextEdit*>(focus)) {
        return !text->isReadOnly();
    }
    if (const auto* combo = qobject_cast<QComboBox*>(focus)) {
        return combo->isEditable();
    }
    return qobject_cast<QAbstractSpinBox*>(focus) || qobject_cast<QKeySequenceEdit*>(focus);
}

bool ShortcutManager::eventFilter(QObject* watched, QEvent* event) {
    const QEvent::Type type = event->type();
    if (type == QEvent::ApplicationDeactivate || (type == QEvent::WindowDeactivate && watched == &window_)) {
        release();  // never leave a jog running behind another window
        return false;
    }
    if ((type != QEvent::KeyPress && type != QEvent::KeyRelease) || !watched->isWidgetType() ||
        static_cast<QWidget*>(watched)->window() != &window_ || QApplication::activeWindow() != &window_) {
        return false;
    }
    const auto* key = static_cast<QKeyEvent*>(event);
    if (type == QEvent::KeyRelease) {
        if (!key->isAutoRepeat() && !held_.isEmpty() && key->key() == heldKey_) {
            release();
            return true;
        }
        return false;
    }
    if (!held_.isEmpty() && key->key() == heldKey_) {
        return true;  // auto-repeat while held
    }
    if (typingInto(watched)) {
        return false;
    }
    const QString id = actionFor(shortcutKey(*key));
    if (id.isEmpty() || (!enabled() && id != QLatin1String("TOGGLE_SHORTCUTS"))) {
        return false;
    }
    if (key->isAutoRepeat()) {
        return true;  // one run per press
    }
    if (!trigger(id)) {
        return false;
    }
    if (held_ == id) {
        heldKey_ = key->key();
    }
    return true;
}

}  // namespace gs::app
