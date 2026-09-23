#include "appearance.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

#include <optional>

namespace gs::app {
namespace {

struct Original {
    QString style;
    QPalette palette;
};

std::optional<Original>& original() {
    static std::optional<Original> saved;
    return saved;
}

bool& applied() {
    static bool dark = false;
    return dark;
}

QPalette darkPalette() {
    const QColor window(0x2b, 0x2d, 0x30);
    const QColor base(0x1e, 0x1f, 0x22);
    const QColor button(0x3c, 0x3f, 0x41);
    const QColor text(0xe6, 0xe6, 0xe6);
    const QColor disabled(0x7a, 0x7a, 0x7a);
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, button);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x8a));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xff, 0x6b, 0x6b));
    p.setColor(QPalette::Link, QColor(0x4e, 0xa1, 0xff));
    p.setColor(QPalette::Highlight, QColor(0x2f, 0x65, 0xca));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Light, QColor(0x4a, 0x4d, 0x50));
    p.setColor(QPalette::Midlight, QColor(0x42, 0x45, 0x48));
    p.setColor(QPalette::Mid, QColor(0x6b, 0x70, 0x76));
    p.setColor(QPalette::Dark, QColor(0x1a, 0x1b, 0x1d));
    p.setColor(QPalette::Shadow, Qt::black);
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, disabled);
    }
    p.setColor(QPalette::Disabled, QPalette::Button, window);
    return p;
}

}  // namespace

void applyDarkMode(bool dark) {
    if (!original()) {
        original() = Original{QApplication::style()->name(), QApplication::palette()};
    }
    if (dark == applied()) {
        return;
    }
    applied() = dark;
    // The palette first: setting the style re-polishes every widget, and
    // those with style sheets keep the palette they were polished with.
    if (dark) {
        QApplication::setPalette(darkPalette());
        QApplication::setStyle(QStyleFactory::create("Fusion"));
    } else {
        QApplication::setPalette(original()->palette);
        QApplication::setStyle(QStyleFactory::create(original()->style));
    }
}

bool darkModeApplied() {
    return applied();
}

}  // namespace gs::app
