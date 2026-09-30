// The UiTest fixture's helpers and the UI tests' main().

#include "ui_test.hpp"

#include <QDeadlineTimer>
#include <QRegularExpression>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdio>

Q_IMPORT_QML_PLUGIN(GSenderPlugin)

namespace {

QtMessageHandler previousHandler = nullptr;

bool isQmlWarning(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (type == QtDebugMsg || type == QtInfoMsg) {
        return false;
    }
    const QLatin1StringView category(context.category ? context.category : "");
    if (category == QLatin1StringView("qml") || category == QLatin1StringView("js") ||
        category.startsWith(QLatin1StringView("qt.qml"))) {
        return true;
    }
    static const QRegularExpression qmlLocation(QStringLiteral("\\.qml:\\d+"));
    return qmlLocation.match(message).hasMatch() ||
           (context.file && QLatin1StringView(context.file).endsWith(QLatin1StringView(".qml")));
}

void recordQmlWarnings(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (isQmlWarning(type, context, message)) {
        qmlWarnings() << message;
    }
    if (previousHandler) {
        previousHandler(type, context, message);
    }
}

}  // namespace

QStringList& qmlWarnings() {
    static QStringList warnings;
    return warnings;
}

QGuiApplication& application() {
    static int argc = 1;
    static char name[] = "gs_ui_tests";
    static char* argv[] = {name, nullptr};
    static QGuiApplication* app = [] {
#ifdef _WIN32
        _setmaxstdio(2048);
#endif
        qputenv("QT_QPA_PLATFORM", "offscreen");
        if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR") && !qEnvironmentVariableIsEmpty("WINDIR")) {
            qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "\\Fonts");
        }
        ui::configureQuick(true);
        previousHandler = qInstallMessageHandler(recordQmlWarnings);
        return new QGuiApplication(argc, argv);
    }();
    return *app;
}

bool waitFor(const std::function<bool()>& done, int timeoutMs) {
    const QDeadlineTimer deadline(timeoutMs);
    while (!done()) {
        if (deadline.hasExpired()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

QQuickItem* findItem(QQuickItem* root, const QString& name) {
    if (!root) {
        return nullptr;
    }
    if (root->objectName() == name) {
        return root;
    }
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
