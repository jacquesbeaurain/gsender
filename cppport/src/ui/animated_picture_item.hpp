#pragma once

// An animated GIF drawn as a browser draws an <img>: each frame scaled down
// with area averaging (Qt Quick's own scaling is bilinear, which aliases thin
// lines at small sizes) to the item's size in device pixels, and inverted
// when `inverted` is set (upstream's `dark:invert` on the probe pictures).
// QPainter only, so it renders on the software renderer too.

#include <QImage>
#include <QMovie>
#include <QQuickPaintedItem>
#include <QSize>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace gs::ui {

class AnimatedPictureItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT

    // A qrc: URL or a file path.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool inverted READ inverted WRITE setInverted NOTIFY invertedChanged)

public:
    explicit AnimatedPictureItem(QQuickItem* parent = nullptr);
    ~AnimatedPictureItem() override;

    QString source() const { return source_; }
    void setSource(const QString& source);
    bool playing() const noexcept { return playing_; }
    void setPlaying(bool playing);
    bool inverted() const noexcept { return inverted_; }
    void setInverted(bool inverted);

    void paint(QPainter* painter) override;

Q_SIGNALS:
    void sourceChanged();
    void playingChanged();
    void invertedChanged();

private:
    void updatePlaying();

    QString source_;
    bool playing_ = true;
    bool inverted_ = false;
    std::unique_ptr<QMovie> movie_;
    // The last frame drawn, as scaled (and inverted) for this size.
    QImage scaled_;
    int scaledFrame_ = -1;
    QSize scaledSize_;
    bool scaledInverted_ = false;
};

}  // namespace gs::ui
