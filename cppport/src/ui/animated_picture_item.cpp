#include "animated_picture_item.hpp"

#include <QPainter>
#include <QQuickWindow>
#include <QUrl>

#include <cmath>

namespace gs::ui {

AnimatedPictureItem::AnimatedPictureItem(QQuickItem* parent) : QQuickPaintedItem(parent) {}

AnimatedPictureItem::~AnimatedPictureItem() = default;

void AnimatedPictureItem::setSource(const QString& source) {
    if (source == source_) {
        return;
    }
    source_ = source;
    scaledFrame_ = -1;
    movie_.reset();
    if (!source.isEmpty()) {
        const QUrl url(source);
        const QString path = url.scheme() == QLatin1String("qrc") ? ":" + url.path()
                             : url.isLocalFile()                  ? url.toLocalFile()
                                                                  : source;
        movie_ = std::make_unique<QMovie>(path);
        movie_->setCacheMode(QMovie::CacheAll);
        connect(movie_.get(), &QMovie::frameChanged, this, [this] { update(); });
        movie_->jumpToFrame(0);
        const QSize size = movie_->currentImage().size();
        setImplicitSize(size.width(), size.height());
        updatePlaying();
    } else {
        setImplicitSize(0, 0);
    }
    update();
    Q_EMIT sourceChanged();
}

void AnimatedPictureItem::setPlaying(bool playing) {
    if (playing == playing_) {
        return;
    }
    playing_ = playing;
    updatePlaying();
    Q_EMIT playingChanged();
}

void AnimatedPictureItem::setInverted(bool inverted) {
    if (inverted == inverted_) {
        return;
    }
    inverted_ = inverted;
    update();
    Q_EMIT invertedChanged();
}

void AnimatedPictureItem::updatePlaying() {
    if (!movie_) {
        return;
    }
    if (playing_ && movie_->frameCount() != 1) {
        if (movie_->state() == QMovie::Paused) {
            movie_->setPaused(false);
        } else if (movie_->state() == QMovie::NotRunning) {
            movie_->start();
        }
    } else if (movie_->state() == QMovie::Running) {
        movie_->setPaused(true);
    }
}

void AnimatedPictureItem::paint(QPainter* painter) {
    if (!movie_ || width() <= 0 || height() <= 0) {
        return;
    }
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSize device(static_cast<int>(std::lround(width() * dpr)), static_cast<int>(std::lround(height() * dpr)));
    if (scaledFrame_ != movie_->currentFrameNumber() || scaledSize_ != device || scaledInverted_ != inverted_) {
        QImage frame = movie_->currentImage().convertToFormat(QImage::Format_ARGB32);
        if (inverted_) {
            frame.invertPixels(QImage::InvertRgb);  // CSS invert(1): the colours, not the alpha
        }
        scaled_ = frame.scaled(device, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        scaledFrame_ = movie_->currentFrameNumber();
        scaledSize_ = device;
        scaledInverted_ = inverted_;
    }
    painter->drawImage(QRectF(0, 0, width(), height()), scaled_);
}

}  // namespace gs::ui
