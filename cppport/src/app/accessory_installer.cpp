#include "accessory_installer.hpp"

#include "jogger.hpp"
#include "machine.hpp"
#include "panels.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace gs::app {

void WizardPage::setComplete(bool complete) {
    if (complete_ != complete) {
        complete_ = complete;
        Q_EMIT completionChanged(complete);
    }
}

namespace {

enum Screen { kHub, kLanding, kRun };

QLabel* heading(const QString& text, int points) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet(QString("font-size: %1pt; font-weight: 700;").arg(points));
    return label;
}

QString linkText(const QString& lead, const QString& url) {
    return QString("%1 <a href=\"%2\">online resources</a>").arg(lead.toHtmlEscaped(), url.toHtmlEscaped());
}

// A picture scaled to `width`, from the app's resources.
QLabel* picture(const QString& resource, int width) {
    auto* label = new QLabel;
    label->setAlignment(Qt::AlignCenter);
    const QPixmap pixmap(resource.isEmpty() ? QStringLiteral(":/accessories/placeholder.png") : resource);
    if (!pixmap.isNull()) {
        label->setPixmap(pixmap.scaledToWidth(std::min(width, pixmap.width()), Qt::SmoothTransformation));
    }
    return label;
}

void clearLayout(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            delete widget;
        } else if (QLayout* child = item->layout()) {
            clearLayout(child);
            delete child;
        }
        delete item;
    }
}

}  // namespace

AccessoryInstallerDialog::AccessoryInstallerDialog(Machine& machine, Jogger& jogger,
                                                   std::vector<AccessoryWizard> wizards, QWidget* parent)
    : QDialog(parent), machine_(machine), jogger_(jogger), wizards_(std::move(wizards)) {
    setWindowTitle(tr("Accessory Installation"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    screens_ = new QStackedWidget;
    layout->addWidget(screens_);

    // ---- the hub (WizardsHub) ----
    auto* hub = new QWidget;
    auto* hubLayout = new QVBoxLayout(hub);
    hubLayout->setContentsMargins(32, 24, 32, 24);
    hubLayout->addWidget(heading(tr("Accessory Installation"), 22));
    auto* about = new QLabel(tr("Select a wizard to configure and install your CNC accessories. Each wizard will "
                                "guide you through the setup process step by step."));
    about->setWordWrap(true);
    hubLayout->addWidget(about);
    auto* cards = new QGridLayout;
    cards->setSpacing(16);
    for (std::size_t i = 0; i < wizards_.size(); ++i) {
        const AccessoryWizard& wizard = wizards_[i];
        auto* card = new QFrame;
        card->setObjectName("wizardCard");
        card->setStyleSheet("QFrame#wizardCard { border: 2px solid palette(midlight); border-radius: 10px; }");
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->addWidget(heading(wizard.title, 15));
        std::size_t steps = 0;
        for (const SubWizard& sub : wizard.subWizards) {
            steps += sub.steps.size();
        }
        const std::size_t configurations = wizard.subWizards.size();
        QString facts = tr("%1 configuration%2").arg(configurations).arg(configurations != 1 ? "s" : "") + "\n" +
                        tr("%1 total steps").arg(steps);
        if (!wizard.subWizards.empty() && !wizard.subWizards.front().estimatedTime.isEmpty()) {
            facts += "\n" + wizard.subWizards.front().estimatedTime;
        }
        auto* factsLabel = new QLabel(facts);
        factsLabel->setStyleSheet("color: palette(dark);");
        cardLayout->addWidget(factsLabel);
        cardLayout->addStretch(1);
        auto* start = new QPushButton(tr("Start Wizard →"));
        start->setObjectName("wizard-" + wizard.id);
        connect(start, &QPushButton::clicked, this, [this, id = wizard.id] { openWizard(id); });
        cardLayout->addWidget(start, 0, Qt::AlignLeft);
        cards->addWidget(card, static_cast<int>(i / 3), static_cast<int>(i % 3));
    }
    hubLayout->addLayout(cards);
    hubLayout->addStretch(1);
    screens_->addWidget(hub);

    // ---- a wizard's landing page (WizardLanding) ----
    auto* landing = new QWidget;
    auto* landingLayout = new QHBoxLayout(landing);
    landingLayout->setContentsMargins(0, 0, 0, 0);
    auto* landingLeft = new QWidget;
    auto* leftLayout = new QVBoxLayout(landingLeft);
    leftLayout->setContentsMargins(32, 24, 32, 24);
    auto* back = new QPushButton(tr("← Back to Wizards"));
    back->setObjectName("wizard-back");
    back->setFlat(true);
    connect(back, &QPushButton::clicked, this, &AccessoryInstallerDialog::backToHub);
    leftLayout->addWidget(back, 0, Qt::AlignLeft);
    landingTitle_ = heading(QString(), 24);
    leftLayout->addWidget(landingTitle_);
    landingInfo_ = new QLabel;
    landingInfo_->setWordWrap(true);
    leftLayout->addWidget(landingInfo_);
    landingChecks_ = new QLabel;
    landingChecks_->setWordWrap(true);
    landingChecks_->setStyleSheet("QLabel { background: #fef2f2; color: #991b1b; border: 1px solid #fca5a5;"
                                  " border-radius: 8px; padding: 10px; }");
    leftLayout->addWidget(landingChecks_);
    subWizardButtons_ = new QVBoxLayout;
    leftLayout->addLayout(subWizardButtons_);
    leftLayout->addStretch(1);
    landingLayout->addWidget(landingLeft, 3);
    auto* landingRight = new QFrame;
    landingRight->setObjectName("landingSide");
    landingRight->setStyleSheet("QFrame#landingSide { background: palette(midlight); }");
    auto* rightLayout = new QVBoxLayout(landingRight);
    rightLayout->setContentsMargins(24, 24, 24, 24);
    landingImage_ = new QLabel;
    landingImage_->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(landingImage_, 1);
    landingHelp_ = new QLabel;
    landingHelp_->setOpenExternalLinks(true);
    landingHelp_->setWordWrap(true);
    landingHelp_->setStyleSheet("QLabel { background: palette(base); border: 2px solid #60a5fa; border-radius: 8px;"
                                " padding: 12px; }");
    rightLayout->addWidget(landingHelp_);
    landingLayout->addWidget(landingRight, 2);
    screens_->addWidget(landing);

    // ---- a configuration's steps (WizardContainer) ----
    auto* run = new QWidget;
    auto* runLayout = new QVBoxLayout(run);
    runLayout->setContentsMargins(0, 0, 0, 0);
    runLayout->setSpacing(0);
    auto* top = new QFrame;
    top->setObjectName("wizardTop");
    top->setStyleSheet("QFrame#wizardTop { border-bottom: 1px solid palette(mid); }");
    auto* topLayout = new QHBoxLayout(top);
    progressText_ = new QLabel;
    topLayout->addWidget(progressText_);
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setFormat(tr("%p% Complete"));
    topLayout->addWidget(progress_, 1);
    auto* exit = new QPushButton(tr("← Exit"));
    exit->setObjectName("wizard-exit");
    exit->setFlat(true);
    connect(exit, &QPushButton::clicked, this, &AccessoryInstallerDialog::exitSubWizard);
    topLayout->addWidget(exit);
    runLayout->addWidget(top);
    auto* body = new QHBoxLayout;
    body->setSpacing(0);
    auto* pageScroll = new QScrollArea;
    pageScroll->setWidgetResizable(true);
    pageScroll->setFrameShape(QFrame::NoFrame);
    pageHolder_ = new QWidget;
    pageLayout_ = new QVBoxLayout(pageHolder_);
    pageLayout_->setContentsMargins(32, 24, 32, 24);
    stepHeading_ = heading(QString(), 20);
    pageLayout_->addWidget(stepHeading_);
    pageScroll->setWidget(pageHolder_);
    body->addWidget(pageScroll, 3);
    auto* sideScroll = new QScrollArea;
    sidePanel_ = sideScroll;
    sideScroll->setWidgetResizable(true);
    sideScroll->setFrameShape(QFrame::NoFrame);
    side_ = new QWidget;
    side_->setObjectName("wizardSide");
    side_->setStyleSheet("QWidget#wizardSide { background: palette(midlight); }");
    sideLayout_ = new QVBoxLayout(side_);
    sideLayout_->setContentsMargins(20, 16, 20, 16);
    sideScroll->setWidget(side_);
    body->addWidget(sideScroll, 2);
    runLayout->addLayout(body, 1);
    navigation_ = new QFrame;
    navigation_->setObjectName("wizardNavigation");
    navigation_->setStyleSheet("QFrame#wizardNavigation { border-top: 1px solid palette(mid); }");
    auto* navLayout = new QHBoxLayout(navigation_);
    previous_ = new QPushButton(tr("‹ Previous"));
    previous_->setObjectName("wizard-previous");
    next_ = new QPushButton(tr("Next ›"));
    next_->setObjectName("wizard-next");
    exitWizard_ = new QPushButton(tr("Exit Wizard"));
    restart_ = new QPushButton(tr("Restart Wizard"));
    connect(previous_, &QPushButton::clicked, this, &AccessoryInstallerDialog::previous);
    connect(next_, &QPushButton::clicked, this, &AccessoryInstallerDialog::next);
    connect(exitWizard_, &QPushButton::clicked, this, &AccessoryInstallerDialog::exitSubWizard);
    connect(restart_, &QPushButton::clicked, this, &AccessoryInstallerDialog::restart);
    navLayout->addWidget(previous_);
    navLayout->addWidget(exitWizard_);
    navLayout->addStretch(1);
    navLayout->addWidget(restart_);
    navLayout->addWidget(next_);
    runLayout->addWidget(navigation_);
    screens_->addWidget(run);

    // The checks follow the machine.
    for (auto signal : {&Machine::connectionChanged, &Machine::stateChanged, &Machine::settingsChanged}) {
        connect(&machine_, signal, this, [this] {
            if (screens_->currentIndex() == kLanding) {
                refreshLanding();
            }
        });
    }
    showHub();
    resize(1180, 760);
}

QStringList AccessoryInstallerDialog::wizardTitles() const {
    QStringList titles;
    for (const AccessoryWizard& wizard : wizards_) {
        titles << wizard.title;
    }
    return titles;
}

const AccessoryWizard* AccessoryInstallerDialog::findWizard(const QString& id) const {
    for (const AccessoryWizard& wizard : wizards_) {
        if (wizard.id == id) {
            return &wizard;
        }
    }
    return nullptr;
}

void AccessoryInstallerDialog::showHub() {
    wizard_ = nullptr;
    subWizard_ = nullptr;
    screens_->setCurrentIndex(kHub);
}

void AccessoryInstallerDialog::backToHub() {
    showHub();
}

bool AccessoryInstallerDialog::openWizard(const QString& id) {
    wizard_ = findWizard(id);
    if (!wizard_) {
        return false;
    }
    subWizard_ = nullptr;
    if (wizard_->subWizards.size() == 1 && failedChecks().isEmpty()) {
        return startSubWizard(wizard_->subWizards.front().id);
    }
    showLanding();
    return true;
}

QStringList AccessoryInstallerDialog::failedChecks() const {
    QStringList reasons;
    if (wizard_) {
        for (const auto& check : wizard_->validations) {
            const WizardCheck result = check();
            if (!result.ok) {
                reasons << (result.reason.isEmpty() ? tr("Validation failed") : result.reason);
            }
        }
    }
    return reasons;
}

void AccessoryInstallerDialog::showLanding() {
    const AccessoryWizard& wizard = *wizard_;
    landingTitle_->setText(wizard.title);
    // The first configuration's facts.
    QStringList info;
    if (!wizard.subWizards.empty()) {
        const SubWizard& first = wizard.subWizards.front();
        if (!first.estimatedTime.isEmpty()) {
            info << tr("<b>Estimated time:</b> %1").arg(first.estimatedTime.toHtmlEscaped());
        }
        if (!first.configVersion.isEmpty()) {
            info << tr("Configuration File Version: %1").arg(first.configVersion.toHtmlEscaped());
        }
        if (!first.description.isEmpty()) {
            info << "<br>" + first.description.toHtmlEscaped();
        }
    }
    landingInfo_->setText(info.join("<br>"));
    const QPixmap pixmap(wizard.image.isEmpty() ? QStringLiteral(":/accessories/placeholder.png") : wizard.image);
    landingImage_->setPixmap(pixmap.isNull() ? QPixmap()
                                             : pixmap.scaledToWidth(std::min(420, pixmap.width()),
                                                                    Qt::SmoothTransformation));
    landingHelp_->setText(tr("<b>Need Help?</b><br>") +
                          linkText(tr("Follow along in our"), wizard.helpUrl.isEmpty()
                                                                  ? QStringLiteral("https://resources.sienci.com/")
                                                                  : wizard.helpUrl));
    screens_->setCurrentIndex(kLanding);
    refreshLanding();
}

void AccessoryInstallerDialog::refreshLanding() {
    if (!wizard_) {
        return;
    }
    // ValidationBanner: the first failing reason.
    const QStringList failed = failedChecks();
    landingChecks_->setText(failed.isEmpty() ? QString() : failed.front());
    landingChecks_->setVisible(!failed.isEmpty());
    clearLayout(subWizardButtons_);
    bool first = true;
    for (const SubWizard& sub : wizard_->subWizards) {
        if (!first && subWizardButtons_->count() == 1) {
            auto* line = new QFrame;
            line->setFrameShape(QFrame::HLine);
            subWizardButtons_->addWidget(line);
        }
        auto* button = new QPushButton(sub.title + "  →");
        button->setObjectName("sub-wizard-" + sub.id);
        button->setMinimumHeight(44);
        button->setEnabled(failed.isEmpty());
        if (first) {
            button->setStyleSheet("QPushButton:enabled { background: #111827; color: white; font-weight: 600; }");
        }
        connect(button, &QPushButton::clicked, this, [this, id = sub.id] { startSubWizard(id); });
        subWizardButtons_->addWidget(button);
        first = false;
    }
}

bool AccessoryInstallerDialog::startSubWizard(const QString& id) {
    if (!wizard_ || !failedChecks().isEmpty()) {
        return false;
    }
    for (const SubWizard& sub : wizard_->subWizards) {
        if (sub.id == id) {
            subWizard_ = &sub;
            completed_.clear();
            completion_ = false;
            screens_->setCurrentIndex(kRun);
            enterStep(0);
            return true;
        }
    }
    return false;
}

void AccessoryInstallerDialog::enterStep(int index) {
    // autoComplete: a step with nothing to do counts as done and is passed.
    const int last = static_cast<int>(subWizard_->steps.size()) - 1;
    while (index <= last && subWizard_->steps[static_cast<std::size_t>(index)].skip &&
           subWizard_->steps[static_cast<std::size_t>(index)].skip()) {
        completed_.insert(index);
        if (index == last) {
            step_ = index;
            if (subWizard_->completion) {
                showCompletion();
                return;
            }
            break;
        }
        ++index;
    }
    step_ = std::min(index, last);
    completion_ = false;
    showStep();
}

void AccessoryInstallerDialog::showStep() {
    const WizardStep& step = subWizard_->steps[static_cast<std::size_t>(step_)];
    // The page: made afresh each time it shows, as a remounted component.
    while (pageLayout_->count() > 1) {
        QLayoutItem* item = pageLayout_->takeAt(1);
        delete item->widget();
        delete item;
    }
    page_ = nullptr;
    const bool single = subWizard_->steps.size() == 1;
    stepHeading_->setText(step.title);
    stepHeading_->setVisible(!single);
    if (!subWizard_->configVersion.isEmpty()) {
        auto* version = new QLabel(tr("Configuration File Version: %1").arg(subWizard_->configVersion));
        version->setStyleSheet("color: palette(dark);");
        pageLayout_->addWidget(version);
    }
    page_ = step.page ? step.page() : new WizardPage;
    pageLayout_->addWidget(page_);
    pageLayout_->addStretch(1);
    connect(page_, &WizardPage::completionChanged, this, [this](bool complete) {
        if (complete) {
            completed_.insert(step_);
        } else {
            completed_.erase(step_);
            completion_ = false;
        }
        refreshNavigation();
    });
    connect(page_, &WizardPage::finish, this, &QDialog::accept);
    if (page_->isComplete()) {
        completed_.insert(step_);
    }

    clearLayout(sideLayout_);
    for (const WizardSideItem& item : step.side) {
        if (QWidget* widget = sideItem(item)) {
            sideLayout_->addWidget(widget);
        }
    }
    sideLayout_->addStretch(1);
    sidePanel_->setVisible(true);
    refreshNavigation();
}

QWidget* AccessoryInstallerDialog::sideItem(const WizardSideItem& item) {
    auto* box = new QWidget;
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 8);
    if (!item.title.isEmpty() && item.kind != WizardSideItem::Kind::Link) {
        auto* title = new QLabel(item.title);
        title->setStyleSheet("font-weight: 600;");
        layout->addWidget(title);
    }
    switch (item.kind) {
        case WizardSideItem::Kind::Image: layout->addWidget(picture(item.image, 420)); break;
        case WizardSideItem::Kind::Jogging: layout->addWidget(new JogPanel(machine_, jogger_)); break;
        case WizardSideItem::Kind::Widget:
            if (item.widget) {
                layout->addWidget(item.widget());
            }
            break;
        case WizardSideItem::Kind::Link: {
            auto* link = new QLabel((item.title.isEmpty() ? QString() : "<b>" + item.title.toHtmlEscaped() + "</b> ") +
                                    linkText(item.text, item.url));
            link->setOpenExternalLinks(true);
            link->setWordWrap(true);
            link->setStyleSheet("QLabel { background: palette(base); border-radius: 8px; padding: 10px; }");
            layout->addWidget(link);
            break;
        }
    }
    return box;
}

void AccessoryInstallerDialog::showCompletion() {
    completion_ = true;
    while (pageLayout_->count() > 1) {
        QLayoutItem* item = pageLayout_->takeAt(1);
        delete item->widget();
        delete item;
    }
    page_ = nullptr;
    stepHeading_->setVisible(false);
    pageLayout_->addWidget(subWizard_->completion());
    pageLayout_->addStretch(1);
    clearLayout(sideLayout_);
    sidePanel_->setVisible(false);  // the closing page takes the width
    refreshNavigation();
}

void AccessoryInstallerDialog::refreshNavigation() {
    const int count = stepCount();
    const bool single = count == 1;
    progressText_->setText(completion_ ? tr("All Steps Complete")
                                       : single ? stepTitle() : tr("Step %1 of %2").arg(step_ + 1).arg(count));
    // ProgressBar: the steps behind, all at the end.
    progress_->setValue(completion_ ? 100 : static_cast<int>(std::lround(100.0 * step_ / std::max(count, 1))));
    progress_->setVisible(!single);
    navigation_->setVisible(!single || completion_);
    previous_->setVisible(!completion_);
    next_->setVisible(!completion_);
    exitWizard_->setVisible(completion_);
    restart_->setVisible(completion_);
    previous_->setEnabled(step_ > 0);
    next_->setEnabled(canGoNext());
}

QString AccessoryInstallerDialog::stepTitle() const {
    if (!subWizard_ || completion_ || screens_->currentIndex() != kRun) {
        return {};
    }
    return subWizard_->steps[static_cast<std::size_t>(step_)].title;
}

int AccessoryInstallerDialog::stepNumber() const {
    return step_ + 1;
}

int AccessoryInstallerDialog::stepCount() const {
    return subWizard_ ? static_cast<int>(subWizard_->steps.size()) : 0;
}

bool AccessoryInstallerDialog::canGoNext() const {
    // Next: a done step moves on; the last one opens the closing page.
    return subWizard_ && !completion_ && completed_.contains(step_) &&
           (step_ + 1 < stepCount() || subWizard_->completion);
}

bool AccessoryInstallerDialog::next() {
    if (!canGoNext()) {
        return false;
    }
    if (step_ + 1 < stepCount()) {
        enterStep(step_ + 1);
    } else {
        showCompletion();
    }
    return true;
}

bool AccessoryInstallerDialog::previous() {
    if (!subWizard_ || completion_ || step_ == 0) {
        return false;
    }
    int index = step_ - 1;
    while (index > 0 && subWizard_->steps[static_cast<std::size_t>(index)].skip &&
           subWizard_->steps[static_cast<std::size_t>(index)].skip()) {
        --index;
    }
    step_ = index;
    showStep();
    return true;
}

void AccessoryInstallerDialog::restart() {
    if (!subWizard_) {
        return;
    }
    completed_.clear();
    completion_ = false;
    enterStep(0);
}

void AccessoryInstallerDialog::exitSubWizard() {
    subWizard_ = nullptr;
    completed_.clear();
    completion_ = false;
    page_ = nullptr;
    if (wizard_) {
        showLanding();
    } else {
        showHub();
    }
}

}  // namespace gs::app
