#include "stats_dialog.hpp"

#include "machine.hpp"
#include "pie_chart.hpp"

#include "gs/controller/controller.hpp"
#include "gs/controller/locations.hpp"
#include "gs/transport/asio_link.hpp"
#include "gs/util/datetime.hpp"
#include "gs/util/jsnumber.hpp"

#include <QApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace gs::app {
namespace {

// gSender's palette (tailwind.config.ts).
constexpr const char* kBlue = "#3F85C7";
constexpr const char* kRed = "#dc2626";
constexpr const char* kGreen = "#059669";
constexpr const char* kOrange = "#bb6a0c";
constexpr const char* kRobin = "#689AC9";
constexpr const char* kYellow = "#eab308";

// Secondary text, between the text and the background: legible in both
// modes (the palette's dark role is near black in dark mode).
QString mutedColor() {
    const QPalette palette = QApplication::palette();
    const QColor text = palette.color(QPalette::WindowText);
    const QColor back = palette.color(QPalette::Window);
    return QColor::fromRgbF(static_cast<float>(text.redF() * 0.6 + back.redF() * 0.4),
                            static_cast<float>(text.greenF() * 0.6 + back.greenF() * 0.4),
                            static_cast<float>(text.blueF() * 0.6 + back.blueF() * 0.4))
        .name();
}

QString mutedStyle() {
    return QString("color: %1;").arg(mutedColor());
}

QString qs(const std::string& text) {
    return QString::fromStdString(text);
}

QString number(double value) {
    return qs(js::numberToString(value));
}

// "rgba(r, g, b, a)" of a colour, for the tinted backgrounds.
QString tint(const char* color, double alpha) {
    const QColor c(color);
    return QString("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

// StatCard
QFrame* card() {
    auto* frame = new QFrame;
    frame->setObjectName("statCard");
    frame->setStyleSheet(
        "QFrame#statCard { background: palette(base); border: 1px solid palette(mid); border-radius: 6px; }");
    return frame;
}

QLabel* heading(const QString& text) {
    auto* label = new QLabel(text);
    label->setStyleSheet("font-size: 20pt; font-weight: 700;");
    return label;
}

// CardHeader with its StatLink.
QWidget* cardHeader(const QString& title, const QString& linkLabel = {}, std::function<void()> onLink = {}) {
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 4);
    auto* label = new QLabel(title);
    label->setStyleSheet(QString("color: %1; font-size: 15pt;").arg(kBlue));
    layout->addWidget(label);
    layout->addStretch(1);
    if (!linkLabel.isEmpty()) {
        auto* link = new QLabel(QString("<a href='#' style='color: %1'>%2 &rsaquo;</a>").arg(kBlue, linkLabel.toHtmlEscaped()));
        link->setObjectName("statLink");
        QObject::connect(link, &QLabel::linkActivated, row, [onLink] { onLink(); });
        layout->addWidget(link);
    }
    return row;
}

QLabel* muted(const QString& text) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet(mutedStyle());
    return label;
}

void clearLayout(QLayout* layout) {
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();  // gone from view now, deleted once back in the event loop
            widget->deleteLater();
        } else if (QLayout* inner = item->layout()) {
            clearLayout(inner);
        }
        delete item;
    }
}

// ConfigRow: the label, a dotted leader and the value ("-" while
// disconnected).
QWidget* configRow(const QString& label, const QString& valueHtml) {
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 3, 0, 3);
    layout->addWidget(new QLabel(label));
    auto* leader = new QFrame;
    leader->setStyleSheet("QFrame { border: none; border-bottom: 2px dotted palette(mid); }");
    leader->setFixedHeight(row->fontMetrics().height() / 2 + 2);
    layout->addWidget(leader, 1, Qt::AlignVCenter);
    auto* value = new QLabel(valueHtml);
    value->setTextFormat(Qt::RichText);
    layout->addWidget(value);
    return row;
}

QString plain(const QString& html) {
    QTextDocument document;
    document.setHtml(html);
    return document.toPlainText();
}

// A Date's toLocaleString('en-US'): "9/23/2026, 2:40:12 AM".
QString enUsDateTime(std::int64_t ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).toString("M/d/yyyy, h:mm:ss AP");
}

// A table cell sorting by a number rather than its text.
class SortItem final : public QTableWidgetItem {
public:
    SortItem(const QString& text, double key) : QTableWidgetItem(text) { setData(Qt::UserRole + 1, key); }
    bool operator<(const QTableWidgetItem& other) const override {
        return data(Qt::UserRole + 1).toDouble() < other.data(Qt::UserRole + 1).toDouble();
    }
};

QTableWidget* table(const QStringList& headers) {
    auto* t = new QTableWidget(0, static_cast<int>(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setWordWrap(true);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

// A cell shown as rich text; the plain text kept for searching and tests.
void setRichCell(QTableWidget* t, int row, int column, const QString& html) {
    auto* item = new QTableWidgetItem;
    item->setData(Qt::UserRole, plain(html));
    t->setItem(row, column, item);
    auto* label = new QLabel(html);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setContentsMargins(6, 6, 6, 6);
    t->setCellWidget(row, column, label);
}

void filterRows(QTableWidget* t, const QString& text) {
    const QString needle = text.toLower();
    for (int row = 0; row < t->rowCount(); ++row) {
        const QTableWidgetItem* first = t->item(row, 0);
        const QString haystack = first ? first->data(Qt::UserRole + 2).toString() : QString();
        t->setRowHidden(row, !needle.isEmpty() && !haystack.contains(needle));
    }
}

// MaintenancePreview's reminder: its word and colour.
std::pair<QString, const char*> reminder(const config::MaintenanceTask& task) {
    switch (config::maintenanceDue(task)) {
        case config::MaintenanceDue::Urgent: return {QObject::tr("Urgent!"), kRed};
        case config::MaintenanceDue::Due: return {QObject::tr("Due"), kOrange};
        case config::MaintenanceDue::Soon: return {QObject::tr("Soon"), kRobin};
        case config::MaintenanceDue::Low: break;
    }
    return {QObject::tr("Low"), kGreen};
}

QString readReleaseNotes() {
    QFile file(":/about/notes.json");
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

}  // namespace

// ---- Add New Task / Edit Task -----------------------------------------------------------------

MaintenanceTaskDialog::MaintenanceTaskDialog(const config::MaintenanceTask* task, StatsConfirmer confirmer,
                                             QWidget* parent)
    : QDialog(parent), editing_(task != nullptr), confirmer_(std::move(confirmer)) {
    if (task) {
        original_ = *task;
    } else {
        original_.rangeStart = 0;
        original_.rangeEnd = 1;
    }
    setWindowTitle(editing_ ? tr("Edit Task") : tr("Add New Task"));
    resize(560, 480);
    auto* layout = new QVBoxLayout(this);
    const auto note = [] {
        auto* label = new QLabel;
        label->setWordWrap(true);
        label->setStyleSheet(QString("color: %1; font-style: italic; font-size: 8pt;").arg(kRed));
        return label;
    };

    layout->addWidget(new QLabel(tr("Task Name")));
    name_ = new QLineEdit(qs(original_.name));
    name_->setObjectName("taskName");
    name_->setPlaceholderText(tr("New Task"));
    layout->addWidget(name_);
    nameNote_ = new QLabel;
    nameNote_->setWordWrap(true);
    layout->addWidget(nameNote_);

    auto* ranges = new QHBoxLayout;
    const auto rangeColumn = [&](const QString& title, double value, const QString& placeholder, QLineEdit*& edit,
                                 QLabel*& error) {
        auto* column = new QVBoxLayout;
        column->addWidget(new QLabel(title));
        edit = new QLineEdit(number(value));
        edit->setPlaceholderText(placeholder);
        column->addWidget(edit);
        error = note();
        error->hide();
        column->addWidget(error);
        ranges->addLayout(column);
    };
    rangeColumn(tr("Task Start Range (Hrs)"), original_.rangeStart, "1", start_, startError_);
    rangeColumn(tr("Task End Range (Hrs)"), original_.rangeEnd, "20", end_, endError_);
    start_->setObjectName("taskRangeStart");
    end_->setObjectName("taskRangeEnd");
    layout->addLayout(ranges);

    layout->addWidget(new QLabel(tr("Task Description")));
    description_ = new QPlainTextEdit(qs(original_.description));
    description_->setObjectName("taskDescription");
    description_->setPlaceholderText(tr("What do I want to do"));
    layout->addWidget(description_, 1);

    auto* buttons = new QHBoxLayout;
    if (editing_) {
        auto* remove = new QPushButton(tr("Delete"));
        remove->setObjectName("taskDelete");
        remove->setStyleSheet(QString("QPushButton { background: %1; color: white; padding: 6px 18px; }").arg(kRed));
        connect(remove, &QPushButton::clicked, this, &MaintenanceTaskDialog::requestDelete);
        buttons->addWidget(remove);
    }
    buttons->addStretch(1);
    auto* cancel = new QPushButton(tr("Cancel"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    auto* save = new QPushButton(editing_ ? tr("Save") : tr("Add"));
    save->setObjectName("taskSubmit");
    save->setDefault(true);
    save->setStyleSheet(QString("QPushButton { background: %1; color: white; padding: 6px 18px; }").arg(kBlue));
    connect(save, &QPushButton::clicked, this, &MaintenanceTaskDialog::submit);
    buttons->addWidget(save);
    layout->addLayout(buttons);

    // The fields check themselves as they are left, and again as they change
    // while wrong.
    nameNote_->setText(tr("Keeping these unique makes it easier for you to remember what it is you need to do."));
    nameNote_->setStyleSheet(mutedStyle() + " font-style: italic; font-size: 8pt;");
    connect(name_, &QLineEdit::editingFinished, this, &MaintenanceTaskDialog::checkName);
    connect(name_, &QLineEdit::textChanged, this, [this] {
        if (nameShowsError_) {
            checkName();
        }
    });
    for (QLineEdit* edit : {start_, end_}) {
        connect(edit, &QLineEdit::editingFinished, this, &MaintenanceTaskDialog::checkRange);
        connect(edit, &QLineEdit::textChanged, this, [this] {
            if (!startError_->isHidden() || !endError_->isHidden()) {
                checkRange();
            }
        });
    }
}

bool MaintenanceTaskDialog::checkName() {
    const QString error = nameError();
    nameShowsError_ = !error.isEmpty();
    nameNote_->setText(nameShowsError_ ? error
                                       : tr("Keeping these unique makes it easier for you to remember what it is "
                                            "you need to do."));
    nameNote_->setStyleSheet(QString("color: %1; font-style: italic; font-size: 8pt;")
                                 .arg(nameShowsError_ ? QString(kRed) : mutedColor()));
    return !nameShowsError_;
}

bool MaintenanceTaskDialog::checkRange() {
    // Number(input.value): an empty field is 0.
    const double start = js::stringToNumber(start_->text().trimmed().toStdString());
    const double end = js::stringToNumber(end_->text().trimmed().toStdString());
    const QString range = qs(config::maintenanceRangeProblem(start, end));
    const bool startWrong = range.startsWith("Start");
    startError_->setText(startWrong ? range : QString());
    startError_->setHidden(!startWrong);
    endError_->setText(startWrong ? QString() : range);
    endError_->setHidden(startWrong || range.isEmpty());
    return range.isEmpty();
}

void MaintenanceTaskDialog::setName(const QString& name) {
    name_->setText(name);
}

void MaintenanceTaskDialog::setRange(const QString& start, const QString& end) {
    start_->setText(start);
    end_->setText(end);
}

void MaintenanceTaskDialog::setDescription(const QString& description) {
    description_->setPlainText(description);
}

QString MaintenanceTaskDialog::nameError() const {
    return qs(config::maintenanceNameProblem(name_->text().toStdString()));
}

QString MaintenanceTaskDialog::rangeStartError() const {
    return startError_->isHidden() ? QString() : startError_->text();
}

QString MaintenanceTaskDialog::rangeEndError() const {
    return endError_->isHidden() ? QString() : endError_->text();
}

bool MaintenanceTaskDialog::validate() {
    const bool name = checkName();
    const bool range = checkRange();
    return name && range;
}

config::MaintenanceTask MaintenanceTaskDialog::task() const {
    config::MaintenanceTask task = original_;
    task.name = name_->text().trimmed().toStdString();
    task.description = description_->toPlainText().toStdString();
    task.rangeStart = js::stringToNumber(start_->text().trimmed().toStdString());
    task.rangeEnd = js::stringToNumber(end_->text().trimmed().toStdString());
    if (!editing_) {
        task.currentTime = 0;
    }
    return task;
}

void MaintenanceTaskDialog::submit() {
    if (validate()) {
        accept();
    }
}

void MaintenanceTaskDialog::requestDelete() {
    const QString title = tr("Delete Task");
    const QString text = tr("Are you sure you want to delete %1?").arg(qs(original_.name));
    const bool yes = confirmer_ ? confirmer_(title, text)
                                : QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No) ==
                                      QMessageBox::Yes;
    if (yes) {
        done(kDeleted);
    }
}

// ---- Stats ------------------------------------------------------------------------------------

StatsDialog::StatsDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("Stats"));
    resize(1180, 780);
    auto* layout = new QVBoxLayout(this);
    pages_ = new QTabWidget;
    pages_->setObjectName("statMenu");
    pages_->setTabPosition(QTabWidget::South);  // StatMenu sits at the bottom
    pages_->addTab(overviewPage(), tr("Overview"));
    pages_->addTab(jobsPage(), tr("Jobs"));
    pages_->addTab(maintenancePage(), tr("Maintenance"));
    pages_->addTab(alarmsPage(), tr("Alarms"));
    pages_->addTab(aboutPage(), tr("About"));
    layout->addWidget(pages_, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Bursts (a connection's settings, a job's end) are gathered into one.
    auto* refresh = new QTimer(this);
    refresh->setSingleShot(true);
    refresh->setInterval(0);
    connect(refresh, &QTimer::timeout, this, &StatsDialog::reload);
    for (const auto signal : {&Machine::historyChanged, &Machine::connectionChanged, &Machine::settingsChanged,
                              &Machine::appSettingsChanged}) {
        connect(&machine_, signal, refresh, qOverload<>(&QTimer::start));
    }
    reload();
}

void StatsDialog::showPage(Page page) {
    pages_->setCurrentIndex(static_cast<int>(page));
}

StatsDialog::Page StatsDialog::page() const {
    return static_cast<Page>(pages_->currentIndex());
}

bool StatsDialog::confirm(const QString& title, const QString& text) {
    if (confirmer_) {
        return confirmer_(title, text);
    }
    return QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
}

QWidget* StatsDialog::diagnosticCard(bool compact) {
    QFrame* frame = card();
    auto* layout = new QVBoxLayout(frame);
    if (compact) {
        layout->addWidget(cardHeader(tr("Diagnostic File")));
    }
    layout->addWidget(muted(tr("Share this file with our customer support or community so others can help you "
                               "better. It contains your machine errors, profile, settings, and more.")));
    auto* download = new QPushButton(tr("Download Diagnostic File"));
    download->setObjectName("downloadDiagnostics");
    download->setMinimumHeight(40);
    connect(download, &QPushButton::clicked, this, &StatsDialog::diagnosticsRequested);
    layout->addWidget(download);
    return frame;
}

QWidget* StatsDialog::overviewPage() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    scroll->setWidget(content);
    auto* columns = new QHBoxLayout(content);
    columns->setSpacing(24);

    // Your Machine
    auto* main = new QVBoxLayout;
    main->addWidget(heading(tr("Your Machine")));
    QFrame* statsCard = card();
    auto* statsColumns = new QHBoxLayout(statsCard);
    auto* statsLeft = new QVBoxLayout;
    statsLeft->addWidget(cardHeader(tr("Stats")));
    results_ = new PieChart;
    results_->setObjectName("jobResultsChart");
    results_->setSeriesLabel(tr("Jobs"));
    results_->setMinimumSize(200, 210);
    results_->setMaximumSize(260, 230);
    resultsEmpty_ = new QLabel(tr("No data to display"));
    resultsEmpty_->setAlignment(Qt::AlignCenter);
    resultsEmpty_->setMinimumHeight(210);
    resultsEmpty_->setStyleSheet(mutedStyle());
    statsLeft->addWidget(results_, 0, Qt::AlignHCenter);
    statsLeft->addWidget(resultsEmpty_);
    statTable_ = new QVBoxLayout;
    statsLeft->addLayout(statTable_);
    statsLeft->addStretch(1);
    statsColumns->addLayout(statsLeft, 1);
    auto* statsRight = new QVBoxLayout;
    statsRight->addWidget(cardHeader(tr("Recent Jobs"), tr("More"), [this] { showPage(Page::Jobs); }));
    recentList_ = new QVBoxLayout;
    recentList_->setSpacing(10);
    statsRight->addLayout(recentList_);
    statsRight->addStretch(1);
    statsColumns->addLayout(statsRight, 1);
    main->addWidget(statsCard);

    auto* lower = new QHBoxLayout;
    lower->setSpacing(16);
    QFrame* maintenanceCard = card();
    auto* maintenanceLayout = new QVBoxLayout(maintenanceCard);
    maintenanceLayout->addWidget(
        cardHeader(tr("Upcoming Maintenance"), tr("Manage"), [this] { showPage(Page::Maintenance); }));
    upcomingList_ = new QVBoxLayout;
    maintenanceLayout->addLayout(upcomingList_);
    maintenanceLayout->addStretch(1);
    lower->addWidget(maintenanceCard, 1);
    QFrame* configurationCard = card();
    auto* configurationLayout = new QVBoxLayout(configurationCard);
    configurationLayout->addWidget(
        cardHeader(tr("Configuration"), tr("Change"), [this] { Q_EMIT configurationRequested(); }));
    profile_ = new QLabel;
    profile_->setObjectName("machineProfileName");
    configurationLayout->addWidget(profile_);
    configurationList_ = new QVBoxLayout;
    configurationList_->setSpacing(0);
    configurationLayout->addLayout(configurationList_);
    configurationLayout->addStretch(1);
    lower->addWidget(configurationCard, 1);
    main->addLayout(lower);
    main->addStretch(1);
    columns->addLayout(main, 2);

    // Get Help
    auto* side = new QVBoxLayout;
    side->addWidget(heading(tr("Get Help")));
    side->addWidget(diagnosticCard(false));
    const struct {
        const char* letter;
        const char* title;
        const char* link;
        const char* text;
    } links[] = {
        {"R", "Resources", "https://resources.sienci.com/view/gs-using-gsender/",
         "Learn about starting with gSender and how to use specific features"},
        {"C", "Community", "https://forum.sienci.com/c/gsender/14",
         "Have conversations with our friendly and helpful community"},
        {"G", "Github", "https://github.com/Sienci-Labs/gsender",
         "Submit issues or grab the latest version of gSender"},
    };
    for (const auto& link : links) {
        QFrame* frame = card();
        frame->setStyleSheet(QString("QFrame#statCard { background: palette(base); border: 2px solid palette(mid); "
                                     "border-top: 2px solid %1; border-radius: 4px; }")
                                 .arg(kBlue));
        auto* row = new QHBoxLayout(frame);
        auto* icon = new QLabel(link.letter);
        icon->setAlignment(Qt::AlignCenter);
        icon->setFixedSize(44, 44);
        icon->setStyleSheet(QString("background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 #a1c0dd); "
                                    "color: white; font-size: 18pt; font-weight: 700; border-radius: 4px;")
                                .arg(kBlue));
        row->addWidget(icon);
        const QString textColor = QApplication::palette().color(QPalette::WindowText).name();
        auto* text = new QLabel(QString("<a href='%1' style='text-decoration: none; color: %2'><b>%3</b></a>"
                                        "<br><span style='color: %4'>%5</span>")
                                    .arg(link.link, textColor, tr(link.title), mutedColor(), tr(link.text)));
        text->setObjectName(QString("externalLink%1").arg(link.title));
        text->setWordWrap(true);
        text->setOpenExternalLinks(true);
        row->addWidget(text, 1);
        auto* arrow = new QLabel(QString("<a href='%1' style='text-decoration: none; color: %2'>&#8599;</a>")
                                     .arg(link.link, kBlue));
        arrow->setOpenExternalLinks(true);
        arrow->setStyleSheet("font-size: 16pt;");
        row->addWidget(arrow);
        side->addWidget(frame);
    }
    QFrame* alarmsCard = card();
    auto* alarmsLayout = new QVBoxLayout(alarmsCard);
    alarmsLayout->addWidget(cardHeader(tr("Alarms & Errors"), tr("View all"), [this] { showPage(Page::Alarms); }));
    alarmList_ = new QVBoxLayout;
    alarmList_->setSpacing(6);
    alarmsLayout->addLayout(alarmList_);
    side->addWidget(alarmsCard);
    side->addStretch(1);
    columns->addLayout(side, 1);
    return scroll;
}

QWidget* StatsDialog::jobsPage() {
    auto* page = new QWidget;
    auto* columns = new QHBoxLayout(page);
    columns->setSpacing(24);
    QFrame* history = card();
    auto* layout = new QVBoxLayout(history);
    auto* top = new QHBoxLayout;
    top->addWidget(cardHeader(tr("Job History")), 1);
    auto* clear = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon), tr("Clear"));
    clear->setObjectName("clearJobHistory");
    connect(clear, &QPushButton::clicked, this, &StatsDialog::clearJobHistory);
    top->addWidget(clear);
    layout->addLayout(top);
    jobSearch_ = new QLineEdit;
    jobSearch_->setPlaceholderText(tr("Search past jobs..."));
    jobSearch_->setClearButtonEnabled(true);
    connect(jobSearch_, &QLineEdit::textChanged, this, [this](const QString& text) { filterRows(jobs_, text); });
    layout->addWidget(jobSearch_);
    jobs_ = table({tr("File Name"), tr("Duration"), tr("# Lines"), tr("Start Time"), tr("Status")});
    jobs_->setObjectName("jobHistory");
    jobs_->setAlternatingRowColors(true);
    jobs_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    jobs_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    jobs_->horizontalHeader()->setStretchLastSection(false);
    // Newest first, as upstream lists them.
    jobs_->horizontalHeader()->setSortIndicator(3, Qt::DescendingOrder);
    layout->addWidget(jobs_, 1);
    columns->addWidget(history, 2);

    auto* side = new QVBoxLayout;
    const auto chartCard = [&](const QString& title, PieChart*& chart, const char* name) {
        QFrame* frame = card();
        auto* inner = new QVBoxLayout(frame);
        inner->addWidget(cardHeader(title), 0, Qt::AlignHCenter);
        chart = new PieChart;
        chart->setObjectName(name);
        chart->setMinimumSize(220, 240);
        inner->addWidget(chart, 1);
        side->addWidget(frame, 1);
    };
    chartCard(tr("Jobs per CNC"), jobsPerCnc_, "jobsPerCnc");
    jobsPerCnc_->setSeriesLabel(tr("Jobs"));
    chartCard(tr("Run Time per CNC"), runTimePerCnc_, "runTimePerCnc");
    runTimePerCnc_->setDoughnut(true);
    // Upstream's tooltip says hours of what are milliseconds.
    runTimePerCnc_->setValueText(
        [](double ms) { return tr("%1 hours").arg(qs(js::toFixed(ms / 3'600'000, 2))); });
    columns->addLayout(side, 1);
    return page;
}

QWidget* StatsDialog::maintenancePage() {
    auto* page = new QWidget;
    auto* columns = new QHBoxLayout(page);
    columns->setSpacing(24);
    QFrame* list = card();
    auto* layout = new QVBoxLayout(list);
    layout->addWidget(cardHeader(tr("Maintenance")));
    auto* toolbar = new QHBoxLayout;
    taskSearch_ = new QLineEdit;
    taskSearch_->setPlaceholderText(tr("Search Tasks..."));
    taskSearch_->setClearButtonEnabled(true);
    connect(taskSearch_, &QLineEdit::textChanged, this, [this](const QString& text) { filterRows(tasks_, text); });
    toolbar->addWidget(taskSearch_, 1);
    auto* add = new QPushButton(tr("Add New Task"));
    add->setObjectName("addTask");
    connect(add, &QPushButton::clicked, this, &StatsDialog::addTask);
    toolbar->addWidget(add);
    auto* resetAll = new QPushButton(tr("Reset All"));
    resetAll->setObjectName("resetAllTasks");
    connect(resetAll, &QPushButton::clicked, this, &StatsDialog::resetAllTasks);
    toolbar->addWidget(resetAll);
    layout->addLayout(toolbar);
    // The time, the task (the description kept hidden, to search) and its
    // buttons; no headers, as upstream's.
    tasks_ = table({QString(), QString(), QString(), QString()});
    tasks_->setObjectName("maintenanceTasks");
    tasks_->horizontalHeader()->hide();
    tasks_->setColumnHidden(2, true);
    tasks_->setSelectionMode(QAbstractItemView::NoSelection);
    tasks_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tasks_->horizontalHeader()->setStretchLastSection(false);
    tasks_->setColumnWidth(0, 90);
    tasks_->setColumnWidth(3, 90);
    connect(tasks_, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        if (const QTableWidgetItem* item = tasks_->item(row, 0)) {
            editTask(item->data(Qt::UserRole + 1).toInt());
        }
    });
    layout->addWidget(tasks_, 1);
    columns->addWidget(list, 2);

    QFrame* upcoming = card();
    auto* side = new QVBoxLayout(upcoming);
    side->addWidget(cardHeader(tr("Upcoming Maintenance")));
    upcomingPage_ = new QVBoxLayout;
    side->addLayout(upcomingPage_);
    side->addStretch(1);
    columns->addWidget(upcoming, 1);
    return page;
}

QWidget* StatsDialog::alarmsPage() {
    auto* page = new QWidget;
    auto* columns = new QHBoxLayout(page);
    columns->setSpacing(24);
    QFrame* list = card();
    auto* layout = new QVBoxLayout(list);
    layout->addWidget(cardHeader(tr("Alarms & Errors")));
    alarmsStack_ = new QStackedWidget;
    auto* empty = new QLabel(tr("No Alarms or Errors recorded. Hooray!"));
    empty->setAlignment(Qt::AlignCenter);
    empty->setStyleSheet(mutedStyle() + " font-size: 12pt;");
    alarmsStack_->addWidget(empty);
    alarms_ = table({QString(), QString()});
    alarms_->setObjectName("alarmList");
    alarms_->horizontalHeader()->hide();
    alarms_->setShowGrid(false);
    alarms_->setSelectionMode(QAbstractItemView::NoSelection);
    alarms_->setColumnWidth(0, 56);
    alarmsStack_->addWidget(alarms_);
    layout->addWidget(alarmsStack_, 1);
    columns->addWidget(list, 2);

    auto* side = new QVBoxLayout;
    side->addWidget(diagnosticCard(true));
    QFrame* clearCard = card();
    auto* clearLayout = new QVBoxLayout(clearCard);
    clearLayout->addWidget(cardHeader(tr("Clear Alarms & Errors")));
    clearLayout->addWidget(muted(tr("Clear all prior alarms and errors. This action cannot be undone.")));
    auto* clear = new QPushButton(style()->standardIcon(QStyle::SP_TrashIcon), tr("Clear Alarms && Errors"));
    clear->setObjectName("clearAlarms");
    clear->setMinimumHeight(40);
    connect(clear, &QPushButton::clicked, this, &StatsDialog::clearAlarms);
    clearLayout->addWidget(clear);
    side->addWidget(clearCard);
    side->addStretch(1);
    columns->addLayout(side, 1);
    return page;
}

QWidget* StatsDialog::aboutPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(18);
    auto* top = new QHBoxLayout;
    auto* logo = new QLabel;
    logo->setPixmap(QPixmap(":/about/icon-square.png").scaled(125, 125, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    top->addWidget(logo);
    auto* title = new QVBoxLayout;
    title->addStretch(1);
    auto* name = new QLabel(tr("gSender"));
    name->setStyleSheet("font-size: 24pt; font-weight: 700;");
    title->addWidget(name);
    title->addWidget(muted(tr("By Sienci Labs")));
    auto* version = muted(tr("Version %1 (the C++ port)").arg(QApplication::applicationVersion()));
    version->setObjectName("aboutVersion");
    title->addWidget(version);
    title->addStretch(1);
    top->addLayout(title);
    top->addStretch(1);
    auto* legal = new QVBoxLayout;
    legal->addStretch(1);
    auto* copyright = muted(tr("Copyright © %1 Sienci Labs Inc.").arg(QDate::currentDate().year()));
    copyright->setWordWrap(false);
    copyright->setAlignment(Qt::AlignRight);
    legal->addWidget(copyright);
    auto* canada = new QHBoxLayout;
    canada->addStretch(1);
    canada->addWidget(muted(tr("Made in Canada")));
    auto* flag = new QLabel;
    flag->setPixmap(QPixmap(":/about/canada-flag-icon.png"));
    canada->addWidget(flag);
    legal->addLayout(canada);
    auto* license = new QLabel(QString("<a href='https://github.com/Sienci-Labs/gsender/blob/master/LICENSE' "
                                       "style='color: %1'>%2</a>")
                                   .arg(kBlue, tr("GNU GPLv3 License")));
    license->setOpenExternalLinks(true);
    license->setAlignment(Qt::AlignRight);
    legal->addWidget(license);
    legal->addStretch(1);
    top->addLayout(legal);
    layout->addLayout(top);

    auto* description = new QLabel(
        tr("gSender is a free and feature-packed CNC control software, designed to be clean and easy to learn while "
           "retaining a depth of capabilities for advanced users. Many thousands of people trust gSender to control "
           "their grbl and grblHAL-based CNCs every day, and they keep coming back for its ease of use, engaged "
           "community, and reliability."));
    description->setWordWrap(true);
    description->setStyleSheet("font-size: 12pt;");
    layout->addWidget(description);

    layout->addWidget(cardHeader(tr("gSender Team")));
    const std::pair<const char*, const char*> team[] = {
        {"Chris T.", "Project Lead"}, {"Kevin G.", "Lead Dev"}, {"Walid K.", "Dev Manager"}, {"Sophia B.", "Dev"},
        {"Shilpa G", "QA"},           {"Stephen C.", "Docs"},   {"Kelly Z.", "Icon Design"},
    };
    QStringList members;
    for (const auto& [member, role] : team) {
        members << QString("<b>%1</b> (%2)").arg(member, role);
    }
    auto* teamLabel = new QLabel(members.join(", "));
    teamLabel->setWordWrap(true);
    teamLabel->setStyleSheet("font-size: 12pt;");
    layout->addWidget(teamLabel);

    auto* notesHeader = new QHBoxLayout;
    notesHeader->addWidget(cardHeader(tr("Release Notes")), 1);
    auto* all = new QLabel(QString("<a href='https://github.com/Sienci-Labs/gsender' style='color: %1'>%2 "
                                   "&#8599;</a>")
                               .arg(kBlue, tr("See all latest updates made")));
    all->setOpenExternalLinks(true);
    notesHeader->addWidget(all);
    layout->addLayout(notesHeader);
    notes_ = new QTextBrowser;
    notes_->setObjectName("releaseNotes");
    notes_->setOpenExternalLinks(true);
    layout->addWidget(notes_, 1);

    // The notes as upstream renders them: markdown, a heading per release.
    const QJsonArray releases = QJsonDocument::fromJson(readReleaseNotes().toUtf8()).array();
    QStringList markdown;
    for (const QJsonValue& value : releases) {
        const QJsonObject release = value.toObject();
        const QString heading = release.value("version").toString() + " (" + release.value("date").toString() + ")";
        releases_ << heading;
        QStringList notes;
        for (const QJsonValue& note : release.value("notes").toArray()) {
            notes << "- " + note.toString();
        }
        markdown << "### " + heading + "\n\n" + notes.join('\n');
    }
    if (markdown.isEmpty()) {
        notes_->setPlainText(tr("No release notes found"));
    } else {
        notes_->setMarkdown(markdown.join("\n\n"));
    }
    return page;
}

// ---- filling in --------------------------------------------------------------------------------

void StatsDialog::reload() {
    config::ConfigStore& store = machine_.config();
    const config::JobStats stats = config::JobStatsStore(store).load();
    const std::vector<config::MaintenanceTask> tasks = config::MaintenanceStore(store).list();
    const std::vector<config::AlarmRecord> alarms = config::AlarmHistory(store).list();
    fillOverview(stats, tasks, alarms);
    fillJobs(stats);
    fillTasks(tasks);
    fillAlarms(alarms);
}

void StatsDialog::fillOverview(const config::JobStats& stats, const std::vector<config::MaintenanceTask>& tasks,
                               const std::vector<config::AlarmRecord>& alarms) {
    controller::Controller* c = machine_.controller();
    const bool connected = machine_.isConnected() && c;
    const std::string port = machine_.port().toStdString();
    // The jobs run on the connected port (StatsProvider's filteredJobs).
    const std::vector<config::JobRecord> portJobs =
        connected ? config::filterJobsByPort(stats.jobs, port) : std::vector<config::JobRecord>{};
    const config::JobResults results = config::calculateJobStats(portJobs);
    const bool chart = connected && !portJobs.empty();
    results_->setSlices({tr("Complete"), tr("Incomplete")},
                        {static_cast<double>(results.completeJobs), static_cast<double>(results.incompleteJobs)},
                        {QColor("#659dd2"), QColor("#C7813F")});
    results_->setVisible(chart);
    resultsEmpty_->setVisible(!chart);

    // StatTable
    clearLayout(statTable_);
    statRows_.clear();
    const auto row = [&](QVBoxLayout* layout, QStringList& texts, const QString& label, const QString& value) {
        const QString shown = connected ? value : QStringLiteral("-");
        layout->addWidget(configRow(label, "<b>" + shown.toHtmlEscaped() + "</b>"));
        texts << label + ": " + shown;
    };
    row(statTable_, statRows_, tr("Total jobs run"), QString::number(results.completeJobs + results.incompleteJobs));
    row(statTable_, statRows_, tr("Total cutting time"), qs(config::statTimeString(results.totalCutTime)));
    row(statTable_, statRows_, tr("Average job time"), qs(config::statTimeString(results.averageCutTime)));
    row(statTable_, statRows_, tr("Longest job"), qs(config::statTimeString(results.longestCutTime)));

    // Recent Jobs: the last five, whatever the port.
    clearLayout(recentList_);
    recentJobs_.clear();
    if (stats.jobs.empty()) {
        auto* empty = new QLabel(tr("No Jobs recorded. Get carving!"));
        empty->setAlignment(Qt::AlignCenter);
        empty->setMinimumHeight(120);
        empty->setStyleSheet(mutedStyle());
        recentList_->addWidget(empty);
    }
    int shown = 0;
    for (auto it = stats.jobs.rbegin(); it != stats.jobs.rend() && shown < 5; ++it, ++shown) {
        const bool complete = it->completed;
        const char* color = complete ? kGreen : kRed;
        const QString status = complete ? tr("Finished") : tr("Stopped");
        const QString duration = qs(config::previewDuration(static_cast<double>(it->duration)));
        auto* line = new QWidget;
        auto* rowLayout = new QHBoxLayout(line);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        auto* icon = new QLabel(complete ? "✓" : "✗");
        icon->setStyleSheet(QString("color: %1; font-size: 13pt;").arg(color));
        icon->setFixedWidth(22);
        icon->setAlignment(Qt::AlignCenter);
        rowLayout->addWidget(icon);
        const QString file = qs(it->file);
        auto* name = new QLabel(line->fontMetrics().elidedText(file, Qt::ElideRight, 180));
        name->setToolTip(file);
        name->setStyleSheet("font-weight: 700;");
        rowLayout->addWidget(name, 1);
        rowLayout->addWidget(new QLabel(duration));
        auto* badge = new QLabel(status);
        badge->setAlignment(Qt::AlignCenter);
        badge->setMinimumWidth(76);
        badge->setStyleSheet(QString("color: %1; border: 1px solid %1; background: %2; border-radius: 10px; "
                                     "padding: 2px 6px;")
                                 .arg(color, tint(color, 0.2)));
        rowLayout->addWidget(badge);
        recentList_->addWidget(line);
        recentJobs_ << file + " | " + duration + " | " + status;
    }

    // Upcoming Maintenance
    fillMaintenancePreview(upcomingList_, tasks, 3, &upcoming_);

    // Configuration
    const config::MachineProfile& profile = machine_.machineProfile();
    profile_->setText(QString("<b>%1 %2 </b>%3")
                          .arg(qs(profile.company).toHtmlEscaped(), qs(profile.name).toHtmlEscaped(),
                               qs(profile.type).toHtmlEscaped()));
    clearLayout(configurationList_);
    configuration_.clear();
    const auto setting = [c](const char* key) { return c ? c->runner().setting(key) : std::string(); };
    const auto enabled = [](bool on) { return on ? tr("Enabled") : tr("Disabled"); };
    const QString portText = qs(port);
    const QString connection =
        transport::looksLikeIpAddress(port)
            ? portText
            : tr("%1 at %2 baud").arg(qs(config::truncatePort(port))).arg(machine_.baudRate());
    QString axes;
    if (c) {
        const std::string letters = c->runner().state().axes.letters;
        QStringList list;
        for (const char letter : letters.empty() ? std::string("XYZ") : letters) {
            list << QString(QChar(letter));
        }
        axes = list.join(", ");
    }
    const double homing = js::stringToNumber(setting("$22"));
    row(configurationList_, configuration_, tr("Connection"), connection);
    row(configurationList_, configuration_, tr("Axes"), axes);
    row(configurationList_, configuration_, tr("Soft limits"), enabled(setting("$20") == "1"));
    row(configurationList_, configuration_, tr("Homing"), enabled(homing > 0));
    row(configurationList_, configuration_, tr("Home location"), qs(controller::homingString(setting("$23"))));
    row(configurationList_, configuration_, tr("Report inches"), enabled(setting("$13") == "1"));

    // Alarms & Errors: the latest four.
    clearLayout(alarmList_);
    alarmPreview_.clear();
    if (alarms.empty()) {
        auto* empty = new QLabel(tr("No Alarms or Errors recorded. Hooray!"));
        empty->setAlignment(Qt::AlignCenter);
        empty->setMinimumHeight(160);
        empty->setStyleSheet(mutedStyle());
        alarmList_->addWidget(empty);
    }
    for (std::size_t i = 0; i < alarms.size() && i < 4; ++i) {
        const config::AlarmRecord& alarm = alarms[i];
        const char* color = alarm.alarm ? kRed : kYellow;
        const QString what = QString("%1 %2").arg(alarm.alarm ? "ALARM" : "ERROR", qs(alarm.code));
        const QString when = tr("on %1").arg(qs(config::isoTime(alarm.time)));  // the record's time as stored
        auto* line = new QFrame;
        line->setObjectName("alarmRow");
        line->setStyleSheet(QString("QFrame#alarmRow { background: %1; border: none; border-left: 4px solid %2; "
                                    "border-radius: 3px; } QLabel { color: %2; }")
                                .arg(tint(color, 0.1), color));
        auto* rowLayout = new QHBoxLayout(line);
        rowLayout->addWidget(new QLabel(what));
        rowLayout->addStretch(1);
        rowLayout->addWidget(new QLabel(when));
        alarmList_->addWidget(line);
        alarmPreview_ << what + " | " + when;
    }
}

void StatsDialog::fillMaintenancePreview(QVBoxLayout* layout, const std::vector<config::MaintenanceTask>& tasks,
                                         int limit, QStringList* texts) {
    clearLayout(layout);
    texts->clear();
    for (const config::MaintenanceTask& task : config::upcomingMaintenance(tasks, static_cast<std::size_t>(limit))) {
        const auto [word, color] = reminder(task);
        const QString hours = tr("%1 hrs").arg(number(config::hoursUntilDue(task)));
        auto* item = new QFrame;
        item->setObjectName("maintenanceReminder");
        item->setStyleSheet(QString("QFrame#maintenanceReminder { background: %1; border-radius: 14px; }")
                                .arg(tint(color, 0.08)));
        auto* rowLayout = new QHBoxLayout(item);
        auto* left = new QVBoxLayout;
        auto* time = new QLabel(hours);
        time->setStyleSheet(QString("color: %1; font-size: 18pt;").arg(color));
        left->addWidget(time);
        auto* name = new QLabel(qs(task.name));
        name->setStyleSheet(mutedStyle());
        left->addWidget(name);
        rowLayout->addLayout(left, 1);
        auto* state = new QLabel(word + "  ●");
        state->setStyleSheet(QString("color: %1; font-size: 18pt;").arg(color));
        rowLayout->addWidget(state);
        layout->addWidget(item);
        *texts << hours + " | " + qs(task.name) + " | " + word;
    }
}

void StatsDialog::fillJobs(const config::JobStats& stats) {
    jobs_->setSortingEnabled(false);
    jobs_->clearContents();
    jobs_->setRowCount(static_cast<int>(stats.jobs.size()));
    int row = 0;
    for (auto it = stats.jobs.rbegin(); it != stats.jobs.rend(); ++it, ++row) {  // newest first
        const config::JobRecord& job = *it;
        auto* file = new QTableWidgetItem(qs(job.file));
        file->setToolTip(qs(job.path));
        // What the search looks through: the records' values (includesString).
        file->setData(Qt::UserRole + 2, QStringList{qs(job.file), number(static_cast<double>(job.duration)),
                                                    QString::number(job.totalLines), qs(config::isoTime(job.startTime)),
                                                    job.completed ? "COMPLETE" : "STOPPED"}
                                            .join('\n')
                                            .toLower());
        jobs_->setItem(row, 0, file);
        jobs_->setItem(row, 1,
                       new SortItem(qs(util::millisecondsToTimeStamp(static_cast<double>(job.duration))),
                                    static_cast<double>(job.duration)));
        jobs_->setItem(row, 2, new SortItem(QString::number(job.totalLines), static_cast<double>(job.totalLines)));
        jobs_->setItem(row, 3, new SortItem(enUsDateTime(job.startTime), static_cast<double>(job.startTime)));
        auto* status = new SortItem(job.completed ? "✓" : "✗", job.completed ? 1 : 0);
        status->setTextAlignment(Qt::AlignCenter);
        status->setForeground(QColor(job.completed ? "green" : "red"));
        status->setToolTip(job.completed ? tr("Complete") : tr("Stopped"));
        jobs_->setItem(row, 4, status);
    }
    jobs_->setSortingEnabled(true);  // by the header's choice
    filterRows(jobs_, jobSearch_->text());

    // Per CNC, the ports as the newest jobs first meet them (the context's
    // jobs are reversed).
    const std::vector<config::JobRecord> newestFirst(stats.jobs.rbegin(), stats.jobs.rend());
    std::vector<QString> labels;
    std::vector<double> counts;
    for (const auto& [port, count] : config::jobsPerPort(newestFirst)) {
        labels.push_back(qs(config::truncatePort(port)));
        counts.push_back(count);
    }
    jobsPerCnc_->setSlices(labels, counts,
                           {QColor("#7ca7d0"), QColor("#22415e"), QColor("#dc2626"), QColor("#bb6a0c"),
                            QColor("#3F85C7"), QColor("#059669")});
    labels.clear();
    std::vector<double> runTimes;
    for (const auto& [port, ms] : config::runTimePerPort(newestFirst)) {
        labels.push_back(qs(config::truncatePort(port)));
        runTimes.push_back(ms);
    }
    runTimePerCnc_->setSlices(labels, runTimes,
                              {QColor("#7ca7d0"), QColor("#dc2626"), QColor("#bb6a0c"), QColor("#3F85C7"),
                               QColor("#059669"), QColor("#22415e")});
}

void StatsDialog::fillTasks(const std::vector<config::MaintenanceTask>& tasks) {
    tasks_->clearContents();
    const std::vector<config::MaintenanceTask> ordered = config::maintenanceListOrder(tasks);
    tasks_->setRowCount(static_cast<int>(ordered.size()));
    for (int row = 0; row < static_cast<int>(ordered.size()); ++row) {
        const config::MaintenanceTask& task = ordered[static_cast<std::size_t>(row)];
        const QString name = qs(task.name);
        // determineTime(): the hours until due, "Due", or urgent.
        QString time;
        QString timeHtml;
        if (task.currentTime < task.rangeStart) {
            time = number(config::hoursUntilDue(task));
            timeHtml = QString("<span style='color: green'>%1</span>").arg(tr("%1 Hrs").arg(time));
        } else if (task.currentTime <= task.rangeEnd) {
            time = tr("Due");
            timeHtml = QString("<b style='color: #E15C00'>%1</b>").arg(time);
        } else {
            timeHtml = QString("<b style='color: red'>&#9888;<br>%1</b>").arg(tr("Urgent!"));
        }
        setRichCell(tasks_, row, 0, "<div align='center'>" + timeHtml + "</div>");
        QTableWidgetItem* first = tasks_->item(row, 0);
        first->setData(Qt::UserRole + 1, task.id);
        first->setData(Qt::UserRole + 2, QStringList{time, name, qs(task.description)}.join('\n').toLower());
        setRichCell(tasks_, row, 1,
                    QString("<b style='font-size: 12pt'>%1</b><br>%2")
                        .arg(name.toHtmlEscaped(), qs(task.description).toHtmlEscaped().replace('\n', "<br>")));
        tasks_->setItem(row, 2, new QTableWidgetItem(qs(task.description)));

        auto* actions = new QWidget;
        auto* layout = new QHBoxLayout(actions);
        layout->setContentsMargins(4, 0, 4, 0);
        auto* done = new QToolButton;
        done->setText("✓");
        done->setObjectName("resetTask");
        done->setStyleSheet(QString("QToolButton { color: %1; font-size: 14pt; border: none; }").arg(kGreen));
        done->setToolTip(tr("Reset maintenance timer for %1").arg(name));
        done->setAccessibleName(done->toolTip());
        const int id = task.id;
        connect(done, &QToolButton::clicked, this, [this, id] { resetTask(id); });
        layout->addWidget(done);
        auto* edit = new QToolButton;
        edit->setText("✎");
        edit->setObjectName("editTask");
        edit->setStyleSheet("QToolButton { font-size: 14pt; border: none; }");
        edit->setToolTip(tr("Edit maintenance task for %1").arg(name));
        edit->setAccessibleName(edit->toolTip());
        connect(edit, &QToolButton::clicked, this, [this, id] { editTask(id); });
        layout->addWidget(edit);
        tasks_->setCellWidget(row, 3, actions);
    }
    tasks_->resizeRowsToContents();
    filterRows(tasks_, taskSearch_->text());
    fillMaintenancePreview(upcomingPage_, tasks, 6, &upcomingPageTexts_);
}

void StatsDialog::fillAlarms(const std::vector<config::AlarmRecord>& alarms) {
    alarms_->clearContents();
    alarms_->setRowCount(static_cast<int>(alarms.size()));
    for (int row = 0; row < static_cast<int>(alarms.size()); ++row) {
        const config::AlarmRecord& alarm = alarms[static_cast<std::size_t>(row)];
        const char* color = alarm.alarm ? kRed : "#f97316";  // red-500 / orange-500
        auto* icon = new QLabel(alarm.alarm ? "✗" : "⚠");
        icon->setAlignment(Qt::AlignCenter);
        icon->setStyleSheet(QString("color: %1; font-size: 20pt;").arg(color));
        alarms_->setItem(row, 0, new QTableWidgetItem);
        alarms_->setCellWidget(row, 0, icon);
        const QString title = QString("%1 %2 - %3").arg(alarm.alarm ? "ALARM" : "ERROR", qs(alarm.code), qs(alarm.source));
        const QString message = alarm.message.empty() ? tr("No associated message") : qs(alarm.message);
        setRichCell(alarms_, row, 1,
                    QString("<span style='color: %1; font-size: 12pt; font-weight: 600'>%2</span><br>"
                            "<span style='color: gray'>%3</span><br>%4<br>%5 <b>%6</b>")
                        .arg(color, title.toHtmlEscaped(), tr("at %1").arg(enUsDateTime(alarm.time)),
                             message.toHtmlEscaped(), tr("Line:"), qs(alarm.line).toHtmlEscaped()));
    }
    alarms_->resizeRowsToContents();
    alarmsStack_->setCurrentIndex(alarms.empty() ? 0 : 1);
}

// ---- actions -----------------------------------------------------------------------------------

void StatsDialog::searchJobs(const QString& text) {
    jobSearch_->setText(text);
}

void StatsDialog::clearJobHistory() {
    if (confirm(tr("Delete Job History"), tr("Are you sure you want to delete all job history?"))) {
        config::JobStatsStore(machine_.config()).clear();
        reload();
    }
}

void StatsDialog::searchTasks(const QString& text) {
    taskSearch_->setText(text);
}

void StatsDialog::resetTask(int id) {
    for (const config::MaintenanceTask& task : config::MaintenanceStore(machine_.config()).list()) {
        if (task.id != id) {
            continue;
        }
        if (confirm(tr("Reset Maintenance Timer"),
                    tr("Are you sure you want to reset the maintenance timer for %1? Only do this if you have just "
                       "performed this maintenance task.")
                        .arg(qs(task.name)))) {
            config::MaintenanceStore(machine_.config()).markDone(id);
            reload();
        }
        return;
    }
}

void StatsDialog::resetAllTasks() {
    if (confirm(tr("Reset All Tasks"), tr("Are you sure you want to reset the times for every Maintenance Task?"))) {
        config::MaintenanceStore(machine_.config()).resetAll();
        reload();
    }
}

void StatsDialog::addTask() {
    MaintenanceTaskDialog dialog(nullptr, confirmer_, this);
    if (dialog.exec() == QDialog::Accepted) {
        config::MaintenanceStore(machine_.config()).add(dialog.task());
        reload();
    }
}

void StatsDialog::editTask(int id) {
    config::MaintenanceStore store(machine_.config());
    for (const config::MaintenanceTask& task : store.list()) {
        if (task.id != id) {
            continue;
        }
        MaintenanceTaskDialog dialog(&task, confirmer_, this);
        const int result = dialog.exec();
        if (result == QDialog::Accepted) {
            store.update(dialog.task());
        } else if (result == MaintenanceTaskDialog::kDeleted) {
            store.remove(id);
        }
        reload();
        return;
    }
}

void StatsDialog::clearAlarms() {
    if (confirm(tr("Delete History"), tr("Are you sure you want to delete all alarm/error history?"))) {
        config::AlarmHistory(machine_.config()).clear();
        reload();
    }
}

}  // namespace gs::app
