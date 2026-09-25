#include "ui_app.hpp"

#include "icon_provider.hpp"

#include <QDir>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>

namespace gs::ui {

void configureQuick(bool offscreen) {
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    if (offscreen) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }
}

QQuickWindow* loadMainWindow(QQmlApplicationEngine& engine) {
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
#ifdef GS_QT_QML_DIR
    // Run from the build tree, Qt looks for its QML modules beside the copied
    // DLLs; point it at the Qt installation's (the LibPack's on Windows).
    if (QDir(QStringLiteral(GS_QT_QML_DIR)).exists()) {
        engine.addImportPath(QStringLiteral(GS_QT_QML_DIR));
    }
#endif
    engine.loadFromModule("GSender", "Main");
    const QList<QObject*> roots = engine.rootObjects();
    return roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.front());
}

}  // namespace gs::ui
