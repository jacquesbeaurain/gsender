#pragma once

// "image://qr/<level>/<text, URI-encoded>": the text's QR code (gs::qr, the
// modules react-qr-code draws), black on white with a four-module quiet
// zone, scaled in whole pixels per module to the size asked for.

#include <QQuickImageProvider>

namespace gs::ui {

class QrProvider final : public QQuickImageProvider {
public:
    QrProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

}  // namespace gs::ui
