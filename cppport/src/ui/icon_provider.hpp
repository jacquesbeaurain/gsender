#pragma once

// "image://icon/<name>/<rrggbb>": the SVG icons of resources/icons (extracted
// from upstream's react-icons by tools/extract_icons.mjs) drawn in a colour,
// their `currentColor` replaced - recolouring in QML would need shader
// effects, which the software renderer (the headless tests) lacks.

#include <QQuickImageProvider>

namespace gs::ui {

class IconProvider final : public QQuickImageProvider {
public:
    IconProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};

}  // namespace gs::ui
