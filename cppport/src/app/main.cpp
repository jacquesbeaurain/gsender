// gSender (C++): the Qt Widgets application.
//
//   gsender [--simulator] [--load <file>] [--start] [--config <file>]
//   gsender --screenshot <png> [--wait <ms>] [--simulator] [--load <file>]
//
// --screenshot renders the window after --wait milliseconds, saves it and
// exits; with -platform offscreen (or QT_QPA_PLATFORM=offscreen) it needs no
// display, which is how the UI is checked in automation.

#include "machine.hpp"
#include "main_window.hpp"
#include "qt_event_loop.hpp"
#include "toolpath_view.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>

#include <string_view>

int main(int argc, char** argv) {
    // The offscreen platform has no font source of its own on Windows.
    bool offscreen = qgetenv("QT_QPA_PLATFORM") == "offscreen";
    for (int i = 1; i + 1 < argc; ++i) {
        offscreen = offscreen || (std::string_view(argv[i]) == "-platform" && std::string_view(argv[i + 1]) == "offscreen");
    }
    if (offscreen && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR") && !qEnvironmentVariableIsEmpty("WINDIR")) {
        qputenv("QT_QPA_FONTDIR", (qgetenv("WINDIR") + "\\Fonts"));
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName("gSender (C++)");
    QApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("CNC control for Grbl and grblHAL");
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption simulator("simulator", "Connect to the built-in simulated Grbl board.");
    const QCommandLineOption load("load", "Load a G-code file.", "file");
    const QCommandLineOption config("config", "Configuration file (default ~/.gsender-cpp_rc).", "file");
    const QCommandLineOption screenshot("screenshot", "Save a screenshot of the window and exit.", "png");
    const QCommandLineOption wait("wait", "Milliseconds before the screenshot (default 1500).", "ms", "1500");
    const QCommandLineOption size("size", "Window size, e.g. 1400x900.", "WxH", "1400x900");
    const QCommandLineOption startJob("start", "Start the loaded job once the machine is ready.");
    const QCommandLineOption view("view", "Toolpath view: top or 3d.", "view", "top");
    parser.addOptions({simulator, load, config, screenshot, wait, size, startJob, view});
    parser.process(app);

    // Own configuration file for now (see DEV_WALKTHROUGH.md, Step 13).
    const QString configFile = parser.isSet(config)
                                   ? parser.value(config)
                                   : QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
                                         .filePath(".gsender-cpp_rc");

    gs::app::QtEventLoop loop;
    gs::app::Machine machine(loop, configFile.toStdWString());
    gs::app::MainWindow window(machine);
    const QStringList dimensions = parser.value(size).split('x');
    window.resize(dimensions.value(0).toInt() > 0 ? dimensions.value(0).toInt() : 1400,
                  dimensions.value(1).toInt() > 0 ? dimensions.value(1).toInt() : 900);
    window.setDialogsEnabled(!parser.isSet(screenshot));
    window.show();
    if (parser.value(view) == "3d") {
        window.toolpathView().set3dView();
    }

    if (parser.isSet(simulator)) {
        machine.connectTo(gs::app::Machine::kSimulatorPort);
    }
    if (parser.isSet(load)) {
        machine.loadFile(parser.value(load));
    }
    if (parser.isSet(startJob)) {
        // Start once connected, initialized, idle and the file is analysed.
        auto* poll = new QTimer(&window);
        QObject::connect(poll, &QTimer::timeout, &window, [&machine, poll] {
            gs::controller::Controller* c = machine.controller();
            if (c && c->runner().hasSettings() && c->state().status.activeState == "Idle" && machine.hasProgram() &&
                !machine.isAnalyzing()) {
                poll->stop();
                c->start();
            }
        });
        poll->start(100);
    }
    if (parser.isSet(screenshot)) {
        const QString file = parser.value(screenshot);
        QTimer::singleShot(parser.value(wait).toInt(), &window, [&window, file] {
            window.grab().save(file);
            QApplication::quit();
        });
    }
    return QApplication::exec();
}
