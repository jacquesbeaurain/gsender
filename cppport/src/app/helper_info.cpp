#include "helper_info.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace gs::app {

HelperInfo::HelperInfo(QWidget* parent) : QFrame(parent) {
    setObjectName("helperInfo");
    // bg-white, border-2 border-orange-600, rounded
    setStyleSheet("QFrame#helperInfo { background: palette(base); border: 2px solid #ea580c; border-radius: 6px; }");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 8, 12);
    auto* header = new QHBoxLayout;
    auto* mark = new QLabel(QString::fromUtf8("ⓘ"));
    mark->setStyleSheet("color: #ea580c; font-size: 16pt;");
    header->addWidget(mark);
    title_ = new QLabel;
    title_->setObjectName("helperTitle");
    title_->setStyleSheet("font-size: 13pt; font-weight: 700;");
    title_->setWordWrap(true);
    header->addWidget(title_, 1);
    auto* close = new QToolButton;
    close->setObjectName("helperClose");
    close->setText(QString::fromUtf8("✕"));
    close->setAutoRaise(true);
    close->setToolTip(tr("Close"));
    connect(close, &QToolButton::clicked, this, &QWidget::hide);
    header->addWidget(close, 0, Qt::AlignTop);
    layout->addLayout(header);
    description_ = new QLabel;
    description_->setObjectName("helperDescription");
    description_->setWordWrap(true);
    description_->setTextFormat(Qt::RichText);
    description_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(description_);
    more_ = new QLabel;
    more_->setOpenExternalLinks(true);
    layout->addWidget(more_);
    hide();
    parent->installEventFilter(this);
}

void HelperInfo::showInfo(const QString& title, const QString& description, const QString& resourceLink) {
    title_->setText(title);
    description_->setText(description);
    link_ = resourceLink;
    more_->setText(resourceLink.isEmpty()
                       ? QString()
                       : QString("<a href='%1'>%2</a>").arg(resourceLink.toHtmlEscaped(), tr("Learn more")));
    more_->setVisible(!resourceLink.isEmpty());
    place();
    show();
    raise();
}

QString HelperInfo::title() const {
    return title_->text();
}

QString HelperInfo::description() const {
    QTextDocument document;
    document.setHtml(description_->text());
    return document.toPlainText();
}

bool HelperInfo::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible()) {
        place();
    }
    return QFrame::eventFilter(watched, event);
}

void HelperInfo::place() {
    // absolute bottom-2/3 left-16 w-1/3: a third of the width, its bottom a
    // third of the way down (kept on screen when taller).
    const QWidget* area = parentWidget();
    const int width = std::max(280, area->width() / 3);
    setFixedWidth(width);
    adjustSize();
    const int height = sizeHint().height();
    const int top = std::max(8, area->height() / 3 - height);
    setGeometry(64, top, width, height);
}

}  // namespace gs::app
