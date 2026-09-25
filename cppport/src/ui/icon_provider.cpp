#include "icon_provider.hpp"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

namespace gs::ui {

IconProvider::IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage IconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const QString name = id.section('/', 0, 0);
    const QString color = id.section('/', 1, 1);
    QFile file(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray svg = file.readAll();
    if (!color.isEmpty()) {
        svg.replace("currentColor", "#" + color.toLatin1());
    }
    QSvgRenderer renderer(svg);
    const QSize natural = renderer.defaultSize().isValid() ? renderer.defaultSize() : QSize(24, 24);
    const QSize target = requestedSize.isValid() && !requestedSize.isEmpty()
                             ? natural.scaled(requestedSize, Qt::KeepAspectRatio)
                             : natural;
    QImage image(target, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    if (size) {
        *size = target;
    }
    return image;
}

}  // namespace gs::ui
