#include "qr_provider.hpp"

#include "gs/util/qrcode.hpp"

#include <QUrl>

#include <algorithm>

namespace gs::ui {

QrProvider::QrProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage QrProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) {
    const qsizetype slash = id.indexOf('/');
    const QString level = slash > 0 ? id.left(slash) : QStringLiteral("L");
    const QString text = QUrl::fromPercentEncoding(id.mid(slash + 1).toUtf8());
    const qr::Level l = level == "H" ? qr::Level::H : level == "Q" ? qr::Level::Q : level == "M" ? qr::Level::M : qr::Level::L;
    const std::optional<qr::Code> code = qr::encode(text.toStdString(), l);
    if (!code) {
        return {};
    }
    constexpr int kQuiet = 4;
    const int modules = code->size + 2 * kQuiet;
    const int wanted = std::max(requestedSize.width(), requestedSize.height());
    const int scale = std::max(1, wanted > 0 ? wanted / modules : 4);
    QImage image(modules * scale, modules * scale, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int r = 0; r < code->size; ++r) {
        for (int c = 0; c < code->size; ++c) {
            if (!code->isDark(r, c)) {
                continue;
            }
            for (int y = 0; y < scale; ++y) {
                auto* line = reinterpret_cast<QRgb*>(image.scanLine((r + kQuiet) * scale + y));
                std::fill_n(line + (c + kQuiet) * scale, scale, qRgb(0, 0, 0));
            }
        }
    }
    if (size) {
        *size = image.size();
    }
    return image;
}

}  // namespace gs::ui
