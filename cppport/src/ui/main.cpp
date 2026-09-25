// gSender (C++): the QML touch UI (being ported - see DEV_WALKTHROUGH.md,
// Step 61; the widget application, gsender, has everything meanwhile).
//
//   gsender-qml [--simulator | --simulator-hal] [--load <file>] [--config <file>]
//               [--dark] [--size WxH] [--screenshot <png> [--wait <ms>]]
//
// --screenshot renders the window after --wait milliseconds, saves it and
// exits; with -platform offscreen it needs no display.

#include "backend.hpp"
#include "machine.hpp"
#include "qt_event_loop.hpp"
#include "ui_app.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>
#include <string_view>

Q_IMPORT_QML_PLUGIN(GSenderPlugin)

int main(int argc, char** argv) {
    bool offscreen = qgetenv("QT_QPA_PLATFORM") == "offscreen";
    for (int i = 1; i + 1 < argc; ++i) {
        offscreen = offscreen || (std::string_view(argv[i]) == "-platform" && std::string_view(argv[i + 1]) == "offscreen");
    }
    // The offscreen platform has no font source of its own on Windows.
    if (offscreen && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR") && !qEnvironmentVariableIsEmpty("WINDIR")) {
        qputenv("QT_QPA_FONTDIR", (qgetenv("WINDIR") + "\\Fonts"));
    }
    gs::ui::configureQuick(offscreen);

    QApplication app(argc, argv);
    QApplication::setApplicationName("gSender (C++)");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setWindowIcon(QIcon(":/about/icon-square.png"));

    QCommandLineParser parser;
    parser.setApplicationDescription("CNC control for Grbl and grblHAL - touch UI");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption simulator("simulator", "Connect to the built-in simulated Grbl board.");
    const QCommandLineOption simulatorHal("simulator-hal", "Connect to the built-in simulated grblHAL board.");
    const QCommandLineOption load("load", "Load a G-code file.", "file");
    const QCommandLineOption config("config", "Configuration file (default ~/.gsender-cpp_rc).", "file");
    const QCommandLineOption dark("dark", "Dark mode (saved in the configuration).");
    const QCommandLineOption screenshot("screenshot", "Save a screenshot of the window and exit.", "png");
    const QCommandLineOption wait("wait", "Milliseconds before the screenshot (default 1500).", "ms", "1500");
    const QCommandLineOption size("size", "Window size, e.g. 1400x900.", "WxH", "1400x900");
    const QCommandLineOption view("view", "Toolpath view: 3d, top, front, right or left.", "view", "3d");
    parser.addOptions({simulator, simulatorHal, load, config, dark, screenshot, wait, size, view});
    parser.process(app);

    const QString configFile = parser.isSet(config)
                                   ? parser.value(config)
                                   : QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
                                         .filePath(".gsender-cpp_rc");
    gs::app::QtEventLoop loop;
    gs::app::Machine machine(loop, configFile.toStdWString());
    gs::ui::UiBackend backend(machine);
    gs::ui::UiBackend::setInstance(&backend);
    if (parser.isSet(dark)) {
        backend.setDarkMode(true);
    }

    QQmlApplicationEngine engine;
    QQuickWindow* window = gs::ui::loadMainWindow(engine);
    if (!window) {
        std::fputs("gsender-qml: the QML UI did not load\n", stderr);
        return 1;
    }
    const QStringList dimensions = parser.value(size).split('x');
    window->resize(dimensions.value(0).toInt() > 0 ? dimensions.value(0).toInt() : 1400,
                   dimensions.value(1).toInt() > 0 ? dimensions.value(1).toInt() : 900);
    if (QObject* toolpath = window->findChild<QObject*>("toolpath")) {
        QMetaObject::invokeMethod(toolpath, "setView", Q_ARG(QString, parser.value(view)));
    }

    if (parser.isSet(simulator) || parser.isSet(simulatorHal)) {
        backend.connectSimulator(parser.isSet(simulatorHal));
    }
    if (parser.isSet(load)) {
        machine.loadFile(parser.value(load));
    }
    if (parser.isSet(screenshot)) {
        const QString file = parser.value(screenshot);
        QTimer::singleShot(parser.value(wait).toInt(), window, [window, file] {
            window->grabWindow().save(file);
            QApplication::quit();
        });
    }
    return QApplication::exec();
}
